# CrowPanel ESP32-P4 Projects

Projects and tests for **CrowPanel Advanced 10.1 inch ESP32-P4 HMI AI Display** (1024x600 IPS Touch Screen).

## Hardware

- **Board:** CrowPanel Advanced 10.1" ESP32-P4
- **Chip:** ESP32-P4 (dual-core RISC-V, 400 MHz)
- **Display:** 1024x600 IPS with capacitive touch
- **PSRAM:** 32 MB
- **Flash:** 16 MB
- **Interfaces:** USB-A Host, SD card, UART (J2: GPIO47/48), I2C, SPI

## Projects

| Project | Description | Status |
|---------|-------------|--------|
| [crown_nes](crown_nes/) | NES Emulator with USB gamepad and ROM browser | Working |

## Building

All projects use **ESP-IDF v6.1** (v5.x may also work).

```bash
. $IDF_PATH/export.sh
cd <project_folder>
idf.py build
idf.py flash monitor
```

### UART Console

CrowPanel uses UART1 on J2 connector for serial output (not USB):
- TX: GPIO47, RX: GPIO48, Baud: 115200
- Use a USB-UART adapter or [UART Bridge for Cardputer](https://github.com/AndyAiCardputer/cardputer-adv-tests/tree/main/tests/uart_bridge_crowpanel)

## Author

**AndyAiCardputer** -- [GitHub](https://github.com/AndyAiCardputer) | [YouTube](https://www.youtube.com/@AndyAiCardputer)
