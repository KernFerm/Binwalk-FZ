/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BW_IO_CHUNK 512U
#define BW_MAX_DETECTIONS 64U
#define BW_MAX_STRINGS 64U
#define BW_MAX_SEARCH_HITS 64U
#define BW_MAX_ENTROPY_BLOCKS 64U
#define BW_MAGIC_MAX 16U
#define BW_META_MAX 96U
#define BW_STRING_TEXT_MAX 48U

typedef bool (*BwReadAt)(void* context, uint64_t offset, uint8_t* data, size_t length, size_t* actual);
typedef bool (*BwCancelled)(void* context);
typedef void (*BwProgress)(void* context, uint64_t completed, uint64_t total);

typedef struct {
    void* context;
    uint64_t size;
    BwReadAt read_at;
} BwReader;

typedef enum {
    BwTypeElf,
    BwTypePe,
    BwTypeZip,
    BwTypeGzip,
    BwType7zip,
    BwTypeRar,
    BwTypePng,
    BwTypeJpeg,
    BwTypeGif,
    BwTypePdf,
    BwTypeSqlite,
    BwTypeWav,
    BwTypeBmp,
    BwTypeBzip2,
    BwTypeXz,
    BwTypeZstd,
    BwTypeFlac,
    BwTypeTar,
} BwType;

typedef struct {
    uint64_t offset;
    uint64_t size;
    BwType type;
    uint8_t magic[BW_MAGIC_MAX];
    uint8_t magic_length;
    char metadata[BW_META_MAX];
} BwDetection;

typedef struct {
    BwDetection detections[BW_MAX_DETECTIONS];
    uint32_t count;
    uint32_t total_valid;
    uint64_t bytes_scanned;
    bool truncated;
    bool cancelled;
    bool io_error;
} BwScanResult;

typedef struct {
    uint64_t offset;
    uint32_t length;
    char text[BW_STRING_TEXT_MAX];
} BwString;

typedef struct {
    BwString strings[BW_MAX_STRINGS];
    uint32_t count;
    uint32_t total;
    uint64_t bytes_scanned;
    bool truncated;
    bool cancelled;
    bool io_error;
} BwStringsResult;

typedef struct {
    double whole;
    double block_min;
    double block_max;
    double block_average;
    uint64_t block_min_offset;
    uint64_t block_max_offset;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t retained_blocks;
    uint64_t block_offsets[BW_MAX_ENTROPY_BLOCKS];
    double block_values[BW_MAX_ENTROPY_BLOCKS];
    bool blocks_truncated;
    uint64_t bytes_scanned;
    bool cancelled;
    bool io_error;
} BwEntropyResult;

typedef struct {
    uint64_t offsets[BW_MAX_SEARCH_HITS];
    uint32_t count;
    uint32_t total;
    uint64_t bytes_scanned;
    bool truncated;
    bool cancelled;
    bool io_error;
} BwSearchResult;

const char* bw_type_name(BwType type);
const char* bw_type_description(BwType type);
uint32_t bw_signature_count(void);

bool bw_scan(
    const BwReader* reader,
    BwScanResult* result,
    BwCancelled cancelled,
    BwProgress progress,
    void* callback_context);

bool bw_strings(
    const BwReader* reader,
    uint32_t minimum_length,
    BwStringsResult* result,
    BwCancelled cancelled,
    BwProgress progress,
    void* callback_context);

bool bw_entropy(
    const BwReader* reader,
    uint32_t block_size,
    BwEntropyResult* result,
    BwCancelled cancelled,
    BwProgress progress,
    void* callback_context);

bool bw_entropy_block(const BwReader* reader, uint64_t offset, uint32_t block_size, double* entropy);

bool bw_search(
    const BwReader* reader,
    const uint8_t* needle,
    size_t needle_length,
    BwSearchResult* result,
    BwCancelled cancelled,
    BwProgress progress,
    void* callback_context);
