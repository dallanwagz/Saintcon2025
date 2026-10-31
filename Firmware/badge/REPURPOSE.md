# Saintcon 2025 badge: repurpose plan

Goal: keep every hardware driver (LEDs, LCD, joystick, NFC tag, minibadge bus, battery, WiFi),
delete the conference game, and turn the badge into a joystick HID device with room left over
for new apps.

## 1. What is on the board (from `Hardware/Badge/Saintcon2025-Schematic.pdf` + `sdkconfig.defaults`)

| Block | Part | Pins | Notes |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1 | 16 MB flash (QIO/120 MHz), PSRAM | 240 MHz, dual core |
| USB-C | J1 | D- GPIO19, D+ GPIO20 | Wired to the S3 **native USB OTG**. USB HID and USB-Serial/JTAG both possible (not at the same time). |
| LEDs | 20x WS2812B-2020 | data GPIO3 | Chain order: index 0 top-left, 9 bottom-left, 10 bottom-right, 19 top-right (see `main.c` boot bar). |
| Joystick | 4-way + center | UP 41, DOWN 43, LEFT 2, RIGHT 42, CENTER 44 | Active low, internal pull-ups. **GPIO43/44 are UART0 TX/RX**, so a UART0 console fights the joystick. |
| LCD | ST7789 240x320, 8-bit i80 | D0-7: 35,48,47,21,14,13,12,11; RS 38, CS 39, RD 36, WR 37, RST 40 | Backlight PWM GPIO18 via A3910 driver. |
| NFC | ST25DV04K dynamic tag | I2C bus A (SDA 10, SCL 9), GPO GPIO6 | The badge is a *tag*, not a reader. Phone reads/writes NDEF; GPO fires on RF write. Mailbox (FTM) unused so far. |
| Secure element | ATECC608B | I2C bus A | Only used for game-API HMAC + storing conference WiFi creds. |
| Minibadges | 3 slots | I2C bus B (SDA 7, SCL 15), CLK GPIO16/17 | EEPROM ID at 0x50, D-pad minibadge at 0x38. |
| Battery | 2x AA + XC9140 boost | BATT_SENSE GPIO1 (ADC1_CH0) | No divider; power switch SW1. |
| "AI alarms" | resistor + trace | GPIO5 (365R, ADC), GPIO4 (1k) | Game gimmick (Dawn Accord). |
| Boot | JP1 solder jumper on GPIO0 | | **No boot button.** Download mode = USB-Serial/JTAG reset, OTA, or shorting JP1. |

## 2. What the firmware does today

Boot (`main/main.c`): LEDs up first (boot progress bar) -> NVS -> I2C -> secure element -> badge
config -> battery -> display/LVGL (pinned to core 1) -> NFC -> Dawn Accord -> minibadge -> UI ->
WiFi -> hwtest screen (first boot) or main screen.

Main screen: status bar (clock, WiFi, battery) + app menu (Game, Settings, Code Entry, Credit
Transfer). Everything behind the menu talks to `https://sc25.redactd.dev` with HMAC signatures from
the secure element. NFC writes from a "Nut" deliver 5-char codes that get redeemed via the API.

Partitions: nvs 24K, otadata, phy, **ota_0 4 MB, ota_1 4 MB, spiffs ~7.9 MB**. SPIFFS held
server-downloaded faction PNGs; the repo has no spiffs image (generation is commented out).

External deps: `lvgl ^9.3`, `espressif/led_strip`, `espp/st25dv` (+ndef), `johboh/nlohmann-json`,
`espressif/esp-boost`, vendored `esp-cryptoauthlib`.

## 3. Keep / rework / delete

Line counts are source lines in `Firmware/badge`.

### Keep as-is (hardware drivers, ~4.5K lines)

