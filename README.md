# Plebwatch

Battery-friendly Bitcoin dashboard for **M5StickC Plus2**, inspired by [Clark Moody Dashboard](https://bitcoin.clarkmoody.com/dashboard/). ✌️

Public metrics come from [mempool.space](https://mempool.space/) (no API key). The Stick sleeps most of the time so it can run untethered for a few days on the built-in battery.

## What you need

### Hardware
| Item | Notes |
|---|---|
| [M5StickC Plus2](https://docs.m5stack.com/en/core/M5StickC%20PLUS2) | Required. Other Stick models need pin/power changes. |
| USB‑C cable | Data-capable cable (charge-only cables will not flash) |
| Wi‑Fi (2.4 GHz) | ESP32 does **not** join 5 GHz-only networks |

### Software (host computer)
| Item | Notes |
|---|---|
| [PlatformIO Core](https://platformio.org/install/cli) | Or PlatformIO IDE / VS Code extension |
| Git | To clone this repo |
| Linux serial access | Arch Linux: add your user to `uucp`, then re-login |

Optional: [M5Burner](https://docs.m5stack.com/en/download) if you prefer GUI flashing later — this walkthrough uses PlatformIO.

## Quick start

```bash
git clone https://github.com/thrillerxx/plebwatch.git
cd plebwatch

cp include/config.h.example include/config.h
# edit include/config.h — put your real SSIDs and passwords

# Install PlatformIO if needed:
#   python3 -c "$(curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py)"
#   export PATH="$HOME/.platformio/penv/bin:$PATH"

pio run -t upload
pio device monitor
```

After upload, unplug USB if you want — it should stay on battery, join Wi‑Fi, show the **landscape PlebWatch face**, then deep-sleep.

## Pages (Button A cycles)
1. **Watch** — full-screen landscape PlebWatch face (flag + time + slogan)
2. **Stack Mode** — sats tracker (`PLEBWATCH_SATS_BALANCE` in `config.h`)
3. Markets → Fees → Mining → Halving → Lightning → Top nodes

| Control | Action |
|---|---|
| **A** short | Next page |
| **A** long | Jump back to Watch face |
| **B** | Brightness cycle (or LN/versions toggle on Top Nodes) |

## Full walkthrough

### 1. Clone the repo
```bash
git clone https://github.com/thrillerxx/plebwatch.git
cd plebwatch
```

### 2. Configure Wi‑Fi
```bash
cp include/config.h.example include/config.h
```

Edit `include/config.h`:

```cpp
static const WifiCred WIFI_NETWORKS[] = {
    {"MyHomeSSID", "my-home-password"},
    {"OfficeSSID", "office-password"},
};
```

- Networks are tried **in order** on every wake.
- You can keep one network or add more entries.
- Never commit `include/config.h` — it is gitignored.

### 3. Install PlatformIO (Linux example)
```bash
python3 -c "$(curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py)"
export PATH="$HOME/.platformio/penv/bin:$PATH"
# add that export line to ~/.bashrc so it persists
```

### 4. Serial permissions (Linux)
If upload fails with “permission denied” on `/dev/ttyACM0` or `/dev/ttyUSB0`:

```bash
# Arch Linux
sudo usermod -aG uucp $USER

# Debian / Ubuntu
sudo usermod -aG dialout $USER
```

Log out and back in (or reboot), then plug the Stick in again.

PlatformIO udev rules (recommended once):
```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
  | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### 5. Plug in the StickC Plus2
1. Hold the power button until it turns on (if off).
2. Connect USB‑C to your computer.
3. Confirm a serial device appears:
   ```bash
   ls /dev/ttyACM* /dev/ttyUSB*
   lsusb   # should show an Espressif / UART device
   ```

### 6. Build and flash
```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd plebwatch
pio run            # compile only
pio run -t upload  # compile + flash
```

First build downloads the ESP32 toolchain and libraries (can take a few minutes).

### 7. First boot checklist
On the screen you should see roughly:
1. Custom splash (~8s)  
2. `WiFi...` / `Fetching...`  
3. **Watch face** (orange band + star + large time)

Then press **A** to cycle Watch → Stack → Markets → …

### 8. Battery / sleep behavior
| Situation | Behavior |
|---|---|
| On battery | Awake ~12s, then deep sleep **20 minutes**, wake, refresh, repeat |
| Charging / USB | Longer awake, refresh about every **3 minutes** |
| New block since last wake | Short beep |

For multi-day runtime: leave it unplugged with the screen allowed to sleep. Always-on display will drain the 200mAh cell in hours.

### 9. Make it yours
Ideas that are easy to fork:
- Change currency display (prices API also returns EUR, GBP, etc.)
- Add/remove pages in `src/ui.cpp` + `include/metrics.h`
- Adjust sleep intervals in `src/main.cpp` (`SLEEP_US_BATTERY`, `AWAKE_MS_BATTERY`)
- Point at your own mempool.space instance by changing URLs in `src/mempool_client.cpp`

## Project layout
```
plebwatch/
  platformio.ini          # Plus2 board + libraries
  include/config.h.example
  include/config.h        # your secrets (local only)
  include/metrics.h
  src/main.cpp            # sleep, buttons, boot flow
  src/wifi_connect.cpp    # try known SSIDs in order
  src/mempool_client.cpp  # HTTPS fetches
  src/ui.cpp              # TFT pages
```

## Troubleshooting

| Problem | Fix |
|---|---|
| No `/dev/ttyACM*` | Try another cable/port; hold power to wake Stick; install usb drivers |
| Upload to `/dev/ttyS0` | Stick not detected — unplug/replug; check `lsusb` |
| Permission denied on serial | `uucp`/`dialout` group + udev rules (step 4) |
| `No WiFi` on screen | 2.4 GHz only; check SSID/password spelling; try one network first |
| Black screen after unplug | Plus2 needs power-hold — use this firmware (M5Unified), not old Plus-only sketches |
| Dies in under an hour | Normal if screen stays on; let it deep-sleep between polls |
| Fetch fail | Captive portal / firewall blocking HTTPS; try home Wi‑Fi first |

## License
MIT — see [LICENSE](LICENSE). Fork it, flash it, improve it.
