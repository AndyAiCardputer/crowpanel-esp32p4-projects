# Crown NES -- NES Emulator for CrowPanel ESP32-P4

NES emulator for CrowPanel Advanced 10.1" ESP32-P4 display, based on the nofrendo engine.

## Features

- NES emulation via nofrendo engine
- ROM browser from SD card (`.nes` files in `/sdcard/roms`)
- USB Host gamepad support (PS5 DualSense via Steam Deck Dock)
- 1024x600 display, NES scaled to 800x600 (4:3 aspect ratio)
- Phase 1: Display + SD + Gamepad (no audio yet)

## Hardware Setup

- **CrowPanel Advanced 10.1" ESP32-P4**
- **USB Gamepad:** PS5 DualSense (connected via Steam Deck Dock or USB hub)
- **SD Card:** FAT32, place `.nes` ROM files in `/roms` folder

## USB Gamepad

Supports USB gamepads via USB Host HID driver. Tested with PS5 DualSense through Steam Deck Dock (USB hub with `CONFIG_USB_HOST_HUBS_SUPPORTED`).

## Building

```bash
. $IDF_PATH/export.sh
cd crown_nes
idf.py build
idf.py flash monitor
```

## SD Card Layout

```
/sdcard/
  roms/
    game1.nes
    game2.nes
    ...
```

## UART Console

Serial output goes to UART1 on J2 connector (not USB):
- TX: GPIO47, RX: GPIO48
- Baud: 115200

## Target

- Chip: ESP32-P4
- Framework: ESP-IDF v6.1
- Display: 1024x600 RGB LCD

## Credits

- NES emulation: [nofrendo](https://github.com/kriztioan/nofrendo) engine
