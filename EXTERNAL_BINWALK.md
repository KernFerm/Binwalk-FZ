# External Binwalk companion

The companion runs the genuine upstream Binwalk executable on Raspberry Pi/Linux. The Flipper Zero is a UART controller and result display; it does not pretend the native device has the Pi's CPU, RAM, signature catalog, or filesystem.

## Hardware and wiring

Use three female-to-female jumpers. Both sides are 3.3 V UART. Do not connect 5 V or connect either device's power rail to the other.

| Flipper Zero | Raspberry Pi 40-pin header |
|---|---|
| Pin 13, USART TX | Physical pin 10, GPIO15/RX |
| Pin 14, USART RX | Physical pin 8, GPIO14/TX |
| Pin 11, GND | Physical pin 6, GND |

TX and RX are crossed. Binwalk FZ temporarily disables the Flipper expansion listener while its external screen owns USART, then restores it on exit.

## Prepare the Pi

1. Enable UART hardware and disable the serial login console with `sudo raspi-config`. Reboot after changing the serial settings.
2. Install genuine Binwalk v3 by following the [upstream installation documentation](https://github.com/ReFirmLabs/binwalk/wiki). Confirm that `binwalk --version` succeeds.
3. Copy this repository's `companion` directory to `/opt/binwalk-fz` and install the bridge dependency:

   ```sh
   cd /opt/binwalk-fz
   python3 -m venv venv
   ./venv/bin/pip install -r requirements.txt
   sudo mkdir -p /var/lib/binwalk-fz/input /var/lib/binwalk-fz/output
   ```

4. Copy authorized inputs into `/var/lib/binwalk-fz/input`.
5. Connect UART and start the bridge:

   ```sh
   sudo /opt/binwalk-fz/venv/bin/python /opt/binwalk-fz/binwalk_fz_bridge.py \
     --serial /dev/serial0 --baud 115200
   ```

6. On the Flipper, select **Settings -> External baud -> 115200**, then open **External Binwalk**.

Left/Right cycles through real regular files in the input directory. OK starts `binwalk --quiet --threads 1 --log ...` with a fixed argument vector. Completed JSON records remain under `/var/lib/binwalk-fz/output`. The Flipper displays the actual result count plus the first result's offset, size, confidence, name, and description.

The bridge accepts only `HELLO`, `STATUS`, `NEXT`, `PREV`, `SCAN`, and `STOP`. It rejects oversized/non-ASCII lines and does not pass UART text to a shell.

For startup at boot, copy `binwalk-fz-bridge.service.example` to `/etc/systemd/system/binwalk-fz-bridge.service` and run:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now binwalk-fz-bridge.service
```

Physical Raspberry Pi/UART testing remains pending until the Pi is available and must not be claimed as complete before then.

Official references: [Flipper expansion UART mapping](https://developer.flipper.net/flipperzero/doxygen/expansion_protocol.html) and [Raspberry Pi UART configuration](https://www.raspberrypi.com/documentation/computers/configuration.html).
