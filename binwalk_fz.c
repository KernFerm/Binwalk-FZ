/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bw_engine.h"
#include "bw_external.h"

#include <furi.h>
#include <furi/core/memmgr.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/file_browser.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BW_DATA_DIR APP_DATA_PATH("")
#define BW_REPORT_PATH APP_DATA_PATH("report.txt")
#define BW_HEX_BYTES 24U
#define BW_APP_VERSION "1.0.0"

typedef enum {
    BwViewMain,
    BwViewBrowser,
    BwViewText,
    BwViewResults,
    BwViewSettings,
    BwViewInput,
    BwViewHex,
    BwViewExternal,
} BwViewId;

typedef enum {
    BwMenuSelect,
    BwMenuExternal,
    BwMenuScan,
    BwMenuResults,
    BwMenuHex,
    BwMenuStrings,
    BwMenuEntropy,
    BwMenuSearch,
    BwMenuReports,
    BwMenuSettings,
} BwMenuItem;

typedef enum {
    BwSettingStringMinimum,
    BwSettingEntropyBlock,
    BwSettingExternalBaud,
    BwSettingVersion,
    BwSettingProtocol,
    BwSettingAbout,
} BwSettingItem;

typedef enum {BwTaskNone, BwTaskScan, BwTaskStrings, BwTaskEntropy, BwTaskSearch} BwTask;
typedef enum {BwInputSearch, BwInputGoto} BwInputPurpose;
typedef enum {BwStatusIdle, BwStatusOk, BwStatusCancelled, BwStatusIo, BwStatusTooLarge} BwStatus;

typedef struct BwApp BwApp;

typedef struct {
    BwApp* app;
    uint32_t revision;
} BwHexModel;

struct BwApp {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* dispatcher;
    Submenu* main_menu;
    Submenu* results_menu;
    FileBrowser* browser;
    Widget* widget;
    VariableItemList* settings;
    TextInput* input;
    View* hex_view;
    View* external_view;
    FuriString* path;
    FuriString* text;
    FuriThread* worker;
    volatile bool cancel;
    volatile uint64_t progress_done;
    volatile uint64_t progress_total;
    BwTask task;
    BwStatus status;
    BwViewId current_view;
    BwViewId text_return;
    BwInputPurpose input_purpose;
    BwScanResult scan;
    BwStringsResult strings;
    BwEntropyResult entropy;
    BwSearchResult search;
    uint32_t selected_detection;
    uint32_t minimum_string;
    uint32_t entropy_block;
    uint8_t minimum_index;
    uint8_t block_index;
    char search_query[65];
    char goto_buffer[17];
    uint64_t hex_offset;
    uint8_t hex_data[BW_HEX_BYTES];
    size_t hex_length;
    size_t heap_before;
    size_t heap_after;
    size_t heap_minimum;
    uint32_t worker_stack_free;
    BwExternal* external;
    uint32_t external_baud;
    uint8_t external_baud_index;
    bool views_added;
};

typedef struct {
    File* file;
    uint64_t size;
} BwStorageReader;

typedef struct {
    BwApp* app;
    uint32_t revision;
} BwExternalModel;

static const uint32_t bw_external_bauds[] = {115200U, 230400U, 460800U};
static const char* const bw_external_baud_names[] = {"115200", "230400", "460800"};

static void bw_switch(BwApp* app, BwViewId view) {
    app->current_view = view;
    view_dispatcher_switch_to_view(app->dispatcher, view);
}

static void bw_set_text(BwApp* app, const char* title, const char* body, BwViewId return_view) {
    widget_reset(app->widget);
    furi_string_printf(app->text, "\e#%s\n%s", title, body);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->text));
    app->text_return = return_view;
    bw_switch(app, BwViewText);
}

static bool bw_file_read_at(void* context, uint64_t offset, uint8_t* data, size_t length, size_t* actual) {
    BwStorageReader* reader = context;
    *actual = 0;
    if(offset > UINT32_MAX || length > UINT32_MAX || offset > reader->size || length > reader->size - offset) return false;
    if(!storage_file_seek(reader->file, (uint32_t)offset, true)) return false;
    *actual = storage_file_read(reader->file, data, length);
    return *actual == length;
}

static bool bw_worker_cancelled(void* context) {
    return ((BwApp*)context)->cancel;
}

static void bw_worker_progress(void* context, uint64_t completed, uint64_t total) {
    BwApp* app = context;
    app->progress_done = completed;
    app->progress_total = total;
}

static void bw_join_worker(BwApp* app) {
    if(app->worker) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }
}

