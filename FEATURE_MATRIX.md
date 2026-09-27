# Feature matrix

| Feature | Status | Notes |
|---|---|---|
| Arbitrary file selection | Implemented | Read-only file browser; no extension assumption |
| External genuine Binwalk scan | Implemented; hardware validation pending | Raspberry Pi/Linux runs upstream `binwalk --log`; Flipper selects input and shows genuine JSON result fields over bounded UART |
| External full CLI/extraction | Available directly on Pi | Deliberately not exposed as arbitrary UART commands |
| Streaming signature scan | Implemented | 512-byte reads with 15-byte overlap |
| Cross-buffer detection | Implemented | Host-tested with a 16-byte PNG signature starting at offset 507 |
| Validated offsets | Implemented | 64-bit core offsets; official storage API limits device files to 4 GiB |
| Detection details | Implemented | Type, description, actual magic, offset, known size, parsed metadata |
| HEX viewer | Implemented | Actual bytes, offsets, ASCII, directional navigation, hexadecimal goto |
| Printable strings | Implemented | Streaming ASCII extraction; configurable minimum 4/6/8/12 |
| ASCII search | Implemented | Streaming, cross-buffer, first 64 offsets retained |
| Whole-file entropy | Implemented | Shannon entropy in bits per byte |
| Block entropy | Implemented | Configurable 256/512/1024/2048-byte blocks; first 64 retained plus exact summary |
| Progress/cancellation | Implemented | Worker thread; Back requests cancellation and closes the file |
| Reports | Implemented | Transactional, synchronized app-data report containing only completed measurements |
| Extraction/decompression | Unavailable | No extraction claim, output path, or execution path exists |
| Recursive scanning | Unavailable | Would depend on extraction |
| Full upstream 111-signature set | Native: partial; external: upstream | 18 validated native types; external Pi invokes the recorded genuine upstream implementation |

Limits are visible rather than replaced with guessed results. A raw byte pattern that fails its structural validator is not reported.
