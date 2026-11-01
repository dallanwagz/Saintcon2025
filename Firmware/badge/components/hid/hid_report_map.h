// The one HID report map both transports (BLE via esp_hidd, USB via TinyUSB) describe themselves with.
// static const on purpose: the USB configuration descriptor needs sizeof() at compile time.
#pragma once
#include <stdint.h>

#define HID_RPT_KEYBOARD 1
#define HID_RPT_MOUSE    2
#define HID_RPT_CONSUMER 3

// clang-format off
static const uint8_t hid_report_map[] = {
    // Keyboard, report 1 (boot-compatible layout: modifiers, reserved, 6 keys) + LED output report
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, HID_RPT_KEYBOARD,
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       //   Usage Minimum (Left Control)
    0x29, 0xE7,       //   Usage Maximum (Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)   modifiers
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Constant)                   reserved
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (Num Lock)
    0x29, 0x05,       //   Usage Maximum (Kana)
    0x91, 0x02,       //   Output (Data, Variable, Absolute)  LEDs
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x03,       //   Report Size (3)
    0x91, 0x01,       //   Output (Constant)                  padding
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x73,       //   Logical Maximum (115)              up to F24, for macro keys
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0x73,       //   Usage Maximum (115)
    0x81, 0x00,       //   Input (Data, Array)                keys
    0xC0,             // End Collection
    // Mouse, report 2
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x85, HID_RPT_MOUSE,
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Button)
    0x19, 0x01,       //     Usage Minimum (1)
    0x29, 0x08,       //     Usage Maximum (8)                buttons 4-8 are free for automations
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x08,       //     Report Count (8)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data, Variable, Absolute) buttons
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x09, 0x38,       //     Usage (Wheel)
    0x15, 0x81,       //     Logical Minimum (-127)
    0x25, 0x7F,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x03,       //     Report Count (3)
    0x81, 0x06,       //     Input (Data, Variable, Relative)
    0xC0,             //   End Collection
    0xC0,             // End Collection
    // Consumer control, report 3
    0x05, 0x0C,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage (Consumer Control)
    0xA1, 0x01,       // Collection (Application)
    0x85, HID_RPT_CONSUMER,
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x03, //   Logical Maximum (1023)
    0x19, 0x00,       //   Usage Minimum (0)
    0x2A, 0xFF, 0x03, //   Usage Maximum (1023)
    0x75, 0x10,       //   Report Size (16)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x00,       //   Input (Data, Array)
    0xC0,             // End Collection
};
// clang-format on