static int32_t bw_worker(void* context) {
    BwApp* app = context;
    app->heap_before = memmgr_get_free_heap();
    app->status = BwStatusIo;
    File* file = storage_file_alloc(app->storage);
    if(file && storage_file_open(file, furi_string_get_cstr(app->path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        BwStorageReader storage_reader = {.file = file, .size = storage_file_size(file)};
        if(storage_reader.size <= UINT32_MAX) {
            BwReader reader = {.context = &storage_reader, .size = storage_reader.size, .read_at = bw_file_read_at};
            bool ok = false;
            if(app->task == BwTaskScan) {
                ok = bw_scan(&reader, &app->scan, bw_worker_cancelled, bw_worker_progress, app);
            } else if(app->task == BwTaskStrings) {
                ok = bw_strings(&reader, app->minimum_string, &app->strings, bw_worker_cancelled, bw_worker_progress, app);
            } else if(app->task == BwTaskEntropy) {
                ok = bw_entropy(&reader, app->entropy_block, &app->entropy, bw_worker_cancelled, bw_worker_progress, app);
            } else if(app->task == BwTaskSearch) {
                ok = bw_search(&reader, (const uint8_t*)app->search_query, strlen(app->search_query), &app->search, bw_worker_cancelled, bw_worker_progress, app);
            }
            if(!ok) {
                if(app->task == BwTaskScan) memset(&app->scan, 0, sizeof(app->scan));
                else if(app->task == BwTaskStrings) memset(&app->strings, 0, sizeof(app->strings));
                else if(app->task == BwTaskEntropy) memset(&app->entropy, 0, sizeof(app->entropy));
                else if(app->task == BwTaskSearch) memset(&app->search, 0, sizeof(app->search));
            }
            app->status = ok ? BwStatusOk : app->cancel ? BwStatusCancelled : BwStatusIo;
        } else {
            app->status = BwStatusTooLarge;
        }
        storage_file_close(file);
    }
    if(file) storage_file_free(file);
    app->heap_after = memmgr_get_free_heap();
    app->heap_minimum = memmgr_get_minimum_free_heap();
    app->worker_stack_free = furi_thread_get_stack_space(furi_thread_get_current_id());
    view_dispatcher_send_custom_event(app->dispatcher, 1);
    return 0;
}

static bool bw_has_file(BwApp* app) {
    return !furi_string_empty(app->path) && strcmp(furi_string_get_cstr(app->path), "/ext");
}

static void bw_no_file(BwApp* app) {
    bw_set_text(app, "No file selected", "Choose Select File first.", BwViewMain);
}

static void bw_start_task(BwApp* app, BwTask task) {
    if(!bw_has_file(app)) { bw_no_file(app); return; }
    bw_join_worker(app);
    app->task = task;
    app->cancel = false;
    app->progress_done = 0;
    app->progress_total = 0;
    app->status = BwStatusIdle;
    if(task == BwTaskScan) memset(&app->scan, 0, sizeof(app->scan));
    else if(task == BwTaskStrings) memset(&app->strings, 0, sizeof(app->strings));
    else if(task == BwTaskEntropy) memset(&app->entropy, 0, sizeof(app->entropy));
    else if(task == BwTaskSearch) memset(&app->search, 0, sizeof(app->search));
    const char* title = task == BwTaskScan ? "Scanning signatures" : task == BwTaskStrings ? "Extracting strings" : task == BwTaskEntropy ? "Calculating entropy" : "Searching";
    char body[128];
    snprintf(body, sizeof(body), "%s\n0%%\n\nBack requests cancellation.", furi_string_get_cstr(app->path));
    bw_set_text(app, title, body, BwViewMain);
    app->worker = furi_thread_alloc_ex("BinwalkWorker", 8192, bw_worker, app);
    if(app->worker) furi_thread_start(app->worker);
    else { app->status = BwStatusIo; bw_set_text(app, "Operation failed", "Could not allocate worker thread.", BwViewMain); }
}

static void bw_show_detection(BwApp* app, uint32_t index) {
    if(index >= app->scan.count) { bw_set_text(app, "Detection Details", "No validated detection selected.", BwViewResults); return; }
    app->selected_detection = index;
    const BwDetection* d = &app->scan.detections[index];
    char magic[3 * BW_MAGIC_MAX + 1] = "";
    size_t used = 0;
    for(uint8_t i = 0; i < d->magic_length && used + 4 < sizeof(magic); i++) used += snprintf(magic + used, sizeof(magic) - used, "%02X%s", (unsigned)d->magic[i], i + 1 == d->magic_length ? "" : " ");
    char body[384];
    snprintf(body, sizeof(body), "Type: %s\nOffset: 0x%llX (%llu)\nSize: %s%llu\nSignature: %s\n\n%s\n%s", bw_type_name(d->type), (unsigned long long)d->offset, (unsigned long long)d->offset, d->size ? "" : "unknown / ", (unsigned long long)d->size, magic, bw_type_description(d->type), d->metadata);
    bw_set_text(app, "Detection Details", body, BwViewResults);
}

static void bw_results_selected(void* context, uint32_t index) {
    bw_show_detection(context, index);
}

static void bw_populate_results_callbacks(BwApp* app) {
    submenu_reset(app->results_menu);
    submenu_set_header(app->results_menu, "Validated detections");
    for(uint32_t i = 0; i < app->scan.count; i++) {
        char label[64];
        snprintf(label, sizeof(label), "%08lX %s", (unsigned long)app->scan.detections[i].offset, bw_type_name(app->scan.detections[i].type));
        submenu_add_item(app->results_menu, label, i, bw_results_selected, app);
    }
}

static void bw_show_strings(BwApp* app) {
    widget_reset(app->widget);
    furi_string_printf(app->text, "\e#Strings\nFound %lu; retained %lu%s\n", (unsigned long)app->strings.total, (unsigned long)app->strings.count, app->strings.truncated ? " (bounded)" : "");
    for(uint32_t i = 0; i < app->strings.count; i++) {
        const BwString* s = &app->strings.strings[i];
        furi_string_cat_printf(app->text, "\n%08lX [%lu] %s%s", (unsigned long)s->offset, (unsigned long)s->length, s->text, s->length >= BW_STRING_TEXT_MAX ? "..." : "");
    }
    furi_string_cat_printf(app->text, "\n\nHeap %lu -> %lu; stack free %lu", (unsigned long)app->heap_before, (unsigned long)app->heap_after, (unsigned long)app->worker_stack_free);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->text)); app->text_return = BwViewMain; bw_switch(app, BwViewText);
}

