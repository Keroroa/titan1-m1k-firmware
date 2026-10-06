# Titan-1 Mouse Firmware

**English** | [简体中文](README.zh-CN.md)

An open-source, ultra-low-latency firmware for the Titan-1 (Bst) gaming mouse
(ATmega32U4 + PixArt PMW3360), built on the architecture of the
[Zaunkönig M1K](https://github.com/zaunkoenig-firmware/m1k-firmware) — plus a
zero-dependency WebHID web configurator and the full reverse-engineering
report of the stock firmware.

The journey: stock firmware → binary patches → QMK port → **this firmware**.
The forensic analysis that made it possible lives in [`docs/`](docs/).

## Why it is fast

| Stage | Typical firmware (incl. QMK) | This firmware |
|---|---|---|
| Sensor polling | Free-running loop, unsynced to USB frames | **Fixed 125 µs tick**, hard-synced to USB SOF |
| Sample → wire timing | Random phase, ~+0.5 ms average | Aligned to the host's polling window, deterministic |
| Button path | Matrix scan + 5 ms debounce + state machine | **EIFR edge latch, zero debounce latency on press (≤125 µs)** |
| Report channel | Send queue | Kill-and-merge on busy bank, never blocks, never drops |
| Sensor sleep | Default power management | Rest mode fully disabled, no wake penalty |
| Size | QMK build ~17 KB | **7.9 KB** (3.8 KB code + 4 KB SROM) |

Full-speed USB caps the report rate at 1 kHz — 8 kHz would require an STM32
with high-speed USB (see Zaunkönig M2K/M3K).

## Features

- **3 DPI stages** (100–12000, step 100) + dedicated cycle key with LED feedback
- **Web configurator** ([`web/titan1.html`](web/titan1.html), Chrome/Edge, single file):
  click-to-switch stage cards, LOD / angle snapping / wheel direction,
  **live report-rate meter + X/Y motion oscilloscope**, **measured-DPI
  calibration**, config import/export, one-click DFU
- **Telemetry stream**: the firmware emits one packet per USB frame over the
  vendor interface (browsers are not allowed to read the mouse interface —
  this is the workaround, same approach as the EGG 8k configurator)
- EEPROM-persisted config; enter DFU by holding the middle button while
  plugging in; the bootloader is never touched, rollback is always possible

## Hardware (reverse-engineered, see docs/)

| Function | ATmega32U4 pins |
|---|---|
| PMW3360 NCS / SPI | B0 / B1 (SCK), B2 (MOSI), B3 (MISO) |
| Buttons 1–5 | D0–D4 (active-low + pull-up; EIFR of INT0-3 used as edge latch) |
| DPI cycle key | D5 (the stock firmware's unused "ghost key") |
| Wheel A/B | C6 / C7 |
| LEDs | B5, B6 (always on), D7 (stage feedback) |
| Enter DFU | Hold PD2 (middle button) while plugging USB |

## Building

Any AVR toolchain (avr-gcc ≥ 8, avr-libc, avr-binutils); plain `make`:

```bash
cd firmware
make        # produces bstmouse2.hex (~7.9 KB of 28.7 KB available)
```

A WSL pipeline example (source on Windows, build in Linux, hex copied back)
is provided as `firmware/build.sh.example`.

## Flashing

1. Enter DFU: hold the middle button while plugging in, or click
   "Enter flash mode" in the web configurator, or run `tools/send_dfu.ps1`
   (Windows, sends the raw-HID command — no buttons needed).
2. Install the [Atmel DFU driver](https://sourceforge.net/projects/dfu-programmer/)
   once (`pnputil /add-driver atmel_usb_dfu.inf /install` with the **whole
   directory**, not just the .inf).
3. Flash:

```bash
dfu-programmer atmega32u4 erase --force
dfu-programmer atmega32u4 flash bstmouse2.hex
dfu-programmer atmega32u4 start
```

The bootloader lives at 0x7E00 and is never erased — a bad flash is always
recoverable by re-flashing.

## raw HID configuration protocol (32-byte packets, usage FF60/61)

`data[0]` = command, `data[1]` = status (0 = OK):

| Command | Value | Purpose |
|---|---|---|
| GET_INFO | 0x10 | 'B''M' magic + firmware/protocol version + active stage |
| GET_CFG / SET_CFG | 0x11 / 0x12 | 3× DPI u16LE + stage + LOD + angle snap + wheel invert |
| GET_DEBUG | 0x13 | Product_ID / SROM_ID / Config1 / Motion / Lift / Snap |
| BOOTLOADER | 0x14 | Jump to DFU |
| TELEMETRY | 0x15 | data[2] = 1/0 enables the per-frame telemetry stream |

Telemetry packet (tag 0x20, one per USB frame): frame counter u16, button
bitmap, X/Y deltas i16×2, wheel i8, motion flag.

## Repository layout

```
firmware/   M1K-architecture firmware (avr-gcc, ~7.9 KB)
web/        web configurator (single file, no dependencies, WebHID)
docs/       stock-firmware forensic reports (pins / SPI protocol / USB reports)
tools/      RE tooling (avrdump.py disassembler, make_mod.py patcher) + DFU trigger
```

## Credits & license

- USB stack lineage: PJRC Teensy (MIT) → Furiosus/qsxcv →
  [Zaunkönig M1K](https://github.com/zaunkoenig-firmware/m1k-firmware) (MIT); this project stays MIT
- SROM blob from the M1K repository (PixArt PMW3360 sensor firmware data,
  shipped per QMK/M1K community convention for interoperability)
- RE methodology cross-checked against the [QMK](https://qmk.fm) pmw33xx driver
- This repository deliberately **excludes** the stock firmware image and its
  derivative disassemblies (copyright); the analysis in `docs/` is independent
  research, and the scripts in `tools/` expect you to supply your own dump

## Disclaimer

Use only on hardware you own. Flashing carries risk; make sure you understand
the DFU process before proceeding. No liability for any damage arising from
the use of this repository's contents.
