/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bw_engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    BwType type;
    const uint8_t* magic;
    uint8_t length;
    uint16_t magic_offset;
} BwPattern;

static const uint8_t magic_elf[] = {0x7F, 'E', 'L', 'F'};
static const uint8_t magic_mz[] = {'M', 'Z'};
static const uint8_t magic_zip[] = {'P', 'K', 0x03, 0x04};
static const uint8_t magic_gzip[] = {0x1F, 0x8B, 0x08};
static const uint8_t magic_7z[] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
static const uint8_t magic_rar4[] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x00};
static const uint8_t magic_rar5[] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00};
static const uint8_t magic_png[] = {
    0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0x0D, 'I', 'H', 'D', 'R'};
static const uint8_t magic_jfif[] = {0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10, 'J', 'F', 'I', 'F', 0};
static const uint8_t magic_exif[] = {0xFF, 0xD8, 0xFF, 0xE1};
static const uint8_t magic_jpeg_db[] = {0xFF, 0xD8, 0xFF, 0xDB};
static const uint8_t magic_gif87[] = {'G', 'I', 'F', '8', '7', 'a'};
static const uint8_t magic_gif89[] = {'G', 'I', 'F', '8', '9', 'a'};
static const uint8_t magic_pdf[] = {'%', 'P', 'D', 'F', '-'};
static const uint8_t magic_sqlite[] = {'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f', 'o', 'r', 'm', 'a', 't', ' ', '3', 0};
static const uint8_t magic_riff[] = {'R', 'I', 'F', 'F'};
static const uint8_t magic_bmp[] = {'B', 'M'};
static const uint8_t magic_bzip2[] = {'B', 'Z', 'h'};
static const uint8_t magic_xz[] = {0xFD, '7', 'z', 'X', 'Z', 0};
static const uint8_t magic_zstd[] = {0x28, 0xB5, 0x2F, 0xFD};
static const uint8_t magic_flac[] = {'f', 'L', 'a', 'C'};
static const uint8_t magic_ustar[] = {'u', 's', 't', 'a', 'r'};

static const BwPattern patterns[] = {
    {BwTypeElf, magic_elf, sizeof(magic_elf), 0},
    {BwTypePe, magic_mz, sizeof(magic_mz), 0},
    {BwTypeZip, magic_zip, sizeof(magic_zip), 0},
    {BwTypeGzip, magic_gzip, sizeof(magic_gzip), 0},
    {BwType7zip, magic_7z, sizeof(magic_7z), 0},
    {BwTypeRar, magic_rar4, sizeof(magic_rar4), 0},
    {BwTypeRar, magic_rar5, sizeof(magic_rar5), 0},
    {BwTypePng, magic_png, sizeof(magic_png), 0},
    {BwTypeJpeg, magic_jfif, sizeof(magic_jfif), 0},
    {BwTypeJpeg, magic_exif, sizeof(magic_exif), 0},
    {BwTypeJpeg, magic_jpeg_db, sizeof(magic_jpeg_db), 0},
    {BwTypeGif, magic_gif87, sizeof(magic_gif87), 0},
    {BwTypeGif, magic_gif89, sizeof(magic_gif89), 0},
    {BwTypePdf, magic_pdf, sizeof(magic_pdf), 0},
    {BwTypeSqlite, magic_sqlite, sizeof(magic_sqlite), 0},
    {BwTypeWav, magic_riff, sizeof(magic_riff), 0},
    {BwTypeBmp, magic_bmp, sizeof(magic_bmp), 0},
    {BwTypeBzip2, magic_bzip2, sizeof(magic_bzip2), 0},
    {BwTypeXz, magic_xz, sizeof(magic_xz), 0},
    {BwTypeZstd, magic_zstd, sizeof(magic_zstd), 0},
    {BwTypeFlac, magic_flac, sizeof(magic_flac), 0},
    {BwTypeTar, magic_ustar, sizeof(magic_ustar), 257},
};

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t* p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static uint16_t be16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool range_ok(const BwReader* reader, uint64_t offset, uint64_t length) {
    return offset <= reader->size && length <= reader->size - offset;
}