static void bw_show_entropy(BwApp* app) {
    widget_reset(app->widget);
    furi_string_printf(app->text, "\e#Entropy\nWhole: %.6f bits/byte\nBlock size: %lu; blocks: %lu\nAverage: %.6f\nMin: %.6f @ 0x%llX\nMax: %.6f @ 0x%llX\n\nBlock entropy%s:", app->entropy.whole, (unsigned long)app->entropy.block_size, (unsigned long)app->entropy.block_count, app->entropy.block_average, app->entropy.block_min, (unsigned long long)app->entropy.block_min_offset, app->entropy.block_max, (unsigned long long)app->entropy.block_max_offset, app->entropy.blocks_truncated ? " (first 64)" : "");
    for(uint32_t i=0;i<app->entropy.retained_blocks;i++) furi_string_cat_printf(app->text,"\n%08lX %.6f",(unsigned long)app->entropy.block_offsets[i],app->entropy.block_values[i]);
    furi_string_cat_printf(app->text,"\n\nHeap %lu -> %lu; stack free %lu",(unsigned long)app->heap_before,(unsigned long)app->heap_after,(unsigned long)app->worker_stack_free);
    widget_add_text_scroll_element(app->widget,0,0,128,64,furi_string_get_cstr(app->text)); app->text_return=BwViewMain; bw_switch(app,BwViewText);
}

static void bw_show_search(BwApp* app) {
    widget_reset(app->widget);
    furi_string_printf(app->text, "\e#Search Results\nQuery: %s\nMatches: %lu; retained %lu%s\n", app->search_query, (unsigned long)app->search.total, (unsigned long)app->search.count, app->search.truncated ? " (bounded)" : "");
    for(uint32_t i=0;i<app->search.count;i++) furi_string_cat_printf(app->text, "\n0x%08lX", (unsigned long)app->search.offsets[i]);
    widget_add_text_scroll_element(app->widget,0,0,128,64,furi_string_get_cstr(app->text)); app->text_return=BwViewMain; bw_switch(app,BwViewText);
}

static void bw_show_worker_result(BwApp* app) {
    if(app->status == BwStatusCancelled) { bw_set_text(app, "Cancelled", "Input closed. No partial result was accepted.", BwViewMain); return; }
    if(app->status == BwStatusTooLarge) { bw_set_text(app, "Input rejected", "Files above 4 GiB exceed the official storage seek API.", BwViewMain); return; }
    if(app->status != BwStatusOk) { bw_set_text(app, "Analysis failed", "Read error or SD card unavailable. No result was produced.", BwViewMain); return; }
    if(app->task == BwTaskScan) {
        bw_populate_results_callbacks(app);
        if(app->scan.count) bw_switch(app, BwViewResults);
        else bw_set_text(app, "Results", "No validated signatures found.\nNo detections were inferred or fabricated.", BwViewMain);
    } else if(app->task == BwTaskStrings) bw_show_strings(app);
    else if(app->task == BwTaskEntropy) bw_show_entropy(app);
    else if(app->task == BwTaskSearch) bw_show_search(app);
}

static bool bw_read_hex(BwApp* app) {
    app->hex_length = 0;
    if(!bw_has_file(app)) return false;
    File* file = storage_file_alloc(app->storage); if(!file) return false;
    bool ok = storage_file_open(file, furi_string_get_cstr(app->path), FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) {
        uint64_t size = storage_file_size(file);
        if(size && app->hex_offset >= size) app->hex_offset = size > BW_HEX_BYTES ? size - BW_HEX_BYTES : 0;
        if(app->hex_offset <= UINT32_MAX && storage_file_seek(file,(uint32_t)app->hex_offset,true)) app->hex_length=storage_file_read(file,app->hex_data,sizeof(app->hex_data)); else ok=false;
        storage_file_close(file);
    }
    storage_file_free(file); return ok;
}

