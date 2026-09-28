# Binwalk FZ

Binwalk FZ analyzes firmware and other binary files from a Flipper Zero. Its native read-only engine scans files on the microSD card and reports bounded, format-validated signatures at their real byte offsets. Version 1.0.4 also includes an external Linux/Raspberry Pi mode: genuine upstream Binwalk runs on the companion computer, while the Flipper acts as its UART controller and results display.

Current release: **v1.0.4**.

## Install the FAP

You need a Flipper Zero with a working microSD card. The supplied release build targets official firmware 1.4.3, target f7, API 87.1. A firmware build with an incompatible API may require the app to be rebuilt.

### Install with qFlipper

1. Download `binwalk_fz.fap` from the latest GitHub release or from this repository's `dist` folder.
2. Connect the Flipper Zero by USB and open qFlipper.
3. Open the microSD card file browser.
4. Open `apps`, then `Tools`.
5. Copy `binwalk_fz.fap` into `/ext/apps/Tools/`.
6. Safely disconnect the device.
7. On the Flipper, open **Apps → Tools → Binwalk FZ**.

The filename ends in `.fap` (Flipper Application Package), not `.fab`.

### Install directly from a microSD card

Put the microSD card in a computer, copy `binwalk_fz.fap` to `apps/Tools/`, safely eject the card, and return it to the Flipper Zero. The app then appears under **Apps → Tools**.

## Use the application

### Native signature analysis

1. Copy a firmware image or other binary file anywhere on the Flipper's microSD card.
2. Open **Select File** and choose the file. The filename and extension do not determine the result.
3. Open **Scan Signatures**. The worker displays real byte progress; press **Back** to request safe cancellation.
4. Open **Results** and select a detection to view its actual hexadecimal and decimal offset, matched signature bytes, description, known size, and parsed metadata.

The native scanner recognizes validated ELF, PE, ZIP, GZIP, 7-Zip, RAR, PNG, JPEG, GIF, PDF, SQLite, WAV, BMP, BZIP2, XZ, Zstandard, FLAC, and TAR structures. A byte pattern that fails its structural checks is not reported. See [SIGNATURE_COMPATIBILITY.md](SIGNATURE_COMPATIBILITY.md) for the exact validators.

### HEX viewer, strings, entropy, and search

- **HEX Viewer** displays real file bytes and ASCII. Up/Down moves 24 bytes, Left/Right moves four bytes, and OK opens hexadecimal goto.
- **Strings** finds real printable ASCII runs using the minimum length selected in Settings.
- **Entropy** calculates whole-file and per-block Shannon entropy using the selected block size.
- **Search** finds a case-sensitive ASCII byte sequence and reports its real offsets.

Files are streamed in bounded chunks instead of being loaded completely into Flipper RAM.

### Reports and settings

- **Reports** transactionally writes completed measured/parser results to `/ext/apps_data/binwalk_fz/report.txt`. A failed write or sync preserves the previous valid report.
- **Settings** selects a string minimum of 4/6/8/12 bytes and an entropy block size of 256/512/1024/2048 bytes.
- **Version** displays the installed application version.
- Select **About** and press OK for an on-device description of native and Raspberry Pi operation. Use Up/Down to scroll.

Cancelled, failed, or rejected operations do not create a partial analysis result.

### External Binwalk on Raspberry Pi/Linux

This mode runs the genuine upstream `binwalk` executable on a Raspberry Pi or Linux computer. It does not imitate signatures or generate placeholder results. Install and configure the companion first by following [EXTERNAL_BINWALK.md](EXTERNAL_BINWALK.md).

1. Install genuine upstream Binwalk and the Binwalk-FZ companion on the Pi/Linux computer.
2. Put files to analyze in `/var/lib/binwalk-fz/input` on the companion computer.
3. Connect crossed **3.3 V UART** TX/RX and a common ground between the Pi and Flipper. Do not connect 5 V signaling to Flipper GPIO.
4. In the Flipper's **Settings**, select the UART baud used by the companion.
5. Open **External Binwalk**. The screen reports the genuine Binwalk version and real files offered by the Pi.
6. Use Left/Right to select a file and OK to start or cancel its scan.
7. The Pi runs Binwalk and the Flipper displays the genuine detection count and first-result details returned from Binwalk's JSON output. The bounded intermediate JSON file is deleted after success, cancellation, or failure.
8. Press **Back** to stop the external session, close UART, and restore the Flipper expansion service.

Files remain on the Pi. Full extraction, recursion, filters, carving, and other upstream command-line features remain available directly on the Pi. The fixed BWF1 protocol does not expose arbitrary shell commands.

## What the app does not do

The native Flipper engine does not extract, decompress, execute, mount, or recursively process detected content. It implements 18 bounded validated types rather than the entire upstream Binwalk signature catalog. External mode provides the genuine upstream catalog on the Pi, but advanced extraction and CLI features are operated directly on the Pi.

`.nfc`, `.rfid`, `.sub`, UART captures, and firmware files are treated as arbitrary bytes unless their contents pass a real validator. No result is invented from a filename or extension. Retained detections, strings, search hits, and entropy blocks are bounded to 64 entries, with totals and truncation shown. Device files above 4 GiB are rejected because firmware 1.4.3 exposes a 32-bit seek API.

Only inspect firmware and files that you own or are authorized to analyze. See [FEATURE_MATRIX.md](FEATURE_MATRIX.md), [SECURITY.md](SECURITY.md), and [TESTING.md](TESTING.md) for exact support, resource limits, and validation evidence.

## Build from source

Requirements: Python 3, `ufbt` 0.2.6 or newer, and official firmware/SDK 1.4.3 (API 87.1) or a compatible SDK.

```powershell
python -m pip install --upgrade ufbt
python -m ufbt update --channel release
python tests/run_tests.py
python tests/test_companion.py
python -m ufbt
```

The build creates `dist/binwalk_fz.fap`. To build inside a full official firmware checkout, place the project at `applications_user/binwalk_fz` and run:

```sh
./fbt fap_binwalk_fz
```

## License

This repository is free software distributed under the **GNU General Public License, version 3 or later (`GPL-3.0-or-later`)**. Upstream Binwalk remains MIT-licensed and retains its attribution. See [LICENSE](LICENSE), [NOTICE](NOTICE), and [UPSTREAM_VERSION.md](UPSTREAM_VERSION.md).
