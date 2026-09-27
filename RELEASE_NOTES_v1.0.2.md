# Binwalk-FZ v1.0.2

Binwalk-FZ analyzes firmware images and other binary files from a Flipper Zero. It provides two real analysis modes: a self-contained native scanner that runs directly on the Flipper, and an optional external mode that controls genuine upstream Binwalk running on a Raspberry Pi or Linux computer.

## Download and install

1. Download **Binwalk-FZ v1.0.2 FAP** from the Assets section below.
2. Connect a Flipper Zero with a microSD card to qFlipper.
3. Copy `binwalk_fz.fap` to `/ext/apps/Tools/` on the microSD card.
4. On the Flipper, open **Apps > Tools > Binwalk FZ**.

This build targets official Flipper firmware **1.4.3**, target **f7**, API **87.1**. An incompatible firmware API may require rebuilding the application.

## Option 1: analyze files directly on the Flipper

No Raspberry Pi or external computer is required for native analysis.

Copy a file to the Flipper microSD card, choose **Select File**, and use:

- **Scan Signatures** to detect structurally validated file formats at their real byte offsets.
- **Results** to inspect the type, offset, matching bytes, known size, and parsed metadata.
- **HEX Viewer** to view actual bytes and jump to a hexadecimal offset.
- **Strings** to extract printable ASCII strings.
- **Search** to find a case-sensitive ASCII byte sequence.
- **Entropy** to calculate whole-file and block Shannon entropy.
- **Report** to save completed measured/parser results to `/ext/apps_data/binwalk_fz/report.txt`.

The native scanner recognizes validated ELF, PE, ZIP, GZIP, 7-Zip, RAR4/RAR5, PNG, JPEG, GIF, PDF, SQLite, WAV, BMP, BZIP2, XZ, Zstandard, FLAC, and TAR structures. Files are streamed in bounded chunks and are not loaded completely into RAM.

## Option 2: genuine upstream Binwalk on Raspberry Pi/Linux

External mode is optional. A Raspberry Pi is not mandatory; a Linux laptop, desktop, mini PC, or Linux virtual machine can also be used.

The Linux computer runs the genuine upstream `binwalk` executable. The Flipper connects over 3.3 V UART and acts as the file selector, controller, and result display. The external computer supplies the processing power and full upstream signature database.

Basic setup:

1. Install upstream Binwalk and `pyserial` on the Linux computer.
2. Run the included `companion/binwalk_fz_bridge.py` companion.
3. Place files in `/var/lib/binwalk-fz/input`.
4. Connect crossed 3.3 V UART TX/RX and common ground. Never connect 5 V UART signaling to the Flipper.
5. Open **External Binwalk** on the Flipper, select a real file, and press OK to scan or cancel.

The Flipper displays the genuine Binwalk version, selected filename, scan state, detection count, and first-result details returned by Binwalk. Full extraction, recursion, filters, carving, and other advanced Binwalk CLI features remain available directly on the Linux computer. See `EXTERNAL_BINWALK.md` for wiring, installation, and service instructions.

## What changed in v1.0.2

- Reports are now written transactionally. A cancelled, failed, disk-full, or sync-failed save preserves the previous valid report.
- Long selected-file paths are no longer silently truncated in reports.
- Live progress uses atomic-width counters suitable for the Flipper's 32-bit processor.
- HEX goto now rejects malformed or overflowing offsets, and navigation cannot wrap around.
- External UART is initialized before its receive worker starts.
- Linux companion directory, stat, output, and cleanup failures now return bounded errors to the Flipper instead of crashing or appearing successful.
- Genuine Binwalk stderr diagnostics are returned when an external scan fails.
- Temporary external JSON results are deleted after success, cancellation, failure, and bridge restart.
- Native GZIP, GIF, RAR4, RAR5, and Zstandard validators now require stronger structural evidence before reporting a detection.
- Regression coverage now includes malformed formats, filesystem failures, temporary-result cleanup, cancellation, and an optional genuine-Binwalk end-to-end test.

## Important limits

- Native mode detects and inspects supported structures; it does not extract, decompress, execute, mount, or recursively scan embedded content.
- Native mode implements 18 validated format families rather than Binwalk's entire upstream signature catalog.
- External mode provides genuine upstream Binwalk analysis, but arbitrary shell commands are intentionally not exposed over UART.
- Device files larger than 4 GiB are rejected because the supported firmware storage API uses 32-bit seeking.
- Retained detections, strings, search hits, and entropy blocks are bounded to protect device memory; the application displays totals and truncation state.
- Raspberry Pi/Linux hardware integration still requires validation with the end user's particular computer, UART adapter, and wiring.

Only analyze firmware and files that you own or are authorized to inspect.

## Verification

- Native engine tests: passed.
- Companion tests: passed; the genuine-Binwalk test runs when Binwalk is installed.
- Clean Flipper build and APPCHK: passed for target f7/API 87.1.
- Authenticated Snyk Code scan: 0 findings at low severity or higher.
- FAP size: **48,432 bytes**.
- SHA-256: `16E13350901722C4DB9335FBCB6F7DC94302CE627D2A3714838B25A9ABC38904`.

Binwalk-FZ is licensed under **GNU GPL v3 or later**. Upstream Binwalk retains its MIT license and attribution.
