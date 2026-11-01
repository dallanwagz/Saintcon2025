// USB transport: the same keyboard / mouse / consumer report map over TinyUSB, plus a CDC port that
// carries the console. The S3 has one internal USB PHY shared by USB-Serial/JTAG (flashing, console)
// and the OTG controller TinyUSB needs, so while this runs the usual idf.py port is gone:
//   - reflash: open the CDC port at 1200 baud (tools/enter_bootloader.py); we hand the PHY back and
//     reboot into the ROM download mode, where the usual USB-Serial/JTAG port reappears
//   - the PHY mux lives in RTC registers and survives soft resets, so hid_usb_release_phy() runs from a
//     shutdown handler and again early at boot (main.c), or a crash-reboot would come up with no console
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_cntl_struct.h"
#include "soc/soc.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "class/hid/hid_device.h"

#include "hid.h"
#include "hid_internal.h"
#include "hid_report_map.h"

static const char *TAG = "hid_usb";

static bool s_active;            // driver installed
static volatile bool s_mounted;  // host enumerated and not suspended

// ---- descriptors: one HID interface + one CDC (two interfaces) ----
enum { ITF_NUM_HID = 0, ITF_NUM_CDC, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };
#define EPNUM_HID_IN    0x81
#define EPNUM_CDC_NOTIF 0x82
#define EPNUM_CDC_OUT   0x03
#define EPNUM_CDC_IN    0x83
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t s_config_desc[] = {
    // config 1, 3 interfaces, no string, total length, remote wakeup, 100 mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // HID: interface, string 4, no boot protocol (three report IDs), report map length, EP IN, 16 B, 10 ms
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 4, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_map), EPNUM_HID_IN, 16, 10),
    // CDC: interface pair, string 5, notification EP, data OUT/IN, 64 B
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

static char s_serial[13];
static char s_product[32];
static const char *s_strings[] = {
    (char[]){0x09, 0x04}, // 0: English (US)
    "Saintcon",           // 1: manufacturer
    s_product,            // 2: product ("<owner> Wrench")
    s_serial,             // 3: serial (base MAC)
    "SC25 Wrench joystick", // 4: HID interface
    "SC25 Wrench console",  // 5: CDC interface
};

// ---- TinyUSB callbacks (TinyUSB task) ----
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_map;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0; // nothing to read back; the stack STALLs the request
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize; // keyboard LED state: ignored
}

void tud_mount_cb(void) {
    s_mounted = true;
    ESP_LOGI(TAG, "USB host mounted us");
    hid_transport_changed();
}

void tud_umount_cb(void) {
    s_mounted = false;
    ESP_LOGI(TAG, "USB host gone");
    hid_transport_changed();
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    s_mounted = false; // host asleep: reports would only pile up
    hid_transport_changed();
}

void tud_resume_cb(void) {
    s_mounted = true;
    hid_transport_changed();
}

// The 1200-baud touch: a host opening the console at 1200 bps asks for the bootloader (same convention as
// Arduino boards). Hand the PHY back to USB-Serial/JTAG and reboot into the ROM download mode.
static void on_line_coding(int itf, cdcacm_event_t *event) {
    (void)itf;
    if (event->line_coding_changed_data.p_line_coding->bit_rate != 1200) {
        return;
    }
    ESP_LOGW(TAG, "1200 baud touch: rebooting into the ROM download mode");
    vTaskDelay(pdMS_TO_TICKS(100));
    hid_usb_release_phy();
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

// ---- public ----
void hid_usb_release_phy(void) {
    // TinyUSB routes the internal FSLS PHY to the OTG controller via RTCCNTL.usb_conf (RTC domain: it survives
    // a soft reset and nothing in the driver undoes it). Put it back on USB-Serial/JTAG and let the hardware
    // default (eFuse) own the mux again, exactly as after a power-on.
    usb_serial_jtag_ll_phy_enable_external(false);
    RTCCNTL.usb_conf.sw_hw_usb_phy_sel = 0;
}

esp_err_t hid_usb_start(void) {
    if (s_active) {
        return ESP_OK;
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BASE);
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_product, sizeof(s_product), "%s", hid_device_name());

    const tinyusb_config_t cfg = {
        .device_descriptor        = NULL, // Kconfig default: Espressif VID, PID from the enabled classes
        .string_descriptor        = s_strings,
        .string_descriptor_count  = sizeof(s_strings) / sizeof(s_strings[0]),
        .external_phy             = false,
        .configuration_descriptor = s_config_desc,
    };
    ESP_LOGW(TAG, "starting TinyUSB: the USB-Serial/JTAG console and flash port go away now; logs continue on the CDC port");
    esp_register_shutdown_handler(hid_usb_release_phy); // every esp_restart() leaves the PHY on USB-Serial/JTAG
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&cfg), TAG, "tinyusb install");

    const tinyusb_config_cdcacm_t acm = {
        .usb_dev                      = TINYUSB_USBDEV_0,
        .cdc_port                     = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz             = 64,
        .callback_rx                  = NULL,
        .callback_rx_wanted_char      = NULL,
        .callback_line_state_changed  = NULL,
        .callback_line_coding_changed = on_line_coding,
    };
    ESP_RETURN_ON_ERROR(tusb_cdc_acm_init(&acm), TAG, "cdc init");
    esp_tusb_init_console(TINYUSB_CDC_ACM_0); // stdout/stderr (and so ESP_LOG) now go to the CDC port
    s_active = true;
    ESP_LOGI(TAG, "USB HID + CDC console up (serial %s)", s_serial);
    return ESP_OK;
}

bool hid_usb_active(void) {
    return s_active;
}

bool hid_usb_mounted(void) {
    return s_active && s_mounted && tud_mounted();
}

void hid_usb_send_report(uint8_t report_id, const uint8_t *data, size_t len) {
    if (!hid_usb_mounted()) {
        return;
    }
    for (int i = 0; i < 20 && !tud_hid_ready(); i++) {
        vTaskDelay(1); // the previous report is still in flight (10 ms polling interval)
    }
    bool ok = tud_hid_report(report_id, data, (uint16_t)len);
    ESP_LOGI(TAG, "report %u [%02x %02x ..] -> %s", report_id, data[0], len > 1 ? data[1] : 0, ok ? "sent" : "busy");
}
