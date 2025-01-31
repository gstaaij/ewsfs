#ifdef EWSFS_LOG
#include <stdarg.h>
#include <stdio.h>
#define NOB_STRIP_PREFIX
#include "nob.h"

typedef struct {
    String_Builder* items;
    size_t count;
    size_t capacity;
} ewsfs_log_t;

static ewsfs_log_t ewsfs_log_list;

#define EWSFS_LOG_MAX_LINES 40
#define EWSFS_LOG_MAX_LINE_LEN 1024
#define EWSFS_LOG_FILE_NAME "ewsfs.log"

void ewsfs_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[EWSFS_LOG_MAX_LINE_LEN];
    vsnprintf(buffer, EWSFS_LOG_MAX_LINE_LEN, fmt, args);
    va_end(args);

    // Reduce the length of the log list until it's less than EWSFS_LOG_MAX_LINES lines
    while (ewsfs_log_list.count >= EWSFS_LOG_MAX_LINES) {
        da_free(ewsfs_log_list.items[0]);
        for (size_t i = 1; i < ewsfs_log_list.count; ++i) {
            ewsfs_log_list.items[i - 1] = ewsfs_log_list.items[i];
        }
        --ewsfs_log_list.count;
    }

    // Add the new line to the log list
    String_Builder new_line = {0};
    sb_append_cstr(&new_line, buffer);
    da_append(&ewsfs_log_list, new_line);
}

static off_t ewsfs_log_size() {
    off_t size = 0;
    for (size_t i = 0; i < ewsfs_log_list.count; ++i)
        size += ewsfs_log_list.items[i].count + 1;
    return size;
}
#else // EWSFS_LOG
void ewsfs_log(const char* fmt, ...) {
    (void) fmt;
}
#endif // EWSFS_LOG