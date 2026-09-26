# Binwalk FZ

Binwalk FZ is a read-only Flipper Zero application for streaming binary signature analysis. It scans arbitrary files without loading them into RAM and reports only structurally validated detections at their real byte offsets. Version 1.0.0 also supports a Raspberry Pi/Linux companion running the genuine upstream Binwalk executable.

Current release: **v1.0.0**.

## Install the FAP

The supplied build targets official Flipper firmware 1.4.3, target f7, API 87.1. Copy `dist/binwalk_fz.fap` to `/ext/apps/Tools/` with qFlipper's microSD file browser, or place the microSD card in a computer and copy the FAP to `apps/Tools/`. On the device, open **Apps → Tools → Binwalk FZ**.

An incompatible firmware API may require rebuilding the FAP.

## Use

1. Choose **Select File** and select any file on the microSD card. Extensions do not affect analysis.
2. Choose **Scan Signatures**. The scan runs in a worker and shows real byte progress. Back requests cancellation.
3. Open **Results** and select a detection to view its actual offset, matched signature bytes, description, known size, and parsed metadata.
4. Use **HEX Viewer** for real bytes and ASCII. Up/Down move 24 bytes, Left/Right move four bytes, and OK opens hexadecimal goto.
5. Use **Strings** for printable ASCII runs, **Entropy** for whole-file and per-block Shannon entropy, or **Search** for a case-sensitive ASCII byte sequence.
6. Choose **Reports** to write completed results to `/ext/apps_data/binwalk_fz/report.txt`.
7. Use **Settings** to select a string minimum of 4/6/8/12 bytes and an entropy block size of 256/512/1024/2048 bytes. Settings also shows **Version**; select **About** and press OK for app, operating-mode, results-policy, and license information.

## External Binwalk on Raspberry Pi/Linux

The external mode runs genuine upstream Binwalk on a Raspberry Pi or Linux computer. Files stay on the Pi; the Flipper selects an input, starts or cancels a scan, and displays the real upstream result count and first detection returned through Binwalk's JSON output.

1. Install and start the companion by following [EXTERNAL_BINWALK.md](EXTERNAL_BINWALK.md).
2. Put files to analyze in `/var/lib/binwalk-fz/input` on the Pi.
3. Connect crossed 3.3 V UART TX/RX and common ground.
4. Open **External Binwalk** on the Flipper.
5. Use Left/Right to choose a Pi input and OK to start or cancel its scan.

The fixed BWF1 protocol does not provide arbitrary command execution. Full extraction, recursion, filters, carving, and other upstream CLI options remain available directly on the Pi.

Supported validated types are ELF, PE, ZIP, GZIP, 7-Zip, RAR, PNG, JPEG, GIF, PDF, SQLite, WAV, BMP, BZIP2, XZ, Zstandard, FLAC, and TAR. See [SIGNATURE_COMPATIBILITY.md](SIGNATURE_COMPATIBILITY.md) for exact validators.

## Important boundaries

The native Flipper engine does not extract, decompress, execute, mount, or recursively process detected content. It implements 18 bounded types, not upstream Binwalk's entire signature catalog. The external mode uses upstream Binwalk's genuine catalog on the Pi but leaves extraction and other advanced operations at the Pi command line. `.nfc`, `.rfid`, `.sub`, UART captures, and firmware files are treated as arbitrary bytes unless their contents pass a real validator. No result is invented from a filename or extension.

Retained results are bounded to 64 detections, strings, search hits, and entropy blocks; totals and truncation are stated. Device files above 4 GiB are rejected because firmware 1.4.3 exposes a 32-bit seek API.

## Build from source

Requirements are Python 3, uFBT, and the official release SDK:

```powershell
python -m pip install --upgrade ufbt
python -m ufbt update --channel release
python tests/run_tests.py
python -m ufbt
```

For a full firmware checkout, place the project at `applications_user/binwalk_fz` and run:

```sh
./fbt fap_binwalk_fz
```

See [PORTING_ANALYSIS.md](PORTING_ANALYSIS.md), [FEATURE_MATRIX.md](FEATURE_MATRIX.md), [SECURITY.md](SECURITY.md), and [TESTING.md](TESTING.md) for the architecture, limits, and evidence.

## License

Binwalk-FZ is licensed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`). Upstream Binwalk remains MIT-licensed. See [LICENSE](LICENSE), [NOTICE](NOTICE), and [UPSTREAM_VERSION.md](UPSTREAM_VERSION.md).
