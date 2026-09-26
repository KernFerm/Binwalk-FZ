# Porting analysis

## Upstream architecture

Binwalk 3.1.1 is a Rust 2024 desktop application. `src/magic.rs` registers 111 signature definitions. Each definition associates one or more byte patterns with a Rust parser and, where available, an internal or external extractor. `src/binwalk.rs` loads a complete target file into memory, searches its patterns with Aho-Corasick, invokes format-specific validators, resolves conflicts/overlaps, and can dispatch extraction. The entropy path divides an in-memory file into up to 2,048 blocks and uses the Rust `entropy` crate. Extraction relies on a mixture of Rust implementations and operating-system utilities.

The Cargo graph includes Aho-Corasick, serde/JSON, clap, chrono, threadpool, Plotly/Kaleido, compression libraries, hashing libraries, UUID generation, terminal formatting, filesystem walking, and a Git dependency. Desktop extraction also expects processes, directories, links, and utilities unavailable or inappropriate inside an external Flipper application.

## Flipper architecture

The Flipper port is a native C external application using only public firmware 1.4.3 APIs. It does not load the selected file into RAM. A `BwReader` abstraction exposes a bounded read-at operation and a known file size. The scanner reads 512-byte chunks and retains 15 bytes of overlap, one less than the longest registered magic pattern. Validators perform bounded absolute reads and checked arithmetic. Large 7-Zip next-header CRC validation is itself streamed in 512-byte pieces and observes cancellation.

The port uses a compact linear signature table rather than Aho-Corasick. With 22 registered patterns and 18 types, this costs more comparisons per byte but avoids the heap and generated automaton required by the desktop engine. Only a validated candidate becomes a result. Results are bounded to 64 retained detections while the total count and truncation state remain explicit.

Version 1.0.0 includes a separate external architecture. A Raspberry Pi/Linux companion invokes the genuine upstream Binwalk executable with fixed arguments and consumes its JSON log. The Flipper's BWF1 UART client selects among real files in a fixed Pi input directory, starts/cancels scans, and displays bounded genuine result fields. This preserves access to the upstream signature registry without claiming that the desktop Rust engine runs on the Flipper.

Strings, ASCII search, whole-file entropy, and block entropy are also streaming operations. Strings, search hits, entropy blocks, report output, and displayed metadata have fixed retention limits. The HEX viewer reads only the visible 24-byte window.

## Component decisions

| Upstream component | Flipper decision | Reason |
|---|---|---|
| Rust signature registry | Native: small audited C table; external: genuine upstream executable | Predictable native ROM/RAM plus full Pi-side upstream behavior |
| Aho-Corasick scan | Replaced by bounded linear streaming scan | Small pattern set and cross-buffer control |
| Format parsers | Independently adapted validators | Avoid whole-file slices and unchecked desktop assumptions |
| Entropy crate/Plotly graph | Replaced by exact Shannon calculation and numeric block list | No plotting/browser runtime |
| Whole-file memory reads | Replaced by 512-byte read-at operations | Bounded RAM |
| Internal/external extractors | Omitted | No safely validated extraction subset yet |
| Recursive extraction | Omitted | Depends on extraction and expands hostile input surface |
| JSON/terminal UI | Replaced by Flipper GUI and bounded text report | Native device interface |
| AFL++ harness | Replaced initially by warning-clean host stress tests | AFL++ is not installed in this environment |

## File treatment

All selected files are arbitrary binary inputs. `.nfc`, `.rfid`, `.sub`, UART captures, firmware images, archives, and documents pass through the same reader. Their filename extension does not create or imply a detection. No selected data is executed.