static void bw_hex_draw(Canvas* canvas, void* model_context) {
    const BwHexModel* model = model_context; const BwApp* app = model->app; char line[64];
    canvas_set_font(canvas, FontPrimary); snprintf(line,sizeof(line),"HEX @ %08lX",(unsigned long)app->hex_offset); canvas_draw_str(canvas,1,9,line);
    canvas_set_font(canvas, FontKeyboard);
    for(size_t row=0;row<6;row++) {
        size_t start=row*4; if(start>=app->hex_length) break; char ascii[5]="....";
        snprintf(line,sizeof(line),"%08lX",(unsigned long)(app->hex_offset+start)); canvas_draw_str(canvas,0,18+(int)row*8,line);
        for(size_t column=0;column<4;column++) { size_t i=start+column; if(i>=app->hex_length) break; snprintf(line,sizeof(line),"%02X",app->hex_data[i]); canvas_draw_str(canvas,39+(int)column*13,18+(int)row*8,line); ascii[column]=(app->hex_data[i]>=32&&app->hex_data[i]<=126)?(char)app->hex_data[i]:'.'; }
        ascii[(app->hex_length-start)<4?(app->hex_length-start):4]=0; canvas_draw_str(canvas,94,18+(int)row*8,ascii);
    }
}

static uint64_t bw_parse_hex(const char* text) {
    uint64_t value=0; while(*text==' '||*text=='\t') text++; if(text[0]=='0'&&(text[1]=='x'||text[1]=='X')) text+=2;
    while(*text) { uint8_t digit; if(*text>='0'&&*text<='9') digit=(uint8_t)(*text-'0'); else if(*text>='a'&&*text<='f') digit=(uint8_t)(*text-'a'+10); else if(*text>='A'&&*text<='F') digit=(uint8_t)(*text-'A'+10); else break; if(value>(UINT64_MAX-digit)/16) return UINT64_MAX; value=value*16+digit; text++; } return value;
}

static void bw_input_done(void* context) {
    BwApp* app=context;
    if(app->input_purpose==BwInputSearch) bw_start_task(app,BwTaskSearch);
    else { app->hex_offset=bw_parse_hex(app->goto_buffer); bw_read_hex(app); BwHexModel* model=view_get_model(app->hex_view); model->revision++; view_commit_model(app->hex_view,true); bw_switch(app,BwViewHex); }
}

static void bw_open_input(BwApp* app, BwInputPurpose purpose) {
    app->input_purpose=purpose;
    char* buffer=purpose==BwInputSearch?app->search_query:app->goto_buffer;
    size_t buffer_size=purpose==BwInputSearch?sizeof(app->search_query):sizeof(app->goto_buffer);
    buffer[0]=0; text_input_reset(app->input); text_input_set_header_text(app->input,purpose==BwInputSearch?"ASCII search":"Goto hex offset"); text_input_set_minimum_length(app->input,1); text_input_set_result_callback(app->input,bw_input_done,app,buffer,buffer_size,true); bw_switch(app,BwViewInput);
}

static bool bw_hex_input(InputEvent* event, void* context) {
    BwApp* app=context; if(event->type!=InputTypeShort&&event->type!=InputTypeRepeat) return false;
    if(event->key==InputKeyUp) app->hex_offset=app->hex_offset>=BW_HEX_BYTES?app->hex_offset-BW_HEX_BYTES:0;
    else if(event->key==InputKeyDown) app->hex_offset+=BW_HEX_BYTES;
    else if(event->key==InputKeyLeft) app->hex_offset=app->hex_offset>=4?app->hex_offset-4:0;
    else if(event->key==InputKeyRight) app->hex_offset+=4;
    else if(event->key==InputKeyOk) { bw_open_input(app,BwInputGoto); return true; }
    else return false;
    bw_read_hex(app); BwHexModel* model=view_get_model(app->hex_view); model->revision++; view_commit_model(app->hex_view,true); return true;
}

static void bw_open_hex(BwApp* app) {
    if(!bw_has_file(app)) { bw_no_file(app); return; } app->hex_offset=0; if(!bw_read_hex(app)) { bw_set_text(app,"HEX Viewer","Unable to read selected file.",BwViewMain); return; } bw_switch(app,BwViewHex);
}