| Component | Lines | What it is |
|---|---|---|
| `shared_components/led` | 873 | **The gem.** Layered LED compositor: 10 layers, 3 priorities, exclusive/paused/timed layers, static-color helper, frame diff, 25 FPS render task, gamma table. Everything LED-related should go through `led_layer_create()`. |
| `components/display` | 972 | ST7789 i80 bus, double full-frame buffers in PSRAM, LVGL task, LEDC backlight, screen timeout/dim with wake callback. Only fix: drop the spurious `REQUIRES "badge"`. |
| `components/nfc` | 1713 | ST25DV wrapper, GPO ISR, RF-write settle/debounce, NDEF parse (text/URI), writers for text/URL/WiFi, rate limit. Self-contained (`i2c_manager` + `st25dv` only). |
| `components/minibadge` | 640 | Slot scan every 2 s, insert/remove/replace callbacks, D-pad minibadge poll task -> esp_event. Fix: `REQUIRES "badge"` is really `display` (for `screen_timeout_reset`). |
| `shared_components/i2c_manager` | 1180 | Two buses, device registry, mux support, EEPROM read helper. |
| `components/battery` + `adc_manager` | 509 | ADC calibration, alkaline curve, filtering, event group. |
| `components/nvs`, `helpers` | 105 | Trivial. |
| `ui/nav.c`, `ui/widgets/*`, `ui/theme.*`, `ui/loadanim.c`, `ui/screenshot.c` | ~2.7K | Joystick spatial navigation (this is what makes the 5-way stick usable in LVGL), modal + spinner, 5-way text input widget, styles/fonts. |

### Rework (badge plumbing, keep the shape, remove the game)

| File | Today | After |
|---|---|---|
| `components/input/input.c` | Joystick read inside the LVGL indev callback; merges minibadge D-pad; arms GPIO light-sleep wake. | Split into a `joystick` driver that produces debounced press/release/hold events for **both** LVGL and HID, plus the LVGL indev adapter. Delete `touch.c/h` (no touch controller on this board, `CONFIG_INPUT_TOUCH_ENABLED=n`). |
| `components/badge/ops/led_patterns.c` | flash / sparkle / ease (breathe) + faction + Dawn Accord wrappers. | Keep flash/sparkle/ease; add chase + rainbow + static-frame from `hwtest.c`; drop faction/violation wrappers. Becomes its own `led_patterns` component with no `badge_game` dependency. |
| `components/badge/config.*`, `config_v1.h`, `migrate.c` | NVS blob v1: hw_pass, registered, brightness, timeout, faction_leds, player_name, team, credits, level, sorting_hat. | Bump to v2: brightness, timeout, led_brightness, hid_profile, hid_transport, (player_name optional for BLE device name). `migrate.c` carries brightness/timeout forward. |
| `components/badge/badge.c` | 1274 lines: provisioning, status sync, OTA splay, Dawn Accord handler, ready tasks. | ~60 lines: load config, apply screen timeout, that's it. |
| `components/badge/ops/nfc.c` | Decodes Nut codes, redeems via API, rewrites WiFi NDEF. | Thin demo: log tag events, flash LEDs on write, keep `nfc_write_url/text/wifi` reachable from a UI app. |
| `shared_components/wifi_manager` | Conference SSID from Kconfig + creds from the secure element, saved networks optional. | NVS saved networks only (the code already exists behind `CONFIG_ALLOW_EXTERNAL_WIFI_NETWORKS`). Remove the `secure_element` include. Kconfig-gate the whole component so a BLE-only build skips WiFi init. |
| `ui/screens/hwtest.c` | 1953 lines: joystick, chip comms, crypto setup, RGB LED, AI inhibitor, AI security. | Keep joystick + NFC comms + RGB LED tests (~1.2K lines); delete crypto and AI tests. Keep the Konami-code entry in the menu, it is fun and free. |
| `ui/screens/main/apps/settings.c` | Name, brightness, timeout, faction switch, version. | Drop faction switch, add LED brightness + HID transport/profile. |
| `main/main.c` | 13 init steps. | Drop SE, Dawn Accord, API; add joystick + HID init. Boot bar stays. |

### Delete (the game, ~9K lines + vendored lib)

