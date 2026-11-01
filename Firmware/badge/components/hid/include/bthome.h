#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTHOME_BUTTONS 5 // up, down, left, right, center -> HA button_1..button_5
#define BTHOME_PRESS   0x01
#define BTHOME_LONG    0x04
#define BTHOME_CMD_MAX 19 // text bytes that fit next to the packet id and the dimmer event in a 31-byte advert

/** Build the advert (<= 31 bytes) for one set of button events; returns its length. Pure, for self-checks. */
size_t bthome_pack_buttons(uint8_t out[31], uint8_t packet_id, uint8_t battery, const uint8_t events[BTHOME_BUTTONS]);
/** Broadcast one button event (0-based button, BTHOME_PRESS / BTHOME_LONG) for ~300 ms. */
void bthome_button(int button, uint8_t event);

/**
 * Menu command advert: a BTHome "dimmer" event (rotate_right for steps > 0, rotate_left for < 0, |steps| 1..255)
 * plus a "text" object holding cmd (truncated to BTHOME_CMD_MAX). Home Assistant fires event.<badge>_dimmer with
 * a `steps` attribute and sets sensor.<badge>_text to cmd from the same advert. Automations trigger on the event
 * and condition on its own `steps` attribute (ha/wrench_menu_command.yaml): HA starts the automation eagerly,
 * inside the callback that fires the event, before the text sensor from the same advert is written, so the text
 * is for the logbook only. Returns the advert length. Pure, for self-checks.
 */
size_t bthome_pack_command(uint8_t out[31], uint8_t packet_id, const char *cmd, int steps);
/** Broadcast one menu command (see bthome_pack_command) for ~1 s. */
void bthome_command(const char *cmd, int steps);

#ifdef __cplusplus
}
#endif