static void bw_browser_selected(void* context) {
    BwApp* app=context; file_browser_stop(app->browser);
    File* file=storage_file_alloc(app->storage); uint64_t size=0; bool ok=file&&storage_file_open(file,furi_string_get_cstr(app->path),FSAM_READ,FSOM_OPEN_EXISTING);
    if(ok) { size=storage_file_size(file); storage_file_close(file); }
    if(file) storage_file_free(file);
    if(ok) { memset(&app->scan,0,sizeof(app->scan)); memset(&app->strings,0,sizeof(app->strings)); memset(&app->entropy,0,sizeof(app->entropy)); memset(&app->search,0,sizeof(app->search)); app->search_query[0]=0; }
    if(ok) { char body[256]; snprintf(body,sizeof(body),"%s\n\nSize: %llu bytes\nReady for read-only analysis.",furi_string_get_cstr(app->path),(unsigned long long)size); bw_set_text(app,"Selected File",body,BwViewMain); }
    else bw_set_text(app,"Selection failed","File could not be opened read-only.",BwViewMain);
}

static void bw_open_browser(BwApp* app) {
    file_browser_configure(app->browser,"*","/ext",true,true,NULL,false); file_browser_start(app->browser,app->path); bw_switch(app,BwViewBrowser);
}

static bool bw_write_all(File* file, const char* text) {
    size_t length=strlen(text); return storage_file_write(file,text,length)==length;
}

static void bw_write_report(BwApp* app) {
    storage_common_mkdir(app->storage,BW_DATA_DIR); File* file=storage_file_alloc(app->storage); bool ok=file&&storage_file_open(file,BW_REPORT_PATH,FSAM_WRITE,FSOM_CREATE_ALWAYS);
    char line[320];
    if(ok) { snprintf(line,sizeof(line),"Binwalk FZ report\nGenerated: %lu\nInput: %s\nValidated detections total/retained: %lu/%lu%s\nSignature set: %lu\nExtraction: unavailable\n",(unsigned long)furi_hal_rtc_get_timestamp(),bw_has_file(app)?furi_string_get_cstr(app->path):"none",(unsigned long)app->scan.total_valid,(unsigned long)app->scan.count,app->scan.truncated?" (bounded)":"",(unsigned long)bw_signature_count()); ok=bw_write_all(file,line); }
    for(uint32_t i=0;ok&&i<app->scan.count;i++) { const BwDetection* d=&app->scan.detections[i]; snprintf(line,sizeof(line),"0x%llX %s %s\n",(unsigned long long)d->offset,bw_type_name(d->type),d->metadata); ok=bw_write_all(file,line); }
    if(ok&&app->entropy.bytes_scanned) { snprintf(line,sizeof(line),"Entropy whole: %.6f; block size: %lu; blocks: %lu; min/max: %.6f/%.6f\n",app->entropy.whole,(unsigned long)app->entropy.block_size,(unsigned long)app->entropy.block_count,app->entropy.block_min,app->entropy.block_max); ok=bw_write_all(file,line); }
    if(ok) { snprintf(line,sizeof(line),"Strings found/retained: %lu/%lu\nSearch query: %s\nSearch matches: %lu\n",(unsigned long)app->strings.total,(unsigned long)app->strings.count,app->search_query,(unsigned long)app->search.total); ok=bw_write_all(file,line); }
    if(file) { if(storage_file_is_open(file)) storage_file_close(file); storage_file_free(file); }
    bw_set_text(app,ok?"Report saved":"Report failed",ok?BW_REPORT_PATH "\nContains measured parser results only.":"Check SD card and free space.",BwViewMain);
}

static void bw_external_draw(Canvas* canvas, void* model_context) {
    const BwExternalModel* model = model_context;
    BwExternalSnapshot snapshot;
    bw_external_snapshot(model->app->external, &snapshot);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 1, 9, "EXTERNAL BINWALK");
    canvas_set_font(canvas, FontSecondary);
    char line[96];
    if(!snapshot.active) {
        canvas_draw_str(canvas, 1, 24, "UART is not active");
        return;
    }
    if(!snapshot.connected) {
        snprintf(line, sizeof(line), "Waiting for Pi @ %lu", (unsigned long)model->app->external_baud);
        canvas_draw_str(canvas, 1, 22, line);
        canvas_draw_str(canvas, 1, 34, "Protocol: BWF1");
        if(snapshot.error[0]) {
            snprintf(line, sizeof(line), "Error: %.18s", snapshot.error);
            canvas_draw_str(canvas, 1, 48, line);
        }
        canvas_draw_str(canvas, 1, 63, "Back");
        return;
    }
    snprintf(line, sizeof(line), "Binwalk %.14s  %.10s", snapshot.binwalk_version, snapshot.state);
    canvas_draw_str(canvas, 1, 20, line);
    if(snapshot.file_count) {
        snprintf(
            line,
            sizeof(line),
            "%lu/%lu %.17s",
            (unsigned long)(snapshot.file_index + 1U),
            (unsigned long)snapshot.file_count,
            snapshot.file_name);
        canvas_draw_str(canvas, 1, 31, line);
        snprintf(line, sizeof(line), "Size %llu bytes", (unsigned long long)snapshot.file_size);
        canvas_draw_str(canvas, 1, 41, line);
    } else {
        canvas_draw_str(canvas, 1, 31, "No files in Pi input");
    }
    if(!strcmp(snapshot.state, "DONE")) {
        snprintf(
            line,
            sizeof(line),
            "%lu results; %.12s",
            (unsigned long)snapshot.detections,
            snapshot.result_name);
        canvas_draw_str(canvas, 1, 51, line);
        snprintf(line, sizeof(line), "First 0x%llX", (unsigned long long)snapshot.first_offset);
        canvas_draw_str(canvas, 1, 62, line);
    } else {
        canvas_draw_str(
            canvas,
            1,
            62,
            snapshot.running ? "OK cancel   Back" : "< > file  OK scan");
    }
}

