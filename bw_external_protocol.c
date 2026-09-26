/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bw_external_protocol.h"

#include <limits.h>
#include <string.h>

static bool bw_parse_u64(const char* text, uint64_t* value) {
    if(!text || !*text) return false;
    uint64_t result = 0;
    while(*text) {
        if(*text < '0' || *text > '9') return false;
        uint8_t digit = (uint8_t)(*text - '0');
        if(result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
        text++;
    }
    *value = result;
    return true;
}

static bool bw_parse_u32(const char* text, uint32_t* value) {
    uint64_t parsed = 0;
    if(!bw_parse_u64(text, &parsed) || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool bw_copy_token(char* output, size_t output_size, const char* token) {
    size_t length = strlen(token);
    if(!length || length >= output_size) return false;
    memcpy(output, token, length + 1U);
    return true;
}

static size_t bw_tokenize(char* line, char** tokens, size_t capacity) {
    size_t count = 0;
    char* position = line;
    while(*position && count < capacity) {
        while(*position == ' ') position++;
        if(!*position) break;
        tokens[count++] = position;
        while(*position && *position != ' ') position++;
        if(*position) *position++ = 0;
    }
    return count;
}

bool bw_external_parse_line(const char* line, BwExternalMessage* message) {
    if(!line || !message) return false;
    size_t length = strlen(line);
    if(!length || length >= BW_EXTERNAL_LINE_MAX) return false;
    char copy[BW_EXTERNAL_LINE_MAX];
    memcpy(copy, line, length + 1U);
    char* tokens[13];
    size_t count = bw_tokenize(copy, tokens, sizeof(tokens) / sizeof(tokens[0]));
    if(count < 3 || strcmp(tokens[0], "BWF1")) return false;
    memset(message, 0, sizeof(*message));

    if(!strcmp(tokens[1], "INFO")) {
        if(count != 9 || !bw_parse_u32(tokens[2], &message->protocol_version) ||
           !bw_copy_token(message->bridge_version, sizeof(message->bridge_version), tokens[3]) ||
           !bw_copy_token(message->binwalk_version, sizeof(message->binwalk_version), tokens[4]) ||
           !bw_parse_u32(tokens[5], &message->file_count) ||
           !bw_parse_u32(tokens[6], &message->file_index) ||
           !bw_parse_u64(tokens[7], &message->file_size) ||
           !bw_copy_token(message->file_name, sizeof(message->file_name), tokens[8]))
            return false;
        if(message->file_count && message->file_index >= message->file_count) return false;
        message->type = BwExternalMessageInfo;
        return true;
    }

    if(!strcmp(tokens[1], "STATUS")) {
        if(count != 10 || !bw_copy_token(message->state, sizeof(message->state), tokens[2]) ||
           !bw_parse_u32(tokens[3], &message->detections) ||
           !bw_parse_u64(tokens[4], &message->first_offset) ||
           !bw_parse_u64(tokens[5], &message->first_size) ||
           !bw_parse_u32(tokens[6], &message->confidence) ||
           message->confidence > 255U ||
           !bw_copy_token(message->result_name, sizeof(message->result_name), tokens[7]) ||
           !bw_copy_token(message->description, sizeof(message->description), tokens[8]) ||
           strcmp(tokens[9], "END"))
            return false;
        message->type = BwExternalMessageStatus;
        return true;
    }

    if(!strcmp(tokens[1], "ERROR")) {
        if(count != 3 || !bw_copy_token(message->error, sizeof(message->error), tokens[2]))
            return false;
        message->type = BwExternalMessageError;
        return true;
    }
    return false;
}

void bw_external_decoder_reset(BwExternalDecoder* decoder) {
    if(decoder) memset(decoder, 0, sizeof(*decoder));
}

void bw_external_decoder_feed(
    BwExternalDecoder* decoder,
    const uint8_t* data,
    size_t length,
    BwExternalMessageCallback callback,
    void* context) {
    if(!decoder || (!data && length)) return;
    for(size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        if(byte == '\n') {
            if(!decoder->overflow && decoder->length) {
                if(decoder->line[decoder->length - 1U] == '\r') decoder->length--;
                decoder->line[decoder->length] = 0;
                BwExternalMessage message;
                if(bw_external_parse_line(decoder->line, &message) && callback)
                    callback(&message, context);
            }
            decoder->length = 0;
            decoder->overflow = false;
        } else if(!decoder->overflow) {
            if(decoder->length + 1U < sizeof(decoder->line))
                decoder->line[decoder->length++] = (char)byte;
            else
                decoder->overflow = true;
        }
    }
}
