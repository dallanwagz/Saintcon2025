// BTHome v2 button events over a short non-connectable advert: Home Assistant (Bluetooth integration or an
// ESPHome Bluetooth proxy) auto-discovers the badge and exposes one event entity per stick direction.
// Format: https://bthome.io/format/  (service data UUID 0xFCD2; objects in rising id order)
#include <assert.h>
#include <string.h>
#include "esp_log.h"
#include "bthome.h"
#include "hid.h"

static const char *TAG = "bthome";

#define BTHOME_BURST_MS 1000 // ESPHome proxies scan a 30 ms window every 320 ms: a 300 ms burst was missed 1 in 6 on a Mac

size_t bthome_pack_buttons(uint8_t out[31], uint8_t packet_id, uint8_t battery, const uint8_t events[BTHOME_BUTTONS]) {
    size_t n = 0;
    out[n++] = 0x02; out[n++] = 0x01; out[n++] = 0x06;      // flags
    size_t sd = n++;                                        // service data length, filled below
    out[n++] = 0x16; out[n++] = 0xD2; out[n++] = 0xFC;      // Service Data, UUID 0xFCD2
    out[n++] = 0x44;                                        // BTHome v2, unencrypted, trigger-based device
    out[n++] = 0x00; out[n++] = packet_id;                  // packet id: receivers drop repeats of the same id
    out[n++] = 0x01; out[n++] = battery;                    // battery %
    for (int i = 0; i < BTHOME_BUTTONS; i++) {              // button 1..5 = up, down, left, right, center
        out[n++] = 0x3A; out[n++] = events[i];
    }
    out[sd] = (uint8_t)(n - sd - 1);
    static const char name[] = CONFIG_BADGE_SHORT_NAME;     // shortened local name: what HA shows
    _Static_assert(sizeof(name) - 1 <= 7, "BADGE_SHORT_NAME must be 7 characters or fewer to fit the advert");
    out[n++] = (uint8_t)(1 + sizeof(name) - 1); out[n++] = 0x08;
    memcpy(out + n, name, sizeof(name) - 1); n += sizeof(name) - 1;
    assert(n <= 31);
    return n;
}

static uint8_t s_pid = 0; // receivers drop repeats of the same id, so every broadcast gets a fresh one

void bthome_button(int button, uint8_t event) {
    uint8_t events[BTHOME_BUTTONS] = {0};
    if (button < 0 || button >= BTHOME_BUTTONS) {
        return;
    }
    events[button] = event;
    uint8_t adv[31];
    size_t n = bthome_pack_buttons(adv, ++s_pid, hid_battery_percent(), events);
    esp_err_t err = hid_broadcast(adv, (uint8_t)n, BTHOME_BURST_MS);
    ESP_LOGI(TAG, "button %d event 0x%02x pid %u -> %s", button + 1, event, s_pid, err == ESP_OK ? "broadcast" : "radio busy");
}

size_t bthome_pack_command(uint8_t out[31], uint8_t packet_id, const char *cmd, int steps) {
    size_t len = strnlen(cmd, BTHOME_CMD_MAX);
    int mag    = steps < 0 ? -steps : steps;
    if (mag > 255) {
        mag = 255;
    }
    // ponytail: no AD flags and no name. Flags are optional in a non-connectable advert and HA already knows the
    // badge's address from the button adverts; the 11 bytes saved go to the command text.
    size_t n  = 0;
    size_t sd = n++;                                        // service data length, filled below
    out[n++] = 0x16; out[n++] = 0xD2; out[n++] = 0xFC;      // Service Data, UUID 0xFCD2
    out[n++] = 0x44;                                        // BTHome v2, unencrypted, trigger-based device
    out[n++] = 0x00; out[n++] = packet_id;                  // packet id
    out[n++] = 0x3C; out[n++] = steps < 0 ? 0x01 : 0x02;    // dimmer event: rotate_left / rotate_right ...
    out[n++] = (uint8_t)mag;                                // ... with this many steps (HA: event attribute "steps")
    out[n++] = 0x53; out[n++] = (uint8_t)len;               // text: the command
    memcpy(out + n, cmd, len); n += len;
    out[sd] = (uint8_t)(n - sd - 1);
    assert(n <= 31);
    return n;
}

void bthome_command(const char *cmd, int steps) {
    uint8_t adv[31];
    size_t n = bthome_pack_command(adv, ++s_pid, cmd, steps);
    esp_err_t err = hid_broadcast(adv, (uint8_t)n, BTHOME_BURST_MS);
    ESP_LOGI(TAG, "command '%.*s' steps %d pid %u -> %s", BTHOME_CMD_MAX, cmd, steps, s_pid, err == ESP_OK ? "broadcast" : "radio busy");
}
