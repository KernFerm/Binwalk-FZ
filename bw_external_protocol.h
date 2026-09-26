/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BW_EXTERNAL_PROTOCOL_VERSION 1U
#define BW_EXTERNAL_LINE_MAX 256U

typedef enum {
    BwExternalMessageNone,
    BwExternalMessageInfo,
    BwExternalMessageStatus,
    BwExternalMessageError,
} BwExternalMessageType;

typedef struct {
    BwExternalMessageType type;
    uint32_t protocol_version;
    uint32_t file_count;
    uint32_t file_index;
    uint64_t file_size;
    uint32_t detections;
    uint64_t first_offset;
    uint64_t first_size;
    uint32_t confidence;
    char bridge_version[24];
    char binwalk_version[32];
    char file_name[64];
    char state[16];
    char result_name[32];
    char description[80];
    char error[64];
} BwExternalMessage;

typedef struct {
    char line[BW_EXTERNAL_LINE_MAX];
    size_t length;
    bool overflow;
} BwExternalDecoder;

typedef void (*BwExternalMessageCallback)(const BwExternalMessage* message, void* context);

void bw_external_decoder_reset(BwExternalDecoder* decoder);
void bw_external_decoder_feed(
    BwExternalDecoder* decoder,
    const uint8_t* data,
    size_t length,
    BwExternalMessageCallback callback,
    void* context);
bool bw_external_parse_line(const char* line, BwExternalMessage* message);
