# OSCAR-100 WB Monitor

Real-time wideband spectrum analyzer and community chat viewer for the **QO-100 (OSCAR-100) geostationary satellite**, running on a Waveshare ESP32-S3 7-inch touchscreen.

Connects simultaneously to the BATC wideband FFT stream and the BATC chat server. Both streams run in the background — switching between pages never drops the connections.

> ### Flash firmware directly from your browser
> **[https://hb9iiu.github.io/ESP32_S3_WaveShare_OSCAR_WB_MONITOR/](https://hb9iiu.github.io/ESP32_S3_WaveShare_OSCAR_WB_MONITOR/)**
> No IDE required — Chrome or Edge only.

---

## Features

**Spectrum page**
- Live FFT waterfall updated at ~2 Hz from `wss://eshail.batc.org.uk/wb/fft`
- Frequency axis covering 10490.5 – 10499.5 MHz (QO-100 wideband transponder)
- Automatic DATV signal detection: frequency, symbol rate, and dB offset from the A71A beacon
- QO-100 bandplan overlay with channel allocation zones
- Scrolling waterfall with adjustable brightness and hide/show toggle
- 6 selectable color schemes for the spectrum trace

**Chat page**
- Live BATC QO-100 chat via Socket.IO WebSocket (EIO4)
- Timestamped message history with automatic word-wrap
- Active users list
- Touch-to-scroll with position indicator

**Weather page**
- Current conditions, 7-hour and 5-day forecasts from Open-Meteo
- Dynamic backgrounds (22 day/night variants), weather icons, moon phase
- Location auto-detected from IP on first boot and cached in flash

**General**
- WiFi setup UI on first boot (on-screen keyboard, network scan)
- All credentials and location data stored persistently in NVS flash
- Factory reset by holding the screen for 3 seconds at boot
- All libraries committed to `lib/` — fresh clone builds with no downloads

---

## Hardware

| Parameter    | Value                                            |
|--------------|--------------------------------------------------|
| Board        | Waveshare ESP32-S3-Touch-LCD-7 Rev 1.2           |
| MCU          | ESP32-S3 dual Xtensa LX7 @ 240 MHz              |
| Flash        | 16 MB QIO OPI                                    |
| PSRAM        | 8 MB OPI (required — sprites use ~560 KB)        |
| Display      | 800×480 RGB parallel LCD                         |
| Touch        | GT911 capacitive, I2C (SDA=GPIO8, SCL=GPIO9)    |
| IO Expander  | CH422G (backlight, LCD power, touch reset)       |
| Connectivity | WiFi 802.11 b/g/n                                |
| USB          | USB-CDC (`ARDUINO_USB_CDC_ON_BOOT=1`)            |

---

## Flashing the firmware

### Option 1 — Browser (no IDE required)

Open the flash page in Chrome or Edge, connect the board via USB, and click Install:

**https://hb9iiu.github.io/ESP32_S3_WaveShare_OSCAR_WB_MONITOR/**

This flashes bootloader, partition table, firmware, and LittleFS in one step. Firefox is not supported (requires Web Serial API).

### Option 2 — PlatformIO

```bash
# Firmware
pio run -e DISPLAY -t upload

# LittleFS filesystem (weather backgrounds, splash screen)
pio run -e DISPLAY -t uploadfs
```

---

## First boot

1. The device starts a WiFi setup screen. Tap a network name and enter the password with the on-screen keyboard.
2. On first successful connection, the device fetches coordinates from ip-api.com and saves them to flash. This only happens once.
3. The main spectrum page loads automatically. Subsequent boots go straight to the spectrum page.

To reset all settings (WiFi + location): touch and hold the screen for 3 seconds during the boot splash.

---

## UI overview

### Spectrum page (default)

```
┌─────────────────────────────────────────────┐
│  Frequency axis  10.491 … 10.499 GHz        │  25 px
├──────────┬──────────────────┬───────────────┤
│ < CHAT   │   WATER FALL     │   WEATHER >   │  36 px  nav bar
├─────────────────────────────────────────────┤
│                                             │
│   Live FFT spectrum (dB scale, grid)        │  274 px
│   Signal labels: freq / SR / dB vs beacon   │
│                                             │
├─────────────────────────────────────────────┤
│   Scrolling waterfall (blue→red by power)   │  100 px
├─────────────────────────────────────────────┤
│   QO-100 bandplan  │  WF–  │  WF+           │  44 px
└─────────────────────────────────────────────┘
```

- Tap **< CHAT** to open the chat page.
- Tap **WATER FALL** to toggle the waterfall panel (spectrum expands to fill the space).
- Tap **WEATHER >** to open the weather page.
- Tap the beacon area to cycle through 6 spectrum color schemes.
- Tap **WF–** / **WF+** to adjust waterfall contrast.

### Chat page

```
┌───────────────────────────────┬─────────────┐
│  14:32  HB9IIU  Hello world!  │  USERS      │
│  14:33  DL3YYZ  73 all        │  HB9IIU     │
│         (continued message…)  │  DL3YYZ     │
│                               │  ──────     │
│                               │ < SPECTRUM  │
└───────────────────────────────┴─────────────┘
```

Swipe up/down to scroll message history. The thin bar on the right edge shows scroll position.

### Weather page

Full-screen weather display. Tap anywhere to return to the spectrum page.

---

## Customisation

| Setting             | How to change                                    |
|---------------------|--------------------------------------------------|
| Spectrum color      | Tap the beacon / A71A label on the spectrum page |
| Waterfall brightness| Tap WF– or WF+ in the bottom bar                |
| Waterfall visible   | Tap the WATER FALL button in the nav bar         |
| WiFi / location     | Hold screen 3 s at boot to factory reset         |

---

## Data sources

| Source            | URL                                          | Use              |
|-------------------|----------------------------------------------|------------------|
| BATC FFT stream   | `wss://eshail.batc.org.uk:443/wb/fft`        | Spectrum data    |
| BATC chat         | `wss://eshail.batc.org.uk:443/wb/chat/socket.io/` | Chat stream |
| Open-Meteo        | `https://api.open-meteo.com/v1/forecast`     | Weather          |
| ip-api.com        | `http://ip-api.com/json/`                    | First-boot geolocation |

---

## Building from source

Requires [PlatformIO](https://platformio.org/) (CLI or VS Code extension).

```bash
git clone https://github.com/HB9IIU/ESP32_S3_WaveShare_OSCAR_WB_MONITOR.git
cd ESP32_S3_WaveShare_OSCAR_WB_MONITOR
pio run -e DISPLAY           # compile
pio run -e DISPLAY -t upload # flash firmware
pio run -e DISPLAY -t uploadfs # flash LittleFS
```

All libraries are in `lib/` and are committed to the repository. No `pio pkg install` step is needed.

---

## Project structure

```
├── platformio.ini          — Build configuration (env: DISPLAY)
├── partitions.csv          — 16 MB custom partition table
├── gen_font.py             — TTF → LVGL .c font converter
├── src/
│   ├── main.cpp            — Application entry point
│   ├── lv_conf.h           — LVGL 8 config (RGB565, PSRAM, 800×480)
│   ├── lv_driver.h         — LVGL flush + GT911 touch callbacks
│   └── fonts/              — Pre-generated LVGL font .c files
├── include/
│   └── HB9IIUdisplayInit.h — LGFX class: RGB bus pinout, CH422G init
├── data/                   — LittleFS assets
│   ├── splash.jpg
│   ├── weatherBackgrounds/ — 22 day/night JPG backgrounds
│   └── weatherIcons/
├── lib/                    — Vendored libraries (LovyanGFX, LVGL, …)
├── web/                    — GitHub Pages flash page source
│   ├── index.html
│   └── manifest.json
└── .github/workflows/
    └── build-flash-page.yml — CI: build + deploy on push to master
```

---

## Partition table

| Partition  | Offset     | Size     | Purpose               |
|------------|------------|----------|-----------------------|
| nvs        | 0x009000   | 20 KB    | WiFi credentials, location |
| otadata    | 0x00E000   | 8 KB     | OTA slot selector     |
| app0       | 0x010000   | 4 MB     | Firmware (active)     |
| app1       | 0x410000   | 4 MB     | OTA update slot       |
| littlefs   | 0x810000   | ~7.9 MB  | Weather assets, splash |
| coredump   | 0xFF0000   | 64 KB    | Crash dumps           |

---

## Library versions

| Library       | Version  |
|---------------|----------|
| LovyanGFX     | 1.2.0    |
| lvgl          | 8.3.x    |
| espressif32   | 6.5.0    |
