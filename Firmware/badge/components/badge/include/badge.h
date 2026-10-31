#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

#include "nvs.h"

// Component headers to expose externally
#include "../config.h"

/**
 * @brief Initialize the badge: load config from NVS, mount SPIFFS, apply the screen timeout.
 */
esp_err_t badge_init(void);

/**
 * @brief Set and persist the screen timeout (seconds, 0 = never) and apply it immediately.
 */
esp_err_t set_screen_timeout(uint32_t timeout);

/**
 * @brief Boot-time deferred tasks to run once the main screen is visible (e.g. fade out the boot LED bar).
 *        Implemented in main.c.
 */
void badge_boot_deferred_tasks(void);

#ifdef __cplusplus
}
#endif
