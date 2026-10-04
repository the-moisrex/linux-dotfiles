# fan-control

Quiets an ASUS TUF GAMING Z790-PLUS WIFI D4 + MSI MEG Coreliquid S360 box on Linux.

## What it sets up

| Component | Mechanism | After install |
|-----------|-----------|---------------|
| AIO (pump/radiator fans) | [coreliquid_driver](https://github.com/sarzeaud/coreliquid_driver) daemon, SMART mode, feeds `coretemp` over USB HID | `my_msi_driver.service` |
| Motherboard fan headers | in-kernel `nct6775` (NCT6798D) + lm_sensors `fancontrol` | `fancontrol.service`, `/etc/fancontrol` |
| Boot persistence | `modules-load.d/nct6775.conf` + both units enabled | — |

No liquidctl (unsupported for S360), no AUR packages.

## Usage

```bash
sudo ./fan-control/map-fans.sh        # discover pwm <-> fan mapping (sweeps duty)
./fan-control/setup-fan-control.sh    # install everything (needs sudo internally)
./fan-control/setup-fan-control.sh --uninstall
```

## Measured values (this board)

- **Stall floor**: pwm duty <= 64 stops fan7; >= 72 starts it. Config uses `MINPWM=80`
  for margin (~885 RPM, inaudible over pump), `MINSTART=110` for kick-start.
- **Curve**: package temp 40°C -> duty 80, 70°C -> duty 255, `AVERAGE=2`, `INTERVAL=10`.
- **Mapping**: `pwm7` drives `fan7`. Other channels (pwm1/2/4/5/6) drive nothing —
  fan2's tach comes from the AIO, which `my_msi_driver` already controls.
- Under 16-thread load: package 85°C, both systems ramped to full duty within ~10s.

## Gotchas

- `/etc/fancontrol` bakes in `hwmonN` names + device paths; if they change
  (BIOS update, module load order), fancontrol exits with "configuration appears
  to be outdated" — re-run `setup-fan-control.sh`.
- `map-fans.sh` briefly runs fans at 100% and at 0% — don't be alarmed.
- AIO daemon dies if the HID device vanishes; `Restart=on-failure` covers it.
