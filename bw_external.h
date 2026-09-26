/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct BwExternal BwExternal;

typedef struct {
    bool active;
    bool connected;
    bool running;
    uint32_t protocol_version;
    uint32_t file_count;
    uint32_t file_index;
    uint32_t detections;
    uint32_t confidence;
    uint32_t serial_errors;
    uint64_t file_size;
    uint64_t first_offset;
    uint64_t first_size;
    char bridge_version[24];
    char binwalk_version[32];
    char file_name[64];
    char state[16];
    char result_name[32];
    char description[80];
    char error[64];
} BwExternalSnapshot;

BwExternal* bw_external_alloc(void);
void bw_external_free(BwExternal* external);
bool bw_external_start(BwExternal* external, uint32_t baudrate);
void bw_external_stop(BwExternal* external);
bool bw_external_scan(BwExternal* external);
bool bw_external_next(BwExternal* external);
bool bw_external_previous(BwExternal* external);
bool bw_external_cancel(BwExternal* external);
void bw_external_snapshot(BwExternal* external, BwExternalSnapshot* snapshot);