static bool read_exact(const BwReader* reader, uint64_t offset, uint8_t* data, size_t length) {
    size_t actual = 0;
    return range_ok(reader, offset, length) && reader->read_at(reader->context, offset, data, length, &actual) &&
           actual == length;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t length) {
    crc = ~crc;
    for(size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for(uint8_t bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static bool crc32_reader(
    const BwReader* reader,
    uint64_t offset,
    uint64_t length,
    uint32_t* value,
    BwCancelled cancelled,
    void* callback_context) {
    uint8_t buffer[BW_IO_CHUNK];
    uint32_t crc = 0;
    while(length) {
        if(cancelled && cancelled(callback_context)) return false;
        size_t part = length > sizeof(buffer) ? sizeof(buffer) : (size_t)length;
        if(!read_exact(reader, offset, buffer, part)) return false;
        crc = crc32_update(crc, buffer, part);
        offset += part;
        length -= part;
    }
    *value = crc;
    return true;
}

static bool detection_exists(const BwScanResult* result, BwType type, uint64_t offset) {
    for(uint32_t i = 0; i < result->count; i++) {
        if(result->detections[i].type == type && result->detections[i].offset == offset) return true;
    }
    return false;
}

static bool validate_detection(
    const BwReader* reader,
    BwType type,
    uint64_t offset,
    const uint8_t* matched_magic,
    uint8_t magic_length,
    BwDetection* out,
    BwCancelled cancelled,
    void* callback_context) {
    uint8_t h[128];
    memset(out, 0, sizeof(*out));
    out->offset = offset;
    out->type = type;
    out->magic_length = magic_length;
    memcpy(out->magic, matched_magic, magic_length);

    switch(type) {
    case BwTypeElf: {
        if(!read_exact(reader, offset, h, 20) || h[4] < 1 || h[4] > 2 || h[5] < 1 || h[5] > 2 || h[6] != 1)
            return false;
        uint16_t machine = h[5] == 1 ? le16(h + 18) : be16(h + 18);
        snprintf(out->metadata, sizeof(out->metadata), "%u-bit, %s endian, machine 0x%04X", h[4] == 1 ? 32U : 64U, h[5] == 1 ? "little" : "big", machine);
        return true;
    }
    case BwTypePe: {
        if(!read_exact(reader, offset, h, 64)) return false;
        uint32_t pe_offset = le32(h + 0x3C);
        if(pe_offset < 64 || pe_offset > 0x100000U || !range_ok(reader, offset, pe_offset + 24U) ||
           !read_exact(reader, offset + pe_offset, h, 24) || memcmp(h, "PE\0\0", 4)) return false;
        uint16_t sections = le16(h + 6);
        uint16_t optional = le16(h + 20);
        if(!sections || sections > 96 || optional > 512) return false;
        snprintf(out->metadata, sizeof(out->metadata), "machine 0x%04X, %u sections", le16(h + 4), sections);
        return true;
    }
    case BwTypeZip: {
        if(!read_exact(reader, offset, h, 30)) return false;
        uint16_t version = le16(h + 4), method = le16(h + 8), name = le16(h + 26), extra = le16(h + 28);
        uint32_t packed = le32(h + 18);
        uint64_t header_size = 30ULL + name + extra;
        if(version < 10 || version > 100 || method > 99 || !range_ok(reader, offset, header_size) ||
           (!(le16(h + 6) & 8U) && !range_ok(reader, offset, header_size + packed))) return false;
        snprintf(out->metadata, sizeof(out->metadata), "version %u.%u, method %u, packed %lu", version / 10, version % 10, method, (unsigned long)packed);
        return true;
    }
    case BwTypeGzip: {
        if(!read_exact(reader, offset, h, 10) || h[2] != 8 || (h[3] & 0xE0U)) return false;
        snprintf(out->metadata, sizeof(out->metadata), "deflate, flags 0x%02X, mtime %lu, OS %u", h[3], (unsigned long)le32(h + 4), h[9]);
        return true;
    }
    case BwType7zip: {
        if(!read_exact(reader, offset, h, 32)) return false;
        uint32_t start_crc = le32(h + 8);
        if(crc32_update(0, h + 12, 20) != start_crc) return false;
        uint64_t next_offset = le64(h + 12), next_size = le64(h + 20);
        if(next_offset > UINT64_MAX - 32 || next_size > UINT64_MAX - 32 - next_offset ||
           !range_ok(reader, offset, 32 + next_offset + next_size)) return false;
        uint32_t next_crc = 0;
        if(!crc32_reader(
               reader,
               offset + 32 + next_offset,
               next_size,
               &next_crc,
               cancelled,
               callback_context) ||
           next_crc != le32(h + 28))
            return false;
        out->size = 32 + next_offset + next_size;
        snprintf(out->metadata, sizeof(out->metadata), "version %u.%u, size %llu", h[6], h[7], (unsigned long long)out->size);
        return true;
    }
    case BwTypeRar: {
        if(magic_length == sizeof(magic_rar4)) {
            if(!read_exact(reader, offset, h, 14) || h[9] != 0x73 || le16(h + 12) < 7 ||
               !range_ok(reader, offset, 7ULL + le16(h + 12)))
                return false;
            snprintf(out->metadata, sizeof(out->metadata), "RAR version 4 archive");
        } else if(magic_length == sizeof(magic_rar5)) {
            if(!read_exact(reader, offset, h, sizeof(magic_rar5))) return false;
            snprintf(out->metadata, sizeof(out->metadata), "RAR version 5 archive");
        } else {
            return false;
        }
        return true;
    }
    case BwTypePng: {
        if(!read_exact(reader, offset, h, 33) || be32(h + 8) != 13 || memcmp(h + 12, "IHDR", 4)) return false;
        uint32_t width = be32(h + 16), height = be32(h + 20);
        bool depth_ok =
            (h[25] == 0 && (h[24] == 1 || h[24] == 2 || h[24] == 4 || h[24] == 8 ||
                            h[24] == 16)) ||
            (h[25] == 2 && (h[24] == 8 || h[24] == 16)) ||
            (h[25] == 3 && (h[24] == 1 || h[24] == 2 || h[24] == 4 || h[24] == 8)) ||
            ((h[25] == 4 || h[25] == 6) && (h[24] == 8 || h[24] == 16));
        if(!width || !height || !depth_ok || h[26] || h[27] || h[28] > 1) return false;
        if(crc32_update(0, h + 12, 17) != be32(h + 29)) return false;
        snprintf(out->metadata, sizeof(out->metadata), "%lux%lu, depth %u, color %u", (unsigned long)width, (unsigned long)height, h[24], h[25]);
        return true;
    }
    case BwTypeJpeg: {
        if(!read_exact(reader, offset, h, magic_length > 6 ? magic_length : 6) || h[0] != 0xFF ||
           h[1] != 0xD8 || h[2] != 0xFF || be16(h + 4) < 2 ||
           !range_ok(reader, offset + 4, be16(h + 4)))
            return false;
        const char* marker = h[3] == 0xE0 ? "JFIF" : h[3] == 0xE1 ? "Exif" : "quantization-table";
        snprintf(out->metadata, sizeof(out->metadata), "%s JPEG stream", marker);
        return true;
    }
    case BwTypeGif: {
        if(!read_exact(reader, offset, h, 13)) return false;
        uint16_t width = le16(h + 6), height = le16(h + 8);
        if(!width || !height) return false;
        snprintf(out->metadata, sizeof(out->metadata), "%c%c%c%c%c%c, %ux%u", h[0], h[1], h[2], h[3], h[4], h[5], width, height);
        return true;
    }
    case BwTypePdf: {
        if(!read_exact(reader, offset, h, 9) || (h[5] != '1' && h[5] != '2') ||
           h[6] != '.' || h[7] < '0' || h[7] > '9' ||
           (h[8] != '\r' && h[8] != '\n'))
            return false;
        snprintf(out->metadata, sizeof(out->metadata), "PDF version %c.%c", h[5], h[7]);
        return true;
    }
    case BwTypeSqlite: {
        if(!read_exact(reader, offset, h, 100)) return false;
        uint32_t page_size = be16(h + 16);
        if(page_size == 1) page_size = 65536;
        if(page_size < 512 || page_size > 65536 || (page_size & (page_size - 1)) || h[18] < 1 || h[18] > 2 ||
           h[19] < 1 || h[19] > 2 || h[20] != 0 || h[21] != 64 || h[22] != 32 || h[23] != 32) return false;
        snprintf(out->metadata, sizeof(out->metadata), "page size %lu, read/write %u/%u", (unsigned long)page_size, h[19], h[18]);
        return true;
    }
    case BwTypeWav: {
        if(!read_exact(reader, offset, h, 44) || memcmp(h + 8, "WAVE", 4) || memcmp(h + 12, "fmt ", 4) || le32(h + 16) < 16) return false;
        uint32_t declared = le32(h + 4);
        uint16_t format = le16(h + 20), channels = le16(h + 22), bits = le16(h + 34);
        if(declared < 36 || !format || !channels || !le32(h + 24) || !bits ||
           !range_ok(reader, offset, (uint64_t)declared + 8))
            return false;
        out->size = (uint64_t)declared + 8;
        snprintf(out->metadata, sizeof(out->metadata), "%u channels, %lu Hz, %u-bit", channels, (unsigned long)le32(h + 24), bits);
        return true;
    }
    case BwTypeBmp: {
        if(!read_exact(reader, offset, h, 30)) return false;
        uint32_t size = le32(h + 2), pixels = le32(h + 10), dib = le32(h + 14);
        if(dib < 12 || dib > UINT32_MAX - 14U || size < 14U + dib ||
           pixels < 14U + dib || pixels >= size || !range_ok(reader, offset, size))
            return false;
        out->size = size;
        snprintf(out->metadata, sizeof(out->metadata), "DIB %lu bytes, image size %lu", (unsigned long)dib, (unsigned long)size);
        return true;
    }
    case BwTypeBzip2: {
        if(!read_exact(reader, offset, h, 10) || h[3] < '1' || h[3] > '9' || memcmp(h + 4, "1AY&SY", 6)) return false;
        snprintf(out->metadata, sizeof(out->metadata), "block size %u00 kB", h[3] - '0');
        return true;
    }
    case BwTypeXz: {
        if(!read_exact(reader, offset, h, 12) || h[6] || (h[7] & 0xF0U) || crc32_update(0, h + 6, 2) != le32(h + 8)) return false;
        snprintf(out->metadata, sizeof(out->metadata), "stream flags 0x%02X%02X", h[6], h[7]);
        return true;
    }
    case BwTypeZstd: {
        if(!read_exact(reader, offset, h, 6) || (h[4] & 0x08U)) return false;
        snprintf(out->metadata, sizeof(out->metadata), "frame descriptor 0x%02X", h[4]);
        return true;
    }
    case BwTypeFlac: {
        if(!read_exact(reader, offset, h, 8) || (h[4] & 0x7FU) != 0 || be32(h + 4) % 0x1000000U != 34 || !range_ok(reader, offset, 42)) return false;
        snprintf(out->metadata, sizeof(out->metadata), "FLAC STREAMINFO present");
        return true;
    }
    case BwTypeTar: {
        uint8_t block[512];
        if(!read_exact(reader, offset, block, sizeof(block)) || memcmp(block + 257, "ustar", 5)) return false;
        uint32_t expected = 0;
        for(size_t i = 148; i < 156; i++) {
            if(block[i] == 0 || block[i] == ' ') continue;
            if(block[i] < '0' || block[i] > '7') return false;
            expected = (expected << 3) + (block[i] - '0');
        }
        uint32_t sum = 0;
        for(size_t i = 0; i < sizeof(block); i++) sum += (i >= 148 && i < 156) ? ' ' : block[i];
        if(sum != expected) return false;
        snprintf(out->metadata, sizeof(out->metadata), "ustar header, checksum valid");
        return true;
    }
    }
    return false;
}

const char* bw_type_name(BwType type) {
    static const char* names[] = {"ELF", "PE", "ZIP", "GZIP", "7-Zip", "RAR", "PNG", "JPEG", "GIF", "PDF", "SQLite", "WAV", "BMP", "BZIP2", "XZ", "Zstandard", "FLAC", "TAR"};
    return type <= BwTypeTar ? names[type] : "Unknown";
}

const char* bw_type_description(BwType type) {
    static const char* descriptions[] = {"ELF executable", "Windows PE executable", "ZIP archive", "gzip compressed data", "7-Zip archive", "RAR archive", "PNG image", "JPEG image", "GIF image", "PDF document", "SQLite database", "RIFF/WAVE audio", "Windows bitmap", "bzip2 compressed data", "XZ compressed data", "Zstandard frame", "FLAC audio", "POSIX tar archive"};
    return type <= BwTypeTar ? descriptions[type] : "Unknown data";
}

uint32_t bw_signature_count(void) {
    return BwTypeTar + 1U;
}

bool bw_scan(const BwReader* reader, BwScanResult* result, BwCancelled cancelled, BwProgress progress, void* callback_context) {
    if(!reader || !reader->read_at || !result) return false;
    memset(result, 0, sizeof(*result));
    uint8_t buffer[BW_IO_CHUNK + BW_MAGIC_MAX - 1];
    size_t carry = 0;
    uint64_t position = 0, processed = 0;
    while(position < reader->size || (reader->size == 0 && position == 0)) {
        if(cancelled && cancelled(callback_context)) { result->cancelled = true; return false; }
        size_t wanted = (reader->size - position) > BW_IO_CHUNK ? BW_IO_CHUNK : (size_t)(reader->size - position);
        size_t actual = 0;
        if(wanted && (!reader->read_at(reader->context, position, buffer + carry, wanted, &actual) || actual != wanted)) {
            result->io_error = true; return false;
        }
        size_t total = carry + actual;
        uint64_t base = position - carry;
        bool eof = position + actual >= reader->size;
        uint64_t safe_end = eof ? base + total : (total >= BW_MAGIC_MAX - 1 ? base + total - (BW_MAGIC_MAX - 1) : base);
        for(size_t local = 0; local < total; local++) {
            uint64_t absolute = base + local;
            if(absolute < processed || absolute >= safe_end) continue;
            for(size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p++) {
                const BwPattern* pattern = &patterns[p];
                if(local + pattern->length > total || absolute < pattern->magic_offset ||
                   memcmp(buffer + local, pattern->magic, pattern->length)) continue;
                uint64_t detection_offset = absolute - pattern->magic_offset;
                if(detection_exists(result, pattern->type, detection_offset)) continue;
                BwDetection detection;
                if(validate_detection(
                       reader,
                       pattern->type,
                       detection_offset,
                       pattern->magic,
                       pattern->length,
                       &detection,
                       cancelled,
                       callback_context)) {
                    result->total_valid++;
                    if(result->count < BW_MAX_DETECTIONS) result->detections[result->count++] = detection;
                    else result->truncated = true;
                }
                if(cancelled && cancelled(callback_context)) {
                    result->cancelled = true;
                    return false;
                }
            }
        }
        processed = safe_end;
        position += actual;
        result->bytes_scanned = position;
        if(progress) progress(callback_context, position, reader->size);
        if(eof) break;
        carry = total < BW_MAGIC_MAX - 1 ? total : BW_MAGIC_MAX - 1;
        memmove(buffer, buffer + total - carry, carry);
    }
    return true;
}

static bool printable(uint8_t c) {
    return c >= 0x20 && c <= 0x7E;
}

bool bw_strings(const BwReader* reader, uint32_t minimum_length, BwStringsResult* result, BwCancelled cancelled, BwProgress progress, void* callback_context) {
    if(!reader || !reader->read_at || !result || minimum_length < 2) return false;
    memset(result, 0, sizeof(*result));
    uint8_t buffer[BW_IO_CHUNK];
    char text[BW_STRING_TEXT_MAX];
    uint64_t run_offset = 0, position = 0;
    uint32_t run_length = 0, stored_length = 0;
    while(position < reader->size) {
        if(cancelled && cancelled(callback_context)) { result->cancelled = true; return false; }
        size_t wanted = reader->size - position > sizeof(buffer) ? sizeof(buffer) : (size_t)(reader->size - position), actual = 0;
        if(!reader->read_at(reader->context, position, buffer, wanted, &actual) || actual != wanted) { result->io_error = true; return false; }
        for(size_t i = 0; i < actual; i++) {
            if(printable(buffer[i])) {
                if(!run_length) run_offset = position + i;
                if(stored_length < sizeof(text) - 1) text[stored_length++] = (char)buffer[i];
                if(run_length < UINT32_MAX) run_length++;
            } else {
                if(run_length >= minimum_length) {
                    result->total++;
                    if(result->count < BW_MAX_STRINGS) {
                        BwString* s = &result->strings[result->count++];
                        s->offset = run_offset; s->length = run_length; memcpy(s->text, text, stored_length); s->text[stored_length] = 0;
                    } else result->truncated = true;
                }
                run_length = stored_length = 0;
            }
        }
        position += actual; result->bytes_scanned = position;
        if(progress) progress(callback_context, position, reader->size);
    }
    if(run_length >= minimum_length) {
        result->total++;
        if(result->count < BW_MAX_STRINGS) {
            BwString* s = &result->strings[result->count++];
            s->offset = run_offset; s->length = run_length; memcpy(s->text, text, stored_length); s->text[stored_length] = 0;
        } else result->truncated = true;
    }
    return true;
}

static double entropy_counts(const uint64_t counts[256], uint64_t total) {
    if(!total) return 0.0;
    double entropy = 0.0;
    for(size_t i = 0; i < 256; i++) if(counts[i]) { double p = (double)counts[i] / (double)total; entropy -= p * (log(p) / log(2.0)); }
    return entropy;
}

bool bw_entropy_block(const BwReader* reader, uint64_t offset, uint32_t block_size, double* entropy) {
    if(!reader || !reader->read_at || !entropy || !block_size || offset >= reader->size) return false;
    uint8_t buffer[BW_IO_CHUNK]; uint64_t counts[256] = {0};
    uint64_t remaining = reader->size - offset; if(remaining > block_size) remaining = block_size; uint64_t total = remaining;
    while(remaining) { size_t part = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining; if(!read_exact(reader, offset, buffer, part)) return false; for(size_t i=0;i<part;i++) counts[buffer[i]]++; offset += part; remaining -= part; }
    *entropy = entropy_counts(counts, total); return true;
}

bool bw_entropy(const BwReader* reader, uint32_t block_size, BwEntropyResult* result, BwCancelled cancelled, BwProgress progress, void* callback_context) {
    if(!reader || !reader->read_at || !result || block_size < 64) return false;
    memset(result, 0, sizeof(*result)); result->block_size = block_size;
    uint8_t buffer[BW_IO_CHUNK]; uint64_t whole_counts[256] = {0}, block_counts[256] = {0};
    uint64_t position = 0, in_block = 0, block_offset = 0; double block_sum = 0.0; result->block_min = 8.0;
    while(position < reader->size) {
        if(cancelled && cancelled(callback_context)) { result->cancelled = true; return false; }
        size_t wanted = reader->size - position > sizeof(buffer) ? sizeof(buffer) : (size_t)(reader->size - position), actual = 0;
        if(!reader->read_at(reader->context, position, buffer, wanted, &actual) || actual != wanted) { result->io_error = true; return false; }
        for(size_t i=0;i<actual;i++) {
            whole_counts[buffer[i]]++; block_counts[buffer[i]]++; in_block++;
            if(in_block == block_size || position + i + 1 == reader->size) {
                double value = entropy_counts(block_counts, in_block);
                if(result->retained_blocks < BW_MAX_ENTROPY_BLOCKS) {
                    result->block_offsets[result->retained_blocks] = block_offset;
                    result->block_values[result->retained_blocks] = value;
                    result->retained_blocks++;
                } else {
                    result->blocks_truncated = true;
                }
                if(value < result->block_min) { result->block_min = value; result->block_min_offset = block_offset; }
                if(value > result->block_max) { result->block_max = value; result->block_max_offset = block_offset; }
                block_sum += value; result->block_count++; memset(block_counts, 0, sizeof(block_counts)); in_block = 0; block_offset = position + i + 1;
            }
        }
        position += actual; result->bytes_scanned = position;
        if(progress) progress(callback_context, position, reader->size);
    }
    result->whole = entropy_counts(whole_counts, reader->size);
    if(result->block_count) result->block_average = block_sum / result->block_count; else result->block_min = 0.0;
    return true;
}

bool bw_search(const BwReader* reader, const uint8_t* needle, size_t needle_length, BwSearchResult* result, BwCancelled cancelled, BwProgress progress, void* callback_context) {
    if(!reader || !reader->read_at || !needle || !needle_length || needle_length > 64 || !result) return false;
    memset(result, 0, sizeof(*result));
    uint8_t buffer[BW_IO_CHUNK + 63]; size_t carry = 0; uint64_t position = 0, processed = 0;
    while(position < reader->size) {
        if(cancelled && cancelled(callback_context)) { result->cancelled = true; return false; }
        size_t wanted = reader->size - position > BW_IO_CHUNK ? BW_IO_CHUNK : (size_t)(reader->size - position), actual = 0;
        if(!reader->read_at(reader->context, position, buffer + carry, wanted, &actual) || actual != wanted) { result->io_error = true; return false; }
        size_t total = carry + actual; uint64_t base = position - carry; bool eof = position + actual >= reader->size;
        uint64_t safe_end = eof ? base + total : (total >= needle_length - 1 ? base + total - (needle_length - 1) : base);
        for(size_t i=0;i+needle_length<=total;i++) { uint64_t absolute=base+i; if(absolute<processed || absolute>=safe_end) continue; if(!memcmp(buffer+i,needle,needle_length)) { result->total++; if(result->count<BW_MAX_SEARCH_HITS) result->offsets[result->count++]=absolute; else result->truncated=true; } }
        processed=safe_end; position+=actual; result->bytes_scanned=position; if(progress) progress(callback_context,position,reader->size); if(eof) break;
        carry = total < needle_length - 1 ? total : needle_length - 1; memmove(buffer,buffer+total-carry,carry);
    }
    return true;
}