| What | Lines | Why |
|---|---|---|
| `components/api` (+ `certs/isrgrootx1.pem`) | ~2.8K C++ | HTTPS client to the dead game server. Takes `nlohmann-json`, `esp-boost`, `esp_https_ota`, `mbedTLS` client paths with it. |
| `components/badge_game` | ~860 | Node topology, factions, nut types. |
| `components/dawn_accord` | ~450 | AI-inhibitor resistor / security-strip monitor. |
| `shared_components/secure_element` + `esp-cryptoauthlib` | ~2.1K + 582 vendored files | Only served the API HMAC and WiFi creds. The chip stays on the board for anyone who wants to play with it later. |
| `components/badge/ota.*` | 237 | Pulls firmware from the game API. No server, no OTA. (Re-add a plain `esp_https_ota` from a URL later if wanted.) |
| `ui/screens/main/apps/{game,credits,code,map}.c` | ~1.9K | Game apps. |
| `ui/flows/{redeem_code,registration,dawn_accord_violation}.*` | ~900 | Game flows. |
| `ui/screens/update.*` | 150 | OTA progress screen. |
| `ui/images/*` except `sc25_logo` | 4.5 MB of C text | Only `sc25_bg` and `sc25_logo` are actually linked today (`left_bump`/`right_bump` are declared, never used). C-array text is ~6x the in-flash size, so `sc25_bg` costs ~230 KB of flash as the main-screen wallpaper. Replace with a flat color or a small tile. |
| `ui/fonts/*` unused | | Referenced: `clarity_14/16/24`, `white_rabbit_16/18/22`, `open_dyslexic_reg_16`, `fa_duotone_icons_16`, plus LVGL Montserrat 12-22. Delete the rest (`bm_mini_*`, `clarity_18/20/22`, `white_rabbit_20`, `fa_sharp_icons_16`). |
| `main/Kconfig.projbuild` DEVICE_TYPE choice | | Only consumed by `secure_element.h`. |
| `sdkconfig.defaults` conference WiFi creds | | Dead network. |

## 4. Where the bytes are, and how to measure

Nothing here is measured yet. The machine has ESP-IDF v5.3.2 with only the RISC-V toolchain;
this project needs IDF >= 5.5.0 and the Xtensa (esp32s3) toolchain. Step 0 installs that and
builds the stock firmware so every later phase has a baseline.

```sh
cd ~/esp && git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git esp-idf-5.5
cd esp-idf-5.5 && ./install.sh esp32s3
. ./export.sh
cd ~/repo/Saintcon2025/Firmware/badge
idf.py set-target esp32s3 && idf.py build
idf.py size-components > ../../docs/size-baseline.txt   # or wherever
```

Measured stock baseline (main @ 360c892, IDF v5.5.1, full table in `docs/size-baseline-stock.txt`):

| | bytes |
|---|---|
| `badge.bin` | 2,825,664 (of a 4 MB OTA slot) |
| Flash code | 1,666,516 |
| Flash rodata | 1,029,180 |
| Internal DIRAM used | 148,955 of 341,760 |

Biggest archives: LVGL 461 KB, libstdc++ 354 KB (the C++ API client), `ui` 351 KB (fonts + wallpaper),
`esp_app_format` 250 KB, net80211 159 KB, `api` 153 KB, mbedcrypto 138 KB, lwIP 104 KB, `nfc` 87 KB.
Internal SRAM is the tighter budget for BLE: the BT controller wants ~64 KB of internal heap.

Flash layout decision for later: with no OTA server, two 4 MB slots are wasted. Either keep them
(zero work, still roomy) or go `factory 8 MB + storage 8 MB`. Not needed for phases 0-3.

## 5. Target architecture