static bool bw_external_input(InputEvent* event, void* context) {
    BwApp* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    BwExternalSnapshot snapshot;
    bw_external_snapshot(app->external, &snapshot);
    if(event->key == InputKeyOk && event->type == InputTypeShort) {
        if(snapshot.running) bw_external_cancel(app->external);
        else if(snapshot.file_count) bw_external_scan(app->external);
        else return false;
    } else if(!snapshot.running && event->key == InputKeyRight) {
        bw_external_next(app->external);
    } else if(!snapshot.running && event->key == InputKeyLeft) {
        bw_external_previous(app->external);
    } else {
        return false;
    }
    BwExternalModel* model = view_get_model(app->external_view);
    model->revision++;
    view_commit_model(app->external_view, true);
    return true;
}

static void bw_start_external(BwApp* app) {
    bw_external_start(app->external, app->external_baud);
    bw_switch(app, BwViewExternal);
    BwExternalModel* model = view_get_model(app->external_view);
    model->revision++;
    view_commit_model(app->external_view, true);
}

static void bw_main_selected(void* context, uint32_t index) {
    BwApp* app=context;
    switch(index) {
    case BwMenuSelect: bw_open_browser(app); break;
    case BwMenuExternal: bw_start_external(app); break;
    case BwMenuScan: bw_start_task(app,BwTaskScan); break;
    case BwMenuResults: if(app->scan.count) { bw_populate_results_callbacks(app); bw_switch(app,BwViewResults); } else bw_set_text(app,"Results","Run Scan Signatures first. No cached detections.",BwViewMain); break;
    case BwMenuHex: bw_open_hex(app); break;
    case BwMenuStrings: bw_start_task(app,BwTaskStrings); break;
    case BwMenuEntropy: bw_start_task(app,BwTaskEntropy); break;
    case BwMenuSearch: if(bw_has_file(app)) bw_open_input(app,BwInputSearch); else bw_no_file(app); break;
    case BwMenuReports: bw_write_report(app); break;
    case BwMenuSettings: bw_switch(app,BwViewSettings); break;
    }
}

static void bw_minimum_changed(VariableItem* item) {
    static const uint32_t values[]={4,6,8,12}; static const char* names[]={"4","6","8","12"}; BwApp* app=variable_item_get_context(item); app->minimum_index=variable_item_get_current_value_index(item); app->minimum_string=values[app->minimum_index]; variable_item_set_current_value_text(item,names[app->minimum_index]);
}

static void bw_block_changed(VariableItem* item) {
    static const uint32_t values[]={256,512,1024,2048}; static const char* names[]={"256","512","1024","2048"}; BwApp* app=variable_item_get_context(item); app->block_index=variable_item_get_current_value_index(item); app->entropy_block=values[app->block_index]; variable_item_set_current_value_text(item,names[app->block_index]);
}

static void bw_external_baud_changed(VariableItem* item) {
    BwApp* app = variable_item_get_context(item);
    app->external_baud_index = variable_item_get_current_value_index(item);
    app->external_baud = bw_external_bauds[app->external_baud_index];
    variable_item_set_current_value_text(item, bw_external_baud_names[app->external_baud_index]);
}

static void bw_settings_enter(void* context, uint32_t index) {
    BwApp* app = context;
    if(index != BwSettingAbout) return;

    bw_set_text(
        app,
        "About Binwalk FZ",
        "Version " BW_APP_VERSION
        "\n\nBinwalk identifies embedded file types and data inside firmware and other binary files."
        "\n\nNative mode performs bounded, read-only analysis on files stored on the Flipper microSD card."
        "\n\nRaspberry Pi: install the Binwalk-FZ companion and genuine upstream Binwalk on a Pi or Linux computer. Connect its 3.3V UART TX/RX and GND to the Flipper, put files in the companion input folder, then open External Binwalk to select and scan them."
        "\n\nThe Pi performs the full upstream signature scan; the Flipper controls it and displays genuine results. Advanced extraction and CLI features are used directly on the Pi."
        "\n\nReal measured/parser results only. No inferred or simulated detections."
        "\n\nLicense: GNU GPL v3 or later",
        BwViewSettings);
}

