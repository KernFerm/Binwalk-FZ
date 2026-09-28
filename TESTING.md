# Testing

## Automated host suite

Run:

```powershell
python tests/run_tests.py
```

The suite compiles `bw_engine.c` and `bw_external_protocol.c` as C11 with MSVC `/W4 /WX` on Windows or `-Wall -Wextra -Werror` elsewhere. It currently validates:

- known-answer headers for all 18 supported types;
- offset-zero and non-zero detections;
- a 16-byte PNG signature beginning at offset 507 across the 512-byte chunk boundary;
- truncated signatures, corrupt PE offsets, invalid PDF versions, invalid PNG CRC, missing RAR EOF, incomplete GIF, and malformed/incomplete Zstandard frames;
- printable strings spanning a chunk boundary with exact offset and length;
- ASCII search spanning a chunk boundary;
- Shannon entropy of zero data (`0.0`) and a uniform 256-symbol distribution (`8.0`);
- zero-byte input, read failure, cancellation, and a 2 MiB scan with no read above 512 bytes.
- exact BWF1 INFO/STATUS parsing, fragmented UART delivery, integer overflow, invalid file index, and oversized-line rejection;
- 1,000 deterministic arbitrary byte buffers from zero through 1,024 bytes.

Result on 2026-09-27: all engine tests passed.

`python tests/test_companion.py` validates real input enumeration, bounded tokens, transient JSON cleanup, bounded filesystem failures, propagation of genuine Binwalk diagnostics, and parsing of upstream JSON fields. It also contains an end-to-end test that executes the installed genuine `binwalk`; this test is explicitly reported as skipped when Binwalk is unavailable rather than being claimed as passed. The companion also passes `py_compile`.

## Firmware build

```powershell
ufbt -c
ufbt
```

The current source compiles and links against official firmware SDK 1.4.3, passes APPCHK for target f7/API 87.1, and produces `dist/binwalk_fz.fap`.

The clean v1.0.4 artifact is 48,432 bytes with SHA-256
`080249425DE218CE51B87BD29EF3BFCBEABCC47B425100C8A56BD33597432A56`.

## Device acceptance

The following must be recorded from the attached physical Flipper before calling the release hardware-validated:

1. Install and open the FAP without a crash.
2. Select a real mixed fixture and verify signature offsets/details.
3. Navigate HEX and goto a known offset; compare displayed bytes.
4. Run strings, search, and entropy; compare with host-known answers.
5. Cancel a large scan and verify the file closes without a partial result.
6. Save and read back the report from `/ext/apps_data/binwalk_fz/report.txt`. Then force cancellation/write failure while a known report exists and confirm the known report remains unchanged with no `.partial` file.
7. Remove the SD card during an operation and verify a closed error without a crash.
8. When Raspberry Pi hardware is available, verify BWF1 handshake, real Binwalk version, Pi file selection, scan/cancel, genuine result counts/offsets, disconnect recovery, and UART/expansion release.

No device result is recorded here until it is observed.

Partial observation on 2026-09-26: the v1.0.0/native build installed and launched, `boundary_png.bin` completed scanning, and the device opened a populated **Validated detections** list. The exact displayed type and offset were not transcribed, so this does not yet satisfy the offset/detail acceptance item. External v1.0.0 Pi behavior remains untested without Pi hardware.

## Snyk status

The FAP has no package dependency manifest. The companion pins pyserial 3.5. Authenticated Snyk Code analysis restricted to Binwalk-FZ on 2026-09-27 completed with zero low-or-higher findings; no finding was ignored or suppressed.
