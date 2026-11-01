#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * BLE HID device: one combined report map with a keyboard (report 1), a mouse (report 2) and a
 * consumer control (report 3). Pairing is Just Works with bonding persisted in NVS.
 * All send functions are safe to call from any task and are no-ops while no host is connected.
 */

typedef enum {
    HID_STATE_IDLE,        // stack not started / stopped
    HID_STATE_ADVERTISING, // waiting for a host
    HID_STATE_CONNECTED,   // host connected (may still be pairing / subscribing)
    HID_STATE_USB_CONNECTED, // a USB host has mounted us; USB wins over BLE for reports while it lasts
} hid_state_t;

typedef void (*hid_state_cb_t)(hid_state_t state);

esp_err_t hid_init(const char *device_name);
hid_state_t hid_get_state(void);
void hid_set_state_callback(hid_state_cb_t cb); // called from the BLE host task

// Keyboard (report 1). One key at a time is plenty for a joystick.
void hid_key_press(uint8_t modifiers, uint8_t keycode);
void hid_key_release(void);
void hid_key_tap(uint8_t modifiers, uint8_t keycode);

// Consumer control (report 3)
void hid_consumer_press(uint16_t usage);
void hid_consumer_release(void);
void hid_consumer_tap(uint16_t usage);

// Mouse (report 2)
void hid_mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);

void hid_set_battery(uint8_t percent);

/**
 * Radio sharing: while suspended HID stops advertising, drops the host link and ignores the HID stack's
 * connect/disconnect events, so another BLE role can own the controller.
 * hid_resume() starts advertising again; bonded hosts reconnect on their own.
 */
void hid_suspend(void);
void hid_resume(void);
bool hid_suspended(void);

/** Address of the connected host as "AA:BB:CC:DD:EE:FF" (false if none). */
bool hid_host_addr(char out[18]);
/** Drop the host link now (a bonded host will usually reconnect while advertising continues). */
void hid_disconnect_host(void);
/** Bonded hosts as address strings. Returns how many were written (<= max). */
int hid_bonded_hosts(char (*out)[18], int max);
/** Remove one bond by its index in hid_bonded_hosts(); disconnects it if connected. */
esp_err_t hid_forget_host(int index);
/** Drop all bonds so a new host can pair. */
esp_err_t hid_forget_bonds(void);

/**
 * Broadcast a raw legacy advert (non-connectable, non-discoverable) for `ms`, then go back to normal.
 * Works while a host is connected (a NONCONN advert needs no connection slot); while disconnected it
 * borrows the radio from HID advertising, which resumes when the burst ends. Used for BTHome events.
 */
esp_err_t hid_broadcast(const uint8_t *adv, uint8_t len, uint32_t ms);
/** Last battery percent handed to hid_set_battery() (100 until the first reading). */
uint8_t hid_battery_percent(void);

/**
 * USB transport (TinyUSB keyboard/mouse/consumer + a CDC console). Takes the USB port away from
 * USB-Serial/JTAG: logs move to the CDC port, and flashing needs tools/enter_bootloader.py (1200-baud
 * touch) or the centre button held at power-on. Turning it off again is a reboot.
 */
esp_err_t hid_usb_start(void);
bool hid_usb_active(void);  // driver installed this boot
bool hid_usb_mounted(void); // a host has enumerated us
/** Hand the internal USB PHY back to USB-Serial/JTAG. The mux survives soft resets, so this runs on every restart and early at boot. */
void hid_usb_release_phy(void);

// USB HID keyboard usage IDs (Keyboard/Keypad page)
#define HID_KEY_B         0x05
#define HID_KEY_ENTER     0x28
#define HID_KEY_ESC       0x29
#define HID_KEY_SPACE     0x2C
#define HID_KEY_F5        0x3E
#define HID_KEY_PAGE_UP   0x4B
#define HID_KEY_PAGE_DOWN 0x4E
#define HID_KEY_RIGHT     0x4F
#define HID_KEY_LEFT      0x50
#define HID_KEY_DOWN      0x51
#define HID_KEY_UP        0x52
#define HID_KEY_F13       0x68 // F13..F24 are 0x68..0x73: keys no real keyboard sends, so hosts can bind them freely

// Consumer page usages
#define HID_CONSUMER_PLAY_PAUSE 0x00CD
#define HID_CONSUMER_SCAN_NEXT  0x00B5
#define HID_CONSUMER_SCAN_PREV  0x00B6
#define HID_CONSUMER_MUTE       0x00E2
#define HID_CONSUMER_VOL_UP     0x00E9
#define HID_CONSUMER_VOL_DOWN   0x00EA

#define HID_MOUSE_BUTTON_LEFT  0x01
#define HID_MOUSE_BUTTON_RIGHT 0x02
#define HID_MOUSE_BUTTON(n)    (1u << ((n) - 1)) // 1..8

#ifdef __cplusplus
}
#endif