static bool bw_custom_event(void* context, uint32_t event) {
    BwApp* app=context; if(event!=1) return false; bw_join_worker(app); bw_show_worker_result(app); return true;
}

static bool bw_back(void* context) {
    BwApp* app=context;
    if(app->current_view==BwViewMain) view_dispatcher_stop(app->dispatcher);
    else if(app->worker) { app->cancel=true; furi_string_set_str(app->text,"\e#Cancelling...\nWaiting for the bounded reader to close the file."); widget_reset(app->widget); widget_add_text_scroll_element(app->widget,0,0,128,64,furi_string_get_cstr(app->text)); }
    else if(app->current_view==BwViewExternal) { bw_external_stop(app->external); bw_switch(app,BwViewMain); }
    else if(app->current_view==BwViewBrowser) { file_browser_stop(app->browser); bw_switch(app,BwViewMain); }
    else if(app->current_view==BwViewText) bw_switch(app,app->text_return);
    else if(app->current_view==BwViewResults||app->current_view==BwViewSettings||app->current_view==BwViewHex||app->current_view==BwViewInput) bw_switch(app,BwViewMain);
    else bw_switch(app,BwViewMain);
    return true;
}

static void bw_tick(void* context) {
    BwApp* app=context;
    if(app->current_view==BwViewExternal) {
        BwExternalModel* model=view_get_model(app->external_view);
        model->revision++;
        view_commit_model(app->external_view,true);
        return;
    }
    if(!app->worker||app->status!=BwStatusIdle) return;
    uint64_t done=app->progress_done,total=app->progress_total; unsigned percent=total?(unsigned)((done*100U)/total):0; const char* title=app->task==BwTaskScan?"Scanning signatures":app->task==BwTaskStrings?"Extracting strings":app->task==BwTaskEntropy?"Calculating entropy":"Searching";
    furi_string_printf(app->text,"\e#%s\n%u%%\n%llu / %llu bytes\n\nBack requests cancellation.",title,percent,(unsigned long long)done,(unsigned long long)total); widget_reset(app->widget); widget_add_text_scroll_element(app->widget,0,0,128,64,furi_string_get_cstr(app->text));
}

