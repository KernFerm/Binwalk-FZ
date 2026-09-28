# Binwalk FZ v1.0.4

Binwalk FZ is a bounded, read-only binary analysis tool for Flipper Zero. It analyzes actual file bytes from microSD and can optionally control genuine upstream Binwalk running on a Raspberry Pi or another Linux computer.

## Native Flipper features

- Validates ELF, PE, ZIP, GZIP, 7-Zip, RAR4/RAR5, PNG, JPEG, GIF, PDF, SQLite, WAV, BMP, BZIP2, XZ, Zstandard, FLAC, and TAR structures.
- Reports actual hexadecimal/decimal offsets, matching signature bytes, descriptions, known sizes, and parsed metadata.
- Requires structural validation; a filename, extension, or unvalidated byte pattern cannot create a detection.
- Streams files in bounded chunks instead of loading the complete file into RAM.
- Includes a real HEX/ASCII viewer with navigation and hexadecimal goto.
- Extracts printable strings with configurable 4/6/8/12-byte minimum length.
- Calculates whole-file and per-block Shannon entropy with configurable block size.
- Searches for case-sensitive ASCII byte sequences and reports real offsets.
- Shows totals and truncation when bounded detection/string/search/entropy lists exceed 64 retained entries.
- Writes completed reports transactionally and preserves the previous valid report after write, sync, cancellation, or SD failure.

Native mode does not extract, decompress, execute, mount, carve, or recursively process detected content. Files above 4 GiB are rejected because firmware 1.4.3 exposes a 32-bit seek API.

## Genuine Binwalk on Raspberry Pi/Linux

External mode runs the real upstream `binwalk` executable on a Raspberry Pi, Linux laptop, desktop, mini PC, or VM. Input files stay under the fixed companion directory `/var/lib/binwalk-fz/input`.

The Flipper selects a real offered file, starts or cancels the process, and displays the genuine Binwalk version, detection count, and first-result details from bounded JSON output. Intermediate JSON is removed after success, cancellation, or failure. The fixed UART protocol exposes no shell command. Extraction, recursion, filters, carving, and the full upstream catalog remain available directly on Linux.

## Quick start

1. Copy a firmware or binary file to microSD.
2. Choose **Select File**, then **Scan Signatures**.
3. Open **Results** and select a detection for its offset and metadata.
4. Use **HEX Viewer**, **Strings**, **Entropy**, or **Search** for additional real file analysis.
5. Use **Reports** to save the completed measured result.
6. For genuine external Binwalk, follow `EXTERNAL_BINWALK.md`, connect 3.3 V UART, select a file offered by Linux, and press OK to run/cancel.

## Install and verification

Requires official Flipper firmware 1.4.3 or later and a microSD card. Copy `binwalk_fz.fap` to `/ext/apps/Tools/` and open **Apps → Tools → Binwalk FZ**.

- Native engine/protocol tests passed.
- External companion tests: 6 passed, 1 environment-dependent test skipped.
- uFBT APPCHK: target f7/API 87.1, no unresolved symbols.
- FAP size: 48,432 bytes.
- SHA-256: `080249425DE218CE51B87BD29EF3BFCBEABCC47B425100C8A56BD33597432A56`

Only analyze files you own or are authorized to inspect. Binwalk FZ is GNU GPL v3 or later and preserves upstream Binwalk MIT attribution.