```
main/                     boot sequence + boot LED bar (as today, fewer steps)
components/
  joystick/               NEW. GPIO poll @ ~10 ms, debounce, press/release/hold/repeat events
                          (esp_event or a small queue). Also exposes raw state for the LVGL indev.
  input/                  LVGL keypad indev fed by joystick + minibadge D-pad (shrunk)
  hid/                    NEW. One API: hid_init(transport), hid_key(), hid_consumer(), hid_mouse(),
                          hid_gamepad(), hid_connected(). Two backends behind it:
                            hid_ble.c  -> IDF esp_hid + NimBLE   (phase 3a)
                            hid_usb.c  -> espressif/esp_tinyusb  (phase 3b)
  led_patterns/           flash / sparkle / breathe / chase / rainbow / static (generic, no game)
  display/, nfc/, minibadge/, battery/, adc_manager/, nvs/, helpers/   (unchanged)
  badge/                  config v2 + migrate + version only
  ui/                     shell (status bar, menu, nav, widgets, theme) + apps:
                            hid.c      profile picker + pairing status
                            leds.c     pattern/brightness picker (lift from hwtest RGB test)
                            nfc.c      write URL/text/WiFi to the tag, show last RF event
                            settings.c brightness/timeout/LED/HID
                            hwtest.c   trimmed self-test (Konami code still opens it)
shared_components/
  led/, i2c_manager/, wifi_manager/ (NVS-only, Kconfig-gated)
```

HID profiles are plain tables: `{UP, DOWN, LEFT, RIGHT, CENTER} -> report`. First set: Media
(vol+/-, prev/next, play), Presenter (PgUp/PgDn, B, Esc), Arrows+Enter, Mouse (cursor + click,
hold = faster), Gamepad (d-pad + button). Minibadge D-pad becomes a second input source for free
because `input.c` already merges it.

LED feedback via layers: LOW-priority idle pattern (user picked), MEDIUM pulsing blue while
advertising / enumerating, HIGH one-shot flash on key send, battery-low red breathe. All of that is
`led_layer_create()` + `led_pattern_*()`, no new primitives.

## 6. Phases

### Status (2026-10-30)

| Phase | State |
|---|---|
| 0 toolchain + baseline | Done. IDF v5.5.1 at `~/esp/esp-idf-5.5.1` (`. ~/esp/esp-idf-5.5.1/export.sh`), stock build measured, console moved to USB-Serial/JTAG. |
| 1 gut the game | Done and flashed to the bench badge. `badge.bin` 2,825,664 -> 1,131,648 bytes; internal DIRAM 149 KB -> 91 KB (`docs/size-phase1.txt`). Boot log clean, v1 config migrated, NFC default URL provisioned. |
| 2 driver cleanup | Done: `joystick` (debounced press/release/hold/repeat), `led_patterns` (idle patterns + status patterns), `leds` and `nfc` apps. |
| 3a BLE HID | Implemented and flashed: `hid` component (esp_hidd + NimBLE, combined keyboard/mouse/consumer map, Just Works bonding in NVS), four profiles, `HID Remote` app. Verified from the Mac: advertises as `SC25 Wrench` with the HID service, accepts a GATT connection, badge logs connect. `badge.bin` 1,474,048 bytes, DIRAM 139 KB (`docs/size-phase3a.txt`). Still to do by hand: pair from a phone/laptop and press the stick in each profile. |
| 3b USB HID | Tested OK 2026-10-30, off by default: `components/hid/hid_usb.c` (esp_tinyusb 1.x, HID interface with the shared report map + a CDC console), config v4 `usb_hid` flag, Settings -> USB toggle, `tools/enter_bootloader.py` for reflashing. Boot guards: centre held at power-on or a crash-reboot keep the badge on USB-Serial/JTAG. `badge.bin` ~1,549 KB (`docs/size-usb.txt`). Verified on the Mac: enumerates as "SC25 Wrench" 303A:4005 (keyboard, mouse, pointer, consumer + CDC `/dev/cu.usbmodem<MAC>`), Media profile sends volume reports over USB while the iPhone stays bonded over BLE, 1200-baud touch -> ROM download mode -> `idf.py flash` -> reboot with console handed to CDC at 2 s, Settings off reboots onto USB-Serial/JTAG, live on re-enumerates in 1 s, centre-held power-on logs "USB HID skipped this boot". macOS pops the Keyboard Setup Assistant once per badge; quit it, it only guesses the physical layout. |
| 4 | Not started. |