static BwApp* bw_app_alloc(void) {
    BwApp* app=calloc(1,sizeof(BwApp)); if(!app) return NULL;
    app->gui=furi_record_open(RECORD_GUI); app->storage=furi_record_open(RECORD_STORAGE); app->dispatcher=view_dispatcher_alloc(); app->main_menu=submenu_alloc(); app->results_menu=submenu_alloc(); app->widget=widget_alloc(); app->settings=variable_item_list_alloc(); app->input=text_input_alloc(); app->hex_view=view_alloc(); app->external_view=view_alloc(); app->external=bw_external_alloc(); app->path=furi_string_alloc_set("/ext"); app->text=furi_string_alloc();
    if(!app->dispatcher||!app->main_menu||!app->results_menu||!app->widget||!app->settings||!app->input||!app->hex_view||!app->external_view||!app->external||!app->path||!app->text) return app;
    app->browser=file_browser_alloc(app->path); if(!app->browser) return app;
    app->minimum_string=4; app->entropy_block=512; app->external_baud=115200U;
    submenu_set_header(app->main_menu,"Binwalk FZ v" BW_APP_VERSION);
    submenu_add_item(app->main_menu,"Select File",BwMenuSelect,bw_main_selected,app); submenu_add_item(app->main_menu,"External Binwalk",BwMenuExternal,bw_main_selected,app); submenu_add_item(app->main_menu,"Scan Signatures",BwMenuScan,bw_main_selected,app); submenu_add_item(app->main_menu,"Results",BwMenuResults,bw_main_selected,app); submenu_add_item(app->main_menu,"HEX Viewer",BwMenuHex,bw_main_selected,app); submenu_add_item(app->main_menu,"Strings",BwMenuStrings,bw_main_selected,app); submenu_add_item(app->main_menu,"Entropy",BwMenuEntropy,bw_main_selected,app); submenu_add_item(app->main_menu,"Search",BwMenuSearch,bw_main_selected,app); submenu_add_item(app->main_menu,"Reports",BwMenuReports,bw_main_selected,app); submenu_add_item(app->main_menu,"Settings",BwMenuSettings,bw_main_selected,app);
    VariableItem* minimum=variable_item_list_add(app->settings,"String minimum",4,bw_minimum_changed,app); variable_item_set_current_value_index(minimum,0); variable_item_set_current_value_text(minimum,"4");
    VariableItem* block=variable_item_list_add(app->settings,"Entropy block",4,bw_block_changed,app); variable_item_set_current_value_index(block,1); variable_item_set_current_value_text(block,"512");
    VariableItem* baud=variable_item_list_add(app->settings,"External baud",COUNT_OF(bw_external_bauds),bw_external_baud_changed,app); variable_item_set_current_value_index(baud,0); variable_item_set_current_value_text(baud,bw_external_baud_names[0]);
    VariableItem* version=variable_item_list_add(app->settings,"Version",1,NULL,app); variable_item_set_current_value_text(version,BW_APP_VERSION);
    VariableItem* protocol=variable_item_list_add(app->settings,"External protocol",1,NULL,app); variable_item_set_current_value_text(protocol,"BWF1");
    VariableItem* about=variable_item_list_add(app->settings,"About",1,NULL,app); variable_item_set_current_value_text(about,"Open");
    variable_item_list_set_enter_callback(app->settings,bw_settings_enter,app);
    file_browser_set_callback(app->browser,bw_browser_selected,app);
    view_set_context(app->hex_view,app); view_set_draw_callback(app->hex_view,bw_hex_draw); view_set_input_callback(app->hex_view,bw_hex_input); view_allocate_model(app->hex_view,ViewModelTypeLocking,sizeof(BwHexModel)); BwHexModel* model=view_get_model(app->hex_view); model->app=app; view_commit_model(app->hex_view,false);
    view_set_context(app->external_view,app); view_set_draw_callback(app->external_view,bw_external_draw); view_set_input_callback(app->external_view,bw_external_input); view_allocate_model(app->external_view,ViewModelTypeLocking,sizeof(BwExternalModel)); BwExternalModel* external_model=view_get_model(app->external_view); external_model->app=app; view_commit_model(app->external_view,false);
    view_dispatcher_set_event_callback_context(app->dispatcher,app); view_dispatcher_set_custom_event_callback(app->dispatcher,bw_custom_event); view_dispatcher_set_navigation_event_callback(app->dispatcher,bw_back); view_dispatcher_set_tick_event_callback(app->dispatcher,bw_tick,250);
    view_dispatcher_add_view(app->dispatcher,BwViewMain,submenu_get_view(app->main_menu)); view_dispatcher_add_view(app->dispatcher,BwViewBrowser,file_browser_get_view(app->browser)); view_dispatcher_add_view(app->dispatcher,BwViewText,widget_get_view(app->widget)); view_dispatcher_add_view(app->dispatcher,BwViewResults,submenu_get_view(app->results_menu)); view_dispatcher_add_view(app->dispatcher,BwViewSettings,variable_item_list_get_view(app->settings)); view_dispatcher_add_view(app->dispatcher,BwViewInput,text_input_get_view(app->input)); view_dispatcher_add_view(app->dispatcher,BwViewHex,app->hex_view); view_dispatcher_add_view(app->dispatcher,BwViewExternal,app->external_view); app->views_added=true; view_dispatcher_attach_to_gui(app->dispatcher,app->gui,ViewDispatcherTypeFullscreen);
    return app;
}

static bool bw_app_valid(BwApp* app) { return app&&app->gui&&app->storage&&app->dispatcher&&app->main_menu&&app->results_menu&&app->browser&&app->widget&&app->settings&&app->input&&app->hex_view&&app->external_view&&app->external&&app->path&&app->text; }

static void bw_app_free(BwApp* app) {
    if(!app) return;
    app->cancel=true;
    bw_join_worker(app);
    if(app->external) bw_external_free(app->external);
    if(app->dispatcher&&app->views_added) { view_dispatcher_remove_view(app->dispatcher,BwViewExternal); view_dispatcher_remove_view(app->dispatcher,BwViewHex); view_dispatcher_remove_view(app->dispatcher,BwViewInput); view_dispatcher_remove_view(app->dispatcher,BwViewSettings); view_dispatcher_remove_view(app->dispatcher,BwViewResults); view_dispatcher_remove_view(app->dispatcher,BwViewText); view_dispatcher_remove_view(app->dispatcher,BwViewBrowser); view_dispatcher_remove_view(app->dispatcher,BwViewMain); }
    if(app->external_view) view_free(app->external_view);
    if(app->hex_view) view_free(app->hex_view);
    if(app->input) text_input_free(app->input);
    if(app->settings) variable_item_list_free(app->settings);
    if(app->widget) widget_free(app->widget);
    if(app->browser) file_browser_free(app->browser);
    if(app->results_menu) submenu_free(app->results_menu);
    if(app->main_menu) submenu_free(app->main_menu);
    if(app->dispatcher) view_dispatcher_free(app->dispatcher);
    if(app->text) furi_string_free(app->text);
    if(app->path) furi_string_free(app->path);
    if(app->storage) furi_record_close(RECORD_STORAGE);
    if(app->gui) furi_record_close(RECORD_GUI);
    free(app);
}

int32_t binwalk_fz_app(void* p) {
    UNUSED(p); BwApp* app=bw_app_alloc(); if(!bw_app_valid(app)) { bw_app_free(app); return -1; } bw_switch(app,BwViewMain); view_dispatcher_run(app->dispatcher); bw_app_free(app); return 0;
}
