#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "lvgl.h"

/** True when this badge has shortcuts (CONFIG_BADGE_SHORTCUTS is not empty): the menu only lists the app then. */
bool shortcuts_available(void);
void shortcuts_app_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
