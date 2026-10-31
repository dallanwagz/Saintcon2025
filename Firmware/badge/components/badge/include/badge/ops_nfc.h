#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// What a phone reads when it taps the badge until an app writes something else
#define BADGE_NFC_URL "https://github.com/compukidmike/Saintcon2025"

esp_err_t badge_nfc_init(void);
void badge_nfc_deinit(void);

#ifdef __cplusplus
}
#endif
