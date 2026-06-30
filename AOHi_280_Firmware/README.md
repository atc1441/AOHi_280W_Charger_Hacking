# AOHi 280W Charger - Custom Display Firmware

A **standalone, clean-room custom firmware** for the display/UI MCU of the **AOHi 280 W
USB‑C charger**. It is a from-scratch reimplementation of the stock display firmware
(reverse-engineered from the stock image, no vendor code linked in) and runs in place
of the stock app.

- **MCU:** relabeled **"BWX468" = Huada/HDSC HC32F460xE** - ARM Cortex‑M4F, 512 KiB flash,
  ~188 KiB SRAM, runs at 138.67 MHz (MPLL).
- **App load address:** `0x8000`, behind the device's existing bootloader at `0x0`.
- **Ports:** C1 (240 W master, separate PD controller), C2/C3/C4 (SW3566), A1/A2 (USB‑A).

> Building only replaces the **internal MCU app**. The UI images live on the external
> 32 MB SPI‑NOR chip, which is already populated on a real device - leave it untouched and
> the display renders identically to stock.

---

## Features

- **Display & UI** - LCD over SPI3 (200×480, mounted landscape); full image/text/number
  rendering from the external‑flash asset tables; the stock page model + button state machine
  (home, per‑port power, temperature, clock faces, settings, Wi‑Fi pairing).
- **USB‑PD power** - per‑port V/I/W telemetry over bit‑bang I²C, total power, NTC temperatures,
  the home power ring; C1 negotiates and holds **20 V / 240 W**, demand‑based Smart distribution.
- **Wi‑Fi companion** - Tuya‑style UART protocol to the BK7231N (handshake, weather/time sync, DP
  status reporting) plus **OTA** of both the **firmware** (staged to internal `0x38000`) and the
  **external UI image** (staged to external `0x800000`), CRC32‑checked (`app/ota.c`).
- **Extras** - screensaver clock/animations, an on‑device **debug page** (live per‑port I²C
  telemetry + link state), a scrolling **Watt history graph**, and an internal video page.

---

## Build

Needs the GNU Arm Embedded toolchain (`arm-none-eabi-gcc`) on `PATH`.

```sh
make            # -> build/firmware.elf + build/firmware.bin (+ .map)
make clean
```

The build is self-contained: `mcu/` provides the register map, Cortex‑M4 core, the few
peripheral drivers used (PORT/SPI/USART/EFM/RTC/PWC/SWDT/INTC), system init, and the linker
script. No vendor DDL.

## Flash

The HC32F460xE is natively supported by J‑Link (SWD on **PA13=SWDIO / PA14=SWCLK**). With a
J‑Link connected:

```powershell
.\flash.ps1                          # build\firmware.bin -> app @ 0x8000
.\flash.ps1 -Action reset            # reset + run
.\flash.ps1 -File fw.bin -Addr 0x0   # flash a specific image/address
```

| Region        | Address      | What                                              |
|---------------|--------------|---------------------------------------------------|
| Internal boot | `0x00000000` | bootloader (keep stock, or build `boot/`)         |
| Internal app  | `0x00008000` | `build/firmware.bin` (this project)               |
| External NOR  | (own chip)   | UI image assets - **leave as‑is** on a real device |

> A corrupted/odd state on the I²C power ICs (e.g. the 0x78 config chip) only resets on a
> **full AC power‑cycle** - a J‑Link reset does not power‑cycle them.

---

## Pinout (HC32F460xE, LQFP64)

Built from a live SWD dump of every pin's PCR/PFSR registers cross‑checked with the firmware.
Ports D/E/G are unused/unbonded. `func N` = the PFSR function-select value read from silicon.

### PortA
| Pin  | Use |
|------|-----|
| PA0 / PA1 | **ADC1** inputs (sense / NTC) |
| PA5 | **USART1 RX** ← Wi‑Fi module |
| PA6 | **USART1 TX** → Wi‑Fi module |
| PA7 | **LCD backlight PWM** (Timer6) |
| PA8 | **LCD reset** |
| PA9 | **Ext‑flash (SPI1) CS** |
| PA10 / PA11 / PA12 | **Ext‑flash SPI1** MISO / MOSI / SCK |
| PA13 / PA14 | **SWD** SWDIO / SWCLK (debug + flash) |

### PortB
| Pin  | Use |
|------|-----|
| PB0 | LCD panel enable (low = on) |
| PB1 | **LCD CS** |
| PB2 | Power‑stage range: **20 V band** |
| PB3 / PB4 | **PDC bit‑bang I²C** SCL / SDA (SW3566 + 0x78 config IC) |
| PB10 | Power‑stage range: low‑V (<9.5 V) path |
| PB12 | Power‑stage range: **HV boost enable** (>10 V) |
| PB13 / PB14 | **LCD SPI3** MOSI / SCK |
| PB15 | **LCD DC** (cmd/data) |

### PortC / PortF / PortH
| Pin  | Use |
|------|-----|
| PC13 | **C1 (0x16) master‑charger I²C SCL** (bit‑bang, open‑drain) |
| PC14 | control line - driven low (purpose TBD) |
| PF2  | **C1 (0x16) master‑charger I²C SDA** (bit‑bang, open‑drain) |
| PH2  | **Telemetry / level‑shifter enable** (driven high) |

### Charging ICs → pins
| Port(s) | Controller | Bus | Address |
|---------|-----------|-----|---------|
| **C1** (240 W) | **BK8Y1** master PD source IC | bit‑bang I²C **SCL=PC13, SDA=PF2** | 7‑bit **0x16** |
| **C2 / C3 / C4** (140 W each) | **SW3566H** PD buck (one each) | PDC bit‑bang I²C **SCL=PB3, SDA=PB4** | **0x3D / 0x3E / 0x3F** |
| (config) | **0x78** PD/power config IC | same PDC bus (PB3/PB4) | 7‑bit **0x3C** (8‑bit 0x78/0x79) |
| **A1 / A2** (USB‑A, 22.5 W each) | **SW3537U** | read via the 0x78 config IC's ADC current sense | - |
| (shared HV boost) | power stage feeding the bucks | GPIO: **PB12** (boost), **PB10** (low‑V), **PB2** (20 V band) | - |

**Key registers**
- **C1 / 0x16:** reg1 = master enable, reg2 = status (bit0 attached / bit2 valid), **reg3 = power
  budget in mW** (write `240000` so it advertises the 20 V / 240 W PDO), reg0x21 = packed V(mV)/I(mA).
- **SW3566:** reg0x88 = telemetry packet (V/I/temp, CRC8), reg0xA0 = per‑port current cap.
- **0x78:** reg0x40 = ADC channel select, reg0x41/0x42 = ADC result lo/hi (ch1/ch2 = USB‑A current,
  ch5 = shared‑rail voltage). Its reads **require a repeated‑START** before the read address.

---

## Layout

```
AOHi_280_Firmware/
├── Makefile          # builds build/firmware.bin (app @ 0x8000)
├── flash.ps1         # J-Link flash helper
├── app/              # application: main loop, UI/menu engine, rendering,
│                     #   Wi-Fi link, power/PD logic, debug + graph pages, OTA
├── hal/              # clean-room drivers (display/spi/i2c/uart/rtc/power/...)
├── lib/              # tick, ringbuf, minimal libc, config, fault handler
├── board/ + device/  # board config + startup (vector table @ 0x8000)
├── mcu/              # self-contained HC32F460 support: register map, M4 core,
│                     #   PORT/SPI/USART/EFM/RTC/PWC/SWDT/INTC drivers, linker script
└── boot/             # optional bootloader reconstruction (separate build)
```
