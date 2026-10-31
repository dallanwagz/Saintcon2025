// Between the two HID transports; not for the apps.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** USB side: queue one input report (no-op unless a host has mounted us). */
void hid_usb_send_report(uint8_t report_id, const uint8_t *data, size_t len);
/** BLE side: re-announce hid_get_state() to the app after a USB mount/unmount. */
void hid_transport_changed(void);
