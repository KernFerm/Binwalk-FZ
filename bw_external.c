/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bw_external.h"
#include "bw_external_protocol.h"

#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BwExternalFlagStop = 1U << 0,
    BwExternalFlagRx = 1U << 1,
    BwExternalFlagError = 1U << 2,
};

struct BwExternal {
    FuriMutex* data_mutex;
    FuriMutex* tx_mutex;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
    bool worker_started;
    FuriHalSerialHandle* serial;
    Expansion* expansion;
    BwExternalDecoder decoder;
    BwExternalSnapshot snapshot;
};

static void bw_external_set_error(BwExternal* external, const char* text) {
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    snprintf(external->snapshot.error, sizeof(external->snapshot.error), "%s", text);
    furi_mutex_release(external->data_mutex);
}

static bool bw_external_send(BwExternal* external, const char* command) {
    if(!external->serial) return false;
    furi_mutex_acquire(external->tx_mutex, FuriWaitForever);
    furi_hal_serial_tx(external->serial, (const uint8_t*)command, strlen(command));
    furi_hal_serial_tx_wait_complete(external->serial);
    furi_mutex_release(external->tx_mutex);
    return true;
}

static void bw_external_message(const BwExternalMessage* message, void* context) {
    BwExternal* external = context;
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    BwExternalSnapshot* snapshot = &external->snapshot;
    if(message->type == BwExternalMessageInfo) {
        snapshot->protocol_version = message->protocol_version;
        if(message->protocol_version == BW_EXTERNAL_PROTOCOL_VERSION) {
            snapshot->connected = true;
            snapshot->error[0] = 0;
            snapshot->file_count = message->file_count;
            snapshot->file_index = message->file_index;
            snapshot->file_size = message->file_size;
            snprintf(snapshot->bridge_version, sizeof(snapshot->bridge_version), "%s", message->bridge_version);
            snprintf(snapshot->binwalk_version, sizeof(snapshot->binwalk_version), "%s", message->binwalk_version);
            snprintf(snapshot->file_name, sizeof(snapshot->file_name), "%s", message->file_name);
        } else {
            snapshot->connected = false;
            snprintf(snapshot->error, sizeof(snapshot->error), "Protocol mismatch: Pi=%" PRIu32, message->protocol_version);
        }
    } else if(message->type == BwExternalMessageStatus) {
        snprintf(snapshot->state, sizeof(snapshot->state), "%s", message->state);
        snapshot->running = !strcmp(message->state, "SCANNING") ||
                            !strcmp(message->state, "STARTING");
        snapshot->detections = message->detections;
        snapshot->first_offset = message->first_offset;
        snapshot->first_size = message->first_size;
        snapshot->confidence = message->confidence;
        snprintf(snapshot->result_name, sizeof(snapshot->result_name), "%s", message->result_name);
        snprintf(snapshot->description, sizeof(snapshot->description), "%s", message->description);
    } else if(message->type == BwExternalMessageError) {
        snprintf(snapshot->error, sizeof(snapshot->error), "%s", message->error);
        snapshot->running = false;
    }
    furi_mutex_release(external->data_mutex);
}

static void bw_external_irq(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    BwExternal* external = context;
    uint32_t flags = 0;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(external->rx_stream, &byte, 1, 0) == 1)
            flags |= BwExternalFlagRx;
        else
            flags |= BwExternalFlagError;
    }
    if(event &
       (FuriHalSerialRxEventFrameError | FuriHalSerialRxEventNoiseError |
        FuriHalSerialRxEventOverrunError | FuriHalSerialRxEventParityError))
        flags |= BwExternalFlagError;
    if(flags && external->worker)
        furi_thread_flags_set(furi_thread_get_id(external->worker), flags);
}

static int32_t bw_external_worker(void* context) {
    BwExternal* external = context;
    while(true) {
        uint32_t flags = furi_thread_flags_wait(
            BwExternalFlagStop | BwExternalFlagRx | BwExternalFlagError,
            FuriFlagWaitAny,
            1000);
        if(flags & FuriFlagError) {
            if(flags == (uint32_t)FuriFlagErrorTimeout)
                bw_external_send(external, "BWF1 STATUS\n");
            else
                bw_external_set_error(external, "UART worker failure");
            continue;
        }
        if(flags & BwExternalFlagStop) break;
        if(flags & BwExternalFlagError) {
            furi_mutex_acquire(external->data_mutex, FuriWaitForever);
            external->snapshot.serial_errors++;
            snprintf(
                external->snapshot.error,
                sizeof(external->snapshot.error),
                "UART receive error (%" PRIu32 ")",
                external->snapshot.serial_errors);
            furi_mutex_release(external->data_mutex);
        }
        if(flags & BwExternalFlagRx) {
            uint8_t buffer[64];
            size_t received;
            do {
                received = furi_stream_buffer_receive(external->rx_stream, buffer, sizeof(buffer), 0);
                if(received)
                    bw_external_decoder_feed(
                        &external->decoder,
                        buffer,
                        received,
                        bw_external_message,
                        external);
            } while(received);
        }
    }
    return 0;
}