The stock firmware for rollback is the `main` branch; the bench badge's original NVS (config v1) is
backed up in the session scratchpad.

Gotcha found while testing: a plain GATT connection from macOS (bleak) made `bluetoothd` latch onto
the badge as a HID device and hold the link without bonding, which stops advertising (one connection
max) until the badge resets. Pair from Bluetooth Settings instead of poking it with a GATT client.
macOS hides the HID service (0x1812) from third-party GATT clients, so a bleak service listing shows
only Scan Parameters, Battery and Device Information; that is normal.

How to drive it: menu -> HID Remote -> pick a profile. The stick is then sent to the host; hold the
center button 1.5 s to get the menu back. Profiles are in `components/hid/hid_profiles.c`
(tables of joystick direction -> HID usage); the BLE plumbing is `components/hid/hid_ble.c`.

USB HID at a glance: Settings -> USB -> "Turn USB keyboard on" starts TinyUSB right away and remembers it;
the badge enumerates as a keyboard/mouse/consumer device plus a CDC serial port, logs move to that CDC port,
and reports go to USB whenever a host has the badge mounted (BLE otherwise). The S3 has one internal USB PHY,
so while TinyUSB runs the USB-Serial/JTAG port used for `idf.py flash` is gone. Reflash recipe:
`python tools/enter_bootloader.py /dev/cu.usbmodem<CDC>` (opens the CDC port at 1200 baud; the firmware hands
the PHY back and reboots into the ROM download mode), then `idf.py -p /dev/cu.usbmodem<JTAG> flash` as usual.
Gotcha that shaped the code: TinyUSB maps the PHY to the OTG controller through `RTCCNTL.usb_conf`, RTC
registers that survive a soft reset, and `usb_del_phy()` never undoes it. `hid_usb_release_phy()` therefore
runs as a shutdown handler, before the download-mode reboot, and first thing in `main.c` on every boot. Escape
hatches without software: hold the joystick centre while powering on (USB stays off for that boot), and a
crash-reboot (panic / watchdog) also skips USB so a bad build cannot hide the console. JP1 remains the last resort.
Turning USB off is a reboot (no clean TinyUSB teardown while a host holds the port).

### Phase 0: toolchain + baseline (half a day)
1. Install IDF 5.5.1 + esp32s3 as above. Build stock firmware, save `size-components`.
2. Flash a badge, confirm it boots to hwtest/main. Record free internal heap after boot
   (`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`).
3. Set `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` so logs stop sharing GPIO43/44 with the joystick.
Done when: a clean build, a flashed badge, two numbers in a text file.

### Phase 1: gut the game (one to two days)
1. `git rm` the Delete list in section 3. Remove `nlohmann-json` and `esp-boost` from
   `idf_component.yml`, remove `esp_https_ota`/`esp_http_client` from any `REQUIRES`.
2. Slim `badge.c`, bump config to v2, write the v1->v2 migration.
3. Fix `REQUIRES` in `display`, `minibadge`, `input`, `ui`, `badge` so the dependency graph is
   `ui -> {display, input, led, led_patterns, nfc, minibadge, battery, badge}` and nothing points
   back at `badge` except `ui`.
4. Menu: Settings, LEDs, NFC, HW Test. Strip faction bits from menu/settings; trim hwtest.
5. Replace `sc25_bg` with a flat color (keep `sc25_logo` on splash).
6. `wifi_manager`: delete `load_provisioned_creds`, Kconfig-gate init. Leave it off by default.
Done when: boots to the menu, boot LED bar works, NFC tag write logs an event and flashes, battery
icon updates, `size-components` shows the drop. Commit on a branch.

### Phase 2: driver cleanup (one day)
1. New `joystick` component: 10 ms poll task, debounce, per-direction press/release/hold/repeat
   events; `input.c` reads its state instead of raw GPIO. Keep the light-sleep GPIO wake.
