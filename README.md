# Plebwatch

Battery-friendly Bitcoin dashboard for **M5StickC Plus2**, inspired by [Clark Moody](https://bitcoin.clarkmoody.com/dashboard/).

## Features
- Hardcoded dual Wi‑Fi (home + hackerspace) via gitignored `include/config.h`
- Pages: Markets, Fee estimates, Mining, Halving, Lightning, Top nodes
- Deep sleep ~20 minutes on battery (faster when charging)
- Button A: next page · Button B: LN top ↔ node versions · Power click: sleep

## Setup
```bash
cp include/config.h.example include/config.h
# edit SSIDs/passwords in include/config.h

export PATH="$HOME/.platformio/penv/bin:$PATH"
cd ~/Projects/plebwatch
pio run -t upload
pio device monitor
```

`include/config.h` is gitignored (contains Wi‑Fi passwords).

## Hardware
M5StickC Plus2 (ESP32-PICO-V3-02, 1.14" TFT, 200mAh battery)
