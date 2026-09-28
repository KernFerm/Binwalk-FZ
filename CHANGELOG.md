# Changelog

## 1.0.4 — 2026-09-27

- Refreshed application and companion version metadata.
- Rebuilt and revalidated the target f7/API 87.1 FAP.

## 1.0.2 — 2026-09-27

- Made native reports transactional and required successful microSD synchronization before replacement.
- Replaced fixed report formatting buffers so long input paths are not silently truncated.
- Changed live progress sharing to atomic-width counters for the 32-bit device.
- Added strict HEX goto validation and overflow-safe navigation.
- Initialized UART before starting the external receive worker.
- Added bounded companion filesystem and genuine Binwalk error reporting.
- Made companion JSON logs transient across success, cancellation, and failure.
- Strengthened native GZIP, RAR, GIF, and Zstandard structural validation.
- Added failure-path companion tests and an optional genuine-Binwalk end-to-end test.

## 1.0.1 — 2026-09-26

- Expanded the on-device About page with clear Raspberry Pi/Linux companion instructions.
- Reworked the README into an end-user installation and usage guide matching the Aircrack-ng-FZ release format.
- Kept the downloadable FAP and genuine-result boundaries explicit for GitHub release users.

## 1.0.0 — 2026-09-26

- Added a bounded BWF1 UART protocol and Raspberry Pi/Linux controller screen.
- Added a Linux companion that runs genuine upstream Binwalk and consumes its JSON results.
- Added real Pi input selection, scan/cancel state, detection count, and first-result details.
- Corrected RAR4 header validation and added exact RAR4/RAR5 patterns.
- Added PDF 2.x support and strengthened PNG, WAV, BMP, and RAR bounds.
- Ensured cancelled/failed native tasks cannot remain available to reports as partial results.
- Removed shell-command construction from the Windows host-test runner.
- Added protocol, deterministic stress, and companion JSON tests.
- Passed the official target f7/API 87.1 build and authenticated Snyk Code scan.
- Added native streaming signature, strings, search, entropy, HEX, report, and settings implementation.
- Added a Settings About page and visible application version.
- Licensed Binwalk-FZ under GNU GPL v3 or later while retaining upstream MIT attribution.