2. `led_patterns` component: move flash/sparkle/ease out of `badge`, port chase/rainbow/static
   from hwtest. Add a `leds` app that is the old hwtest RGB overlay without the pass/fail buttons.
3. `nfc` app: three buttons (write URL / write text / write WiFi) using the existing writers.
Done when: the joystick drives menus exactly as before, hold-repeat works, the LEDs app runs every
pattern, NFC app writes a URL a phone can open.

### Phase 3a: BLE HID (one to two days)
1. sdkconfig: `CONFIG_BT_ENABLED`, `CONFIG_BT_NIMBLE_ENABLED`, `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`.
   Start from IDF `examples/bluetooth/esp_hid_device` (it supports NimBLE).
2. `hid/hid_ble.c`: report map = keyboard + consumer control + mouse (+ gamepad later). Device name
   from config (`SC25 Wrench` default). Bonding on, advertise on boot when transport = BLE.
3. `hid` app: profile list, connection state, forget-bonds button. Center-hold 2 s from any
   screen = send the profile's "home" action? Decide in the app, not here.
4. LED layers for advertising/connected/key-sent.
Done when: a phone or laptop pairs, each profile does what its table says, battery lasts a day of
idle (verify light-sleep still arms with BLE up).

### Phase 3b: USB HID (one day, after 3a)
1. Add `espressif/esp_tinyusb`; composite HID + CDC. Logs go to CDC.
2. **Before enabling it on a badge**, implement the CDC 1200-baud bootloader trick (on line coding
   change to 1200 bps: `REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT); esp_restart();`).
   Once TinyUSB owns the OTG PHY the USB-Serial/JTAG path is gone, and JP1 is a solder jumper.
3. Runtime transport select in Settings (BLE / USB / auto = USB when VBUS present).
Done when: `idf.py flash` still works through the 1200-baud trick, the badge shows up as a keyboard
on a laptop, same profiles behave identically on both transports.

### Phase 4: optional
- Repartition to `factory 8 MB + littlefs 8 MB` if OTA is truly dead.
- ESP-NOW badge-to-badge LED sync (needs WiFi init, no AP).
- NFC-driven macros: phone writes a text record, badge types it over HID.
- Light sleep between inputs when on battery (the GPIO wake is already armed in `input.c`).

## 7. Risks and gotchas

- **No boot button.** Never flash a build that disables USB-Serial/JTAG unless the CDC bootloader
  trick is in and tested. Keep phases 0-3a on USB-Serial/JTAG console.
- **GPIO43/44** are UART0 TX/RX and the joystick DOWN/CENTER. Set the console to USB-Serial/JTAG
  explicitly in `sdkconfig.defaults` (phase 0).
- **Internal RAM** is the constraint for BT, not flash. LVGL buffers already live in PSRAM
  (`CONFIG_LCD_I80_COLOR_IN_PSRAM=y`); keep `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` if WiFi stays.
  Measure internal heap after `hid_init()`.
- **LVGL is pinned to core 1** and the display takes a lock; HID callbacks must not touch LVGL
  directly. Post UI events through `enqueue_ui_event()` like everything else does.
- **Config blob v1 on fielded badges**: the v2 migration must carry brightness/timeout and must not
  reset `hw_pass`-gated behavior into the hwtest screen every boot (just drop the gate).
- **NFC and BLE together**: the ST25DV sits on I2C, unaffected. The 13.56 MHz antenna loop and the
  2.4 GHz module are far apart; nothing to do.
- The `led` render task runs at priority 4 with a 3 KB stack and calls user render callbacks;
  pattern callbacks must stay allocation-free (they are today).

## 8. Decisions (settled 2026-10-30)

1. **BLE first, USB second.** Wireless matches the battery badge and the AfterCon Nut precedent,
   and it leaves the USB-Serial/JTAG flashing path intact until the CDC bootloader trick exists.
2. **WiFi stays**, Kconfig-gated, default off.
3. **Keep the two 4 MB OTA slots** for now; repartition is phase 4.
4. **Keep the trimmed HW Test screen** (joystick, NFC comms, RGB LEDs).
