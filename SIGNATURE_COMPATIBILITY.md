# Signature compatibility

The native FAP registers 22 magic patterns covering 18 validated types. External mode uses the genuine signature registry installed with upstream Binwalk on the Pi.

| Type | Magic/anchor | Validation and metadata |
|---|---|---|
| ELF | `7F 45 4C 46` | Class, byte order, version, machine |
| Windows PE | `4D 5A` | Checked DOS `e_lfanew`, `PE\0\0`, machine, section count, optional-header size |
| ZIP | `50 4B 03 04` | Local-header version, flags, method, name/extra lengths, packed-data bounds |
| GZIP | `1F 8B 08` | Deflate method, reserved flags, bounded optional fields, header/trailer bounds, timestamp, OS |
| 7-Zip | `37 7A BC AF 27 1C` | Start-header CRC, checked next-header range, streamed next-header CRC, version/size |
| RAR | Full RAR4 and RAR5 signatures | RAR4 archive-header bounds plus version-specific EOF marker and measured archive size |
| PNG | 16-byte PNG/IHDR pattern | IHDR length/type, dimensions/methods, IHDR CRC |
| JPEG | Upstream JFIF, Exif, and DQT starts | SOI/marker, segment length/bounds, marker family |
| GIF | `GIF87a`, `GIF89a` | Logical dimensions, color tables, extensions, image blocks/sub-blocks, LZW code size, and trailer |
| PDF | `%PDF-` | PDF 1.x/2.x numeric version and required line ending |
| SQLite | `SQLite format 3\0` | 100-byte database header, page size, format versions, payload fractions |
| WAV | `RIFF` | `WAVE`/`fmt ` structure, declared RIFF bounds, channels/rate/bit depth |
| BMP | `BM` | File size, pixel offset, DIB size, file bounds |
| BZIP2 | `BZh` | Block-size digit and block header magic |
| XZ | `FD 37 7A 58 5A 00` | Stream flags and header CRC |
| Zstandard | `28 B5 2F FD` | Frame descriptor, optional fields, block headers/types/sizes, last-block marker, checksum bounds, measured size |
| FLAC | `fLaC` | Required first STREAMINFO block and length |
| TAR | `ustar` at header offset 257 | Rebased file offset and POSIX header checksum |

The table is intentionally smaller than upstream Binwalk's 111-definition registry. Unsupported patterns are not reported by name and are never inferred from file extensions.