BwExternal* bw_external_alloc(void) {
    BwExternal* external = calloc(1, sizeof(BwExternal));
    if(!external) return NULL;
    external->data_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    external->tx_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!external->data_mutex || !external->tx_mutex) {
        if(external->tx_mutex) furi_mutex_free(external->tx_mutex);
        if(external->data_mutex) furi_mutex_free(external->data_mutex);
        free(external);
        return NULL;
    }
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "DISCONNECTED");
    return external;
}

bool bw_external_start(BwExternal* external, uint32_t baudrate) {
    if(!external || external->serial || baudrate < 9600U) return false;
    memset(&external->snapshot, 0, sizeof(external->snapshot));
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "CONNECTING");
    external->snapshot.active = true;
    bw_external_decoder_reset(&external->decoder);
    external->rx_stream = furi_stream_buffer_alloc(1024, 1);
    external->worker = furi_thread_alloc_ex("BwExternal", 2048, bw_external_worker, external);
    if(!external->rx_stream || !external->worker) {
        bw_external_set_error(external, "Allocation failed");
        bw_external_stop(external);
        return false;
    }
    external->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(external->expansion);
    external->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!external->serial) {
        bw_external_set_error(external, "USART busy");
        bw_external_stop(external);
        return false;
    }
    furi_thread_start(external->worker);
    external->worker_started = true;
    furi_hal_serial_init(external->serial, baudrate);
    furi_hal_serial_async_rx_start(external->serial, bw_external_irq, external, true);
    bw_external_send(external, "BWF1 HELLO\n");
    bw_external_send(external, "BWF1 STATUS\n");
    return true;
}

void bw_external_stop(BwExternal* external) {
    if(!external) return;
    if(external->serial) {
        bw_external_send(external, "BWF1 STOP\n");
        furi_hal_serial_async_rx_stop(external->serial);
    }
    if(external->worker) {
        if(external->worker_started) {
            furi_thread_flags_set(furi_thread_get_id(external->worker), BwExternalFlagStop);
            furi_thread_join(external->worker);
        }
        furi_thread_free(external->worker);
        external->worker = NULL;
        external->worker_started = false;
    }
    if(external->serial) {
        furi_hal_serial_deinit(external->serial);
        furi_hal_serial_control_release(external->serial);
        external->serial = NULL;
    }
    if(external->expansion) {
        expansion_enable(external->expansion);
        furi_record_close(RECORD_EXPANSION);
        external->expansion = NULL;
    }
    if(external->rx_stream) {
        furi_stream_buffer_free(external->rx_stream);
        external->rx_stream = NULL;
    }
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    external->snapshot.active = false;
    external->snapshot.connected = false;
    external->snapshot.running = false;
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "DISCONNECTED");
    furi_mutex_release(external->data_mutex);
}

static bool bw_external_command(BwExternal* external, const char* command) {
    if(!external) return false;
    BwExternalSnapshot snapshot;
    bw_external_snapshot(external, &snapshot);
    return snapshot.connected && bw_external_send(external, command);
}

bool bw_external_scan(BwExternal* external) {
    if(!bw_external_command(external, "BWF1 SCAN\n")) return false;
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    external->snapshot.running = true;
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "STARTING");
    furi_mutex_release(external->data_mutex);
    return true;
}

bool bw_external_next(BwExternal* external) {
    return bw_external_command(external, "BWF1 NEXT\n");
}

bool bw_external_previous(BwExternal* external) {
    return bw_external_command(external, "BWF1 PREV\n");
}

bool bw_external_cancel(BwExternal* external) {
    return bw_external_command(external, "BWF1 STOP\n");
}

void bw_external_snapshot(BwExternal* external, BwExternalSnapshot* snapshot) {
    if(!external || !snapshot) return;
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    *snapshot = external->snapshot;
    furi_mutex_release(external->data_mutex);
}

void bw_external_free(BwExternal* external) {
    if(!external) return;
    bw_external_stop(external);
    furi_mutex_free(external->tx_mutex);
    furi_mutex_free(external->data_mutex);
    free(external);
}
