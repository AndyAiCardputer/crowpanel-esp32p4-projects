# Display Color Test

Simple display test for CrowPanel Advanced 10.1" ESP32-P4.

Cycles through 6 background colors (Red, Yellow, Green, Cyan, Blue, Magenta) every 2 seconds with "Hello Andy+Ai" text centered on screen.

## What It Tests

- MIPI DSI display initialization (EK79007 controller)
- PSRAM (framebuffer needs ~2.4 MB, must be in PSRAM)
- LVGL rendering on 1024x600 screen
- LDO3 (2.5V) and LDO4 (3.3V) power rails for display
- Backlight PWM control

## Hardware

- CrowPanel Advanced 10.1" ESP32-P4

No additional hardware needed -- just the board and a USB cable.

## Building

```bash
. $IDF_PATH/export.sh
cd test_display_colors
idf.py build
idf.py flash
```

### Flashing

Connect USB cable to the **USB 2.0 Device** port (not UART0/CH341).

To enter download mode: **Hold BOOT -> Press RESET -> Release BOOT**.

### Serial Output

Serial logs go to UART1 on J2 connector (GPIO47=TX, GPIO48=RX, 115200 baud).
Use a USB-UART adapter or [UART Bridge for Cardputer](https://github.com/AndyAiCardputer/cardputer-adv-tests/tree/main/tests/uart_bridge_crowpanel).

## Key Configuration (sdkconfig.defaults)

```ini
CONFIG_SPIRAM=y              # PSRAM is mandatory for display
CONFIG_SPIRAM_SPEED_200M=y   # 200 MHz for smooth rendering
CONFIG_LV_FONT_MONTSERRAT_42=y  # Large font for the text
```

## Expected Result

The screen cycles through colors with contrasting text:

| Color | Text Color |
|-------|------------|
| Red | White |
| Yellow | Black |
| Green | White |
| Cyan | Black |
| Blue | White |
| Magenta | White |

If the screen stays black, see [Troubleshooting](../docs/CROWPANEL_KNOWLEDGE_BASE.md#the-black-screen-problem-most-common-issue) in the Knowledge Base.
