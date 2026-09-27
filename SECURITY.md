# Security

## Input model

Every selected file is untrusted. The application opens it read-only, never executes it, and never invokes a decompressor or external program. Extraction is unavailable.

In external mode, the Linux companion invokes the locally installed genuine Binwalk executable using a fixed argument vector and no shell. It scans only real regular, non-symlink files beneath `/var/lib/binwalk-fz/input`. UART cannot provide a path or command line. Transient JSON output is created beneath `/var/lib/binwalk-fz/output`, rejected above 32 MiB before parsing, and removed after success, cancellation, or failure.

The core checks `offset + length` as `length <= size - offset`, validates declared sizes before reads, constrains 7-Zip 64-bit additions, and rejects device files above the official 32-bit storage seek limit. Candidate magics must pass format-specific validation before becoming detections.

## Resource bounds

- File reads are at most 512 bytes.
- Signature overlap is 15 bytes.
- At most 64 detections, strings, search hits, and block-entropy values are retained.
- String text is retained to 47 printable bytes while the real run length is recorded.
- Reports use dynamically sized lines and transactional `.partial`/`.backup` replacement.
- The HEX viewer reads only 24 bytes per window.
- All long analyses run on a worker thread and observe cancellation.
- External UART lines, numeric fields, tokens, result JSON size, file count, and displayed fields are bounded.

## Output safety

Reports are written only to the application's fixed data path. No filename from the analyzed input becomes an output path. The report contains completed parser/measurement results only. Writes are synchronized before promotion, and a failed write, sync, or promotion does not replace the previous valid report.

The companion uses a direct physical UART with no cryptographic authentication. Treat the connected Pi and wiring as trusted local hardware. Advanced upstream extraction and external helper programs are not exposed through the Flipper bridge.

## Responsible use

Analyze only files and device data you own or are authorized to inspect. Report reproducible crashes with the smallest non-sensitive input possible, firmware version, and exact app build hash.

Authenticated Snyk Code analysis restricted to the Binwalk-FZ project completed with zero low-or-higher findings after shell-based compiler discovery was removed.
