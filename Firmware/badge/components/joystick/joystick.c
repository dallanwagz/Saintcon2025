#include "joystick.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "joystick";

// ponytail: fixed timing; make these Kconfig if a profile ever needs different feel
#define JOY_POLL_MS       10
#define JOY_DEBOUNCE_N    2 // consecutive identical samples before a change is accepted (20 ms)
#define JOY_HOLD_MS       500
#define JOY_REPEAT_MS     100
#define JOY_MAX_LISTENERS 4
#define JOY_TASK_STACK    4096 // listeners may send BLE notifications from this task

static const gpio_num_t joy_gpios[JOY_DIR_COUNT] = {
    [JOY_UP] = CONFIG_INPUT_JOY_UP_GPIO,       [JOY_DOWN] = CONFIG_INPUT_JOY_DOWN_GPIO,
    [JOY_LEFT] = CONFIG_INPUT_JOY_LEFT_GPIO,   [JOY_RIGHT] = CONFIG_INPUT_JOY_RIGHT_GPIO,
    [JOY_CENTER] = CONFIG_INPUT_JOY_CENTER_GPIO,
};

static volatile uint8_t s_state = 0;
static struct {
    joystick_listener_t cb;
    void *arg;
} s_listeners[JOY_MAX_LISTENERS];
static TaskHandle_t s_task = NULL;

static void emit(joystick_dir_t dir, joystick_event_type_t type, uint32_t held_ms) {
    joystick_event_t ev = {.dir = dir, .type = type, .held_ms = held_ms};
    for (int i = 0; i < JOY_MAX_LISTENERS; i++) {
        if (s_listeners[i].cb) {
            s_listeners[i].cb(&ev, s_listeners[i].arg);
        }
    }
}

static uint8_t read_raw(void) {
    uint8_t s = 0;
    for (int d = 0; d < JOY_DIR_COUNT; d++) {
        if (gpio_get_level(joy_gpios[d]) == 0) { // active low
            s |= JOY_BIT(d);
        }
    }
    return s;
}

static void joystick_task(void *arg) {
    (void)arg;
    uint8_t candidate = 0;
    int stable        = 0;
    uint32_t press_ms[JOY_DIR_COUNT]       = {0};
    uint32_t next_repeat_ms[JOY_DIR_COUNT] = {0};
    bool held[JOY_DIR_COUNT]               = {false};
    TickType_t last_wake                   = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(JOY_POLL_MS));
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

        // Debounce: accept a new state once it has been seen JOY_DEBOUNCE_N times in a row
        uint8_t raw = read_raw();
        if (raw != candidate) {
            candidate = raw;
            stable    = 0;
        } else if (stable < JOY_DEBOUNCE_N) {
            stable++;
        }
        if (stable == JOY_DEBOUNCE_N && candidate != s_state) {
            uint8_t changed = candidate ^ s_state;
            s_state         = candidate;
            for (int d = 0; d < JOY_DIR_COUNT; d++) {
                if (!(changed & JOY_BIT(d))) {
                    continue;
                }
                if (candidate & JOY_BIT(d)) {
                    press_ms[d]       = now;
                    next_repeat_ms[d] = now + JOY_REPEAT_MS;
                    held[d]           = false;
                    emit(d, JOY_EVENT_PRESS, 0);
                } else {
                    emit(d, JOY_EVENT_RELEASE, now - press_ms[d]);
                }
            }
        }

        // Hold / repeat for everything currently down
        for (int d = 0; d < JOY_DIR_COUNT; d++) {
            if (!(s_state & JOY_BIT(d))) {
                continue;
            }
            uint32_t h = now - press_ms[d];
            if (!held[d] && h >= JOY_HOLD_MS) {
                held[d] = true;
                emit(d, JOY_EVENT_HOLD, h);
            }
            if ((int32_t)(now - next_repeat_ms[d]) >= 0) {
                next_repeat_ms[d] += JOY_REPEAT_MS;
                emit(d, JOY_EVENT_REPEAT, h);
            }
        }
    }
}

esp_err_t joystick_init(void) {
    if (s_task) {
        return ESP_OK;
    }
    uint64_t pin_mask = 0;
    for (int d = 0; d < JOY_DIR_COUNT; d++) {
        pin_mask |= (1ULL << joy_gpios[d]);
    }
    gpio_config_t io = {
        .pin_bit_mask = pin_mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config failed");

    // Any direction wakes the chip from light sleep
    for (int d = 0; d < JOY_DIR_COUNT; d++) {
        gpio_wakeup_enable(joy_gpios[d], GPIO_INTR_LOW_LEVEL);
    }
    if (esp_sleep_enable_gpio_wakeup() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable GPIO wakeup - wakeup triggers conflict");
    }

    s_state = read_raw();
    if (xTaskCreate(joystick_task, "joystick", JOY_TASK_STACK, NULL, 5, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Joystick polling started (up=%d down=%d left=%d right=%d center=%d)", joy_gpios[JOY_UP], joy_gpios[JOY_DOWN],
             joy_gpios[JOY_LEFT], joy_gpios[JOY_RIGHT], joy_gpios[JOY_CENTER]);
    return ESP_OK;
}

uint8_t joystick_get_state(void) {
    return s_state;
}

esp_err_t joystick_add_listener(joystick_listener_t cb, void *arg) {
    for (int i = 0; i < JOY_MAX_LISTENERS; i++) {
        if (s_listeners[i].cb == NULL) {
            s_listeners[i].cb  = cb;
            s_listeners[i].arg = arg;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}
