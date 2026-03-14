# USB Gamepad Test

USB Host gamepad test for CrowPanel Advanced 10.1" ESP32-P4 with on-screen log.

Tested with **PS5 DualSense** controller via Steam Deck Dock.

## What It Tests

- USB Host mode on ESP32-P4 native USB 2.0 port
- HID Host driver for gamepads
- PS5 DualSense input parsing (sticks, buttons, triggers, D-pad)
- Generic gamepad fallback parser
- USB Hub support (multi-level, required for docking stations)
- LVGL on-screen logging and real-time gamepad state display

## On-Screen Display

The test shows:
- **Title bar** -- test name
- **Status** -- connected device (VID:PID)
- **Sticks** -- left/right stick X/Y values, L2/R2 triggers, D-pad direction
- **Buttons** -- currently pressed buttons (X, O, Square, Triangle, L1, R1, etc.)
- **System Log** -- scrolling 12-line log with USB events (connect, disconnect, HID reports)

## Hardware

- CrowPanel Advanced 10.1" ESP32-P4
- USB gamepad (PS5 DualSense tested)
- **Powered USB hub required** -- CrowPanel does NOT supply 5V on USB Host port

### Tested Setup

```
CrowPanel USB 2.0 (Host mode)
  -> Steam Deck Dock (powered hub)
       -> PS5 DualSense (054C:0CE6)
```

Any powered USB hub should work. The key requirement is external 5V power for the gamepad.

## Building

```bash
. $IDF_PATH/export.sh
cd test_usb_gamepad
idf.py build
idf.py flash
```

### Flashing

Connect USB cable to the **USB 2.0 Device** port (not UART0/CH341).

To enter download mode: **Hold BOOT -> Press RESET -> Release BOOT**.

After flashing, press RESET to run. Then reconnect the USB cable (or use a second cable) to the same port for USB Host mode.

### Serial Output

Serial logs go to UART1 on J2 connector (GPIO47=TX, GPIO48=RX, 115200 baud).
Use a USB-UART adapter or [UART Bridge for Cardputer](https://github.com/AndyAiCardputer/cardputer-adv-tests/tree/main/tests/uart_bridge_crowpanel).

## Key Configuration (sdkconfig.defaults)

```ini
CONFIG_SPIRAM=y                        # PSRAM for display
CONFIG_SPIRAM_SPEED_200M=y

# USB Host
CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256
CONFIG_USB_HOST_HW_BUFFER_BIAS_BALANCED=y
CONFIG_USB_HOST_DEBOUNCE_DELAY_MS=250

# USB Hub support (required for docking stations)
CONFIG_USB_HOST_HUBS_SUPPORTED=y
CONFIG_USB_HOST_HUB_MULTI_LEVEL=y

# UART1 console (J2 connector)
CONFIG_ESP_CONSOLE_UART_CUSTOM=y
CONFIG_ESP_CONSOLE_UART_NUM=1
CONFIG_ESP_CONSOLE_UART_TX_GPIO=47
CONFIG_ESP_CONSOLE_UART_RX_GPIO=48
```

## Supported Gamepads

| Gamepad | VID:PID | Status |
|---------|---------|--------|
| PS5 DualSense | 054C:0CE6 | Full support (sticks, buttons, triggers, D-pad) |
| Generic HID gamepad | any | Basic support (sticks + buttons) |

## Troubleshooting

- **Gamepad not detected:** Make sure the hub is powered. CrowPanel USB port does not supply 5V.
- **"No clients" in log:** USB Host initialized but no HID device found. Check cable and hub power.
- **Black screen:** See [Knowledge Base](../docs/CROWPANEL_KNOWLEDGE_BASE.md#the-black-screen-problem-most-common-issue).
