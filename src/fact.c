#include <inttypes.h>
#include <string.h>
#include <time.h>
#include "fact.h"
#include "block.h"
#define NOB_STRIP_PREFIX
#include "nob.h"

#include "log.c"

#define FACT_END_ADDRESS_SIZE 8

ewsfs_block_index_list_t fact_block_indexes = {0};
ewsfs_block_index_list_t used_block_indexes = {0};
cJSON* fact_root;
ewsfs_fact_buffer_t fact_current_file_on_disk = {0};
ewsfs_fact_buffer_t fact_file_buffer = {0};
static FILE* fsfile;

bool ewsfs_fact_read_from_image(FILE* file, ewsfs_fact_buffer_t* buffer) {
    uint8_t temp_buffer[EWSFS_BLOCK_SIZE];
    uint64_t current_block_index = 0;
    do {
        // Read the next block
        if (ewsfs_block_read(file, current_block_index, temp_buffer) != 0)
            return false;
        // Add the current block index to the lists of used indexes
        da_append(&fact_block_indexes, current_block_index);
        da_append(&used_block_indexes, current_block_index);

        // Get the next block index
        current_block_index = 0;
        for (int i = 0; i < FACT_END_ADDRESS_SIZE; ++i) {
            int buffer_index = EWSFS_BLOCK_SIZE - i - 1;
            current_block_index |= temp_buffer[buffer_index] << i*8;
        }

        // We need to trim off at least the address at the end of the block
        int end_trim = FACT_END_ADDRESS_SIZE;
        // If this is the last FACT block, trim off the trailing zeroes as well
        if (current_block_index == 0) {
            int i = EWSFS_BLOCK_SIZE - end_trim - 1;
            while (i > 0 && temp_buffer[i] == 0) {
                --i;
                ++end_trim;
            }
        }

        // Append the temp buffer to the final buffer, except for the last end_trim number of bytes
        da_append_many(buffer, temp_buffer, EWSFS_BLOCK_SIZE - end_trim);
        // Repeat until there aren't any blocks anymore
    } while (current_block_index != 0);
    return true;
}

// Always call this function AFTER reading the FACT at least once
bool ewsfs_fact_write_to_image(FILE* file, const ewsfs_fact_buffer_t buffer) {
    uint64_t fact_size_per_block = EWSFS_BLOCK_SIZE - FACT_END_ADDRESS_SIZE;
    double amount_of_blocks_double = buffer.count / (double) fact_size_per_block;
    // Ceil the amount_of_blocks_double value
    size_t amount_of_blocks = amount_of_blocks_double > (size_t) amount_of_blocks_double ? (size_t) amount_of_blocks_double + 1 : (size_t) amount_of_blocks_double;

    for (size_t i = 0; i < amount_of_blocks; ++i) {
        bool is_last_block = i == amount_of_blocks - 1;

        // We need to add a new block index if there aren't enough in the fact_block_indexes list
        if (
            (!is_last_block && i + 1 >= fact_block_indexes.count) ||
            ( is_last_block && i     >= fact_block_indexes.count)
        ) {
            uint64_t new_block_index = 0;
            if (!ewsfs_block_get_next_free_index(&used_block_indexes, &new_block_index))
                return false;
            da_append(&fact_block_indexes, new_block_index);
        }

        // The block we'll be writing to the file
        uint8_t current_block[EWSFS_BLOCK_SIZE];
        memset(current_block, 0, EWSFS_BLOCK_SIZE);

        // Copy the data of the current FACT block to current_block
        uint64_t current_block_index = fact_block_indexes.items[i];
        size_t current_block_size = !is_last_block ? fact_size_per_block : buffer.count - (i * fact_size_per_block);
        uint8_t* offset_buffer = buffer.items + i * fact_size_per_block;
        memcpy(current_block, offset_buffer, current_block_size);

        // If this is not the last block, we'll also need to insert the block index at the end of current_block
        if (!is_last_block) {
            uint64_t next_block_index = fact_block_indexes.items[i + 1];
            for (int j = 0; j < FACT_END_ADDRESS_SIZE; ++j) {
                int buffer_index = EWSFS_BLOCK_SIZE - j - 1;
                current_block[buffer_index] = (uint8_t) ((next_block_index >> j*8) & 0xff);
            }
        }

        // Write current_block to the file
        if (ewsfs_block_write(file, current_block_index, current_block) != 0)
            return false;
    }
    return true;
}

int ewsfs_fact_file_truncate(off_t length) {
    off_t sizediff = length - fact_file_buffer.count;
    // Add the necessary amount of zero characters to the buffer
    // If length < fact_file_buffer.count, sizediff is negative, and this for loop is skipped
    for (off_t i = 0; i < sizediff; ++i) {
        da_append(&fact_file_buffer, '\0');
    }
    fact_file_buffer.count = length;
    return 0;
}

int ewsfs_fact_file_read(char* buffer, size_t size, off_t offset) {
    size_t bytecount = 0;
    for (size_t i = offset; i < offset + size && i < fact_current_file_on_disk.count; ++i) {
        buffer[i] = fact_current_file_on_disk.items[i];
        bytecount++;
    }
    return bytecount;
}

int ewsfs_fact_file_write(const char* buffer, size_t size, off_t offset) {
    size_t bytecount = 0;
    for (size_t i = offset; i < offset + size; ++i) {
        if (i < fact_file_buffer.count)
            fact_file_buffer.items[i] = buffer[i - offset];
        else
            da_append(&fact_file_buffer, buffer[i - offset]);
        bytecount++;
    }
    return bytecount;
}

int ewsfs_fact_file_flush(FILE* file) {
    cJSON* new_root = cJSON_ParseWithLength((char*) fact_file_buffer.items, fact_file_buffer.count);
    if (!new_root || !ewsfs_fact_validate(new_root) || !ewsfs_fact_write_to_image(file, fact_file_buffer)) {
        // If not successful, reset the fact_file_buffer
        fact_file_buffer.count = 0;
        da_append_many(&fact_file_buffer, fact_current_file_on_disk.items, fact_current_file_on_disk.count);
        return EOF;
    }
    // If successful, copy the fact_file_buffer to the fact_current_file_on_disk
    fact_current_file_on_disk.count = 0;
    da_append_many(&fact_current_file_on_disk, fact_file_buffer.items, fact_file_buffer.count);

    // We need to free the old cJSON, otherwise the memory will leak
    cJSON_free(fact_root);
    fact_root = new_root;
    return 0;
}

long ewsfs_fact_file_size() {
    return fact_current_file_on_disk.count;
}

void ewsfs_fact_save_to_disk() {
    // Make sure the FACT is valid
    assert(ewsfs_fact_validate(fact_root));

    // Print the FACT cJSON structure to a string
    const char* printed_json = cJSON_Print(fact_root);

    // Copy the JSON to the relevant FACT file buffers
    fact_file_buffer.count = 0;
    sb_append_cstr(&fact_file_buffer, printed_json);
    fact_current_file_on_disk.count = 0;
    da_append_many(&fact_current_file_on_disk, fact_file_buffer.items, fact_file_buffer.count);

    // Make sure the JSON is written back properly
    assert(ewsfs_fact_write_to_image(fsfile, fact_file_buffer));

    // Flush the device or image file, so the changes are pushed to the disk
    fflush(fsfile);

    ewsfs_log("[FACT] Saved fact.json");
}


typedef struct {
    cJSON* item;
    String_Builder buffer;
    int flags;
} file_handle_t;

#define MAX_FILE_HANDLES 1024
static file_handle_t file_handles[MAX_FILE_HANDLES];

cJSON* ewsfs_file_get_item(const char* path) {
    String_View sv_path = sv_from_cstr(path);
    if (sv_path.count < 1) return NULL;
    if (sv_path.data[0] != '/') return NULL;
    if (sv_path.count == 1) return fact_root;

    cJSON* current_dir_contents = cJSON_GetObjectItemCaseSensitive(fact_root, "contents");
    cJSON* item = NULL;

    String_View name;
    // Traverse the directory structure
    while (sv_path.count > 0) {
        name = sv_chop_by_delim(&sv_path, '/');
        // This skips the initial delimiter and also any potential double delimiters or a delimiter at the end
        if (name.count == 0) continue;

        // Go over all of the items in the `contents` array
        bool found = false;
        cJSON_ArrayForEach(item, current_dir_contents) {
            const char* name_json_str = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "name"));
            String_View name_json = sv_from_cstr(name_json_str);
            // If it matches with the name we got from the path...
            if (sv_eq(name, name_json)) {
                found = true;

                if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "is_dir"))) {
                    // ...and if it's a directory, we continue traversing the directory structure.
                    current_dir_contents = cJSON_GetObjectItemCaseSensitive(item, "contents");
                    break;
                }

                // ...and if it's a file, we have reached the end.

                // If the directory structure wasn't fully traversed,
                // this wasn't the file the user was looking for.
                if (sv_path.count != 0) return NULL;

                // We are done, so return this file
                return item;
            }
        }

        if (!found) return NULL; // We couldn't find the file, so return NULL
    }

    // Return the item (it's a directory in this case)
    return item;
}

int ewsfs_file_getattr(const char* path, struct stat* st) {
#ifdef EWSFS_LOG
    if (strcmp(path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 2;
        st->st_size = ewsfs_log_size();
        return 0;
    }
#endif // EWSFS_LOG

    ewsfs_log("[GETATTR] ewsfs_file_getattr: %s", path);

    // Get the item for this path and fail if it doesn't exist
    cJSON* item = ewsfs_file_get_item(path);
    if (!item) {
        ewsfs_log("[GETATTR] Item not found");
        return -ENOENT;
    }

    cJSON* item_attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");

    // Decode the permission string
    char* perms_str = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item_attributes, "permissions"));
    int perms_int;
    sscanf(perms_str, "%o", &perms_int);
    if (perms_int == EOF) {
        ewsfs_log("[GETATTR] Permissions not valid");
        return -ENOENT;
    }

    // Set stat fields depending on item type
    if (item == fact_root || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "is_dir"))) {
        st->st_mode = S_IFDIR | perms_int;
        st->st_nlink = 2;
        st->st_size = 4096;
    } else {
        st->st_mode = S_IFREG | perms_int; // TODO: make permissions writable
        st->st_nlink = 2;
        st->st_size = (off_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(item, "file_size"));
    }

    // Set universal stat fields
    st->st_ctime = (time_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(item_attributes, "date_created"));
    st->st_mtime = (time_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(item_attributes, "date_modified"));
    st->st_atime = (time_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(item_attributes, "date_accessed"));
    return 0;
}

int ewsfs_file_readdir(const char* path, void* buffer, fuse_fill_dir_t filler) {
#ifdef EWSFS_LOG
    if (strcmp(path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        return -ENOTDIR;
    }
#endif // EWSFS_LOG

    ewsfs_log("[READDIR] ewsfs_file_readdir: %s", path);

    cJSON* dir = ewsfs_file_get_item(path);
    if (!dir) {
        ewsfs_log("[READDIR] Item not found");
        return -ENOENT;
    }
    if (dir != fact_root && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dir, "is_dir"))) {
        ewsfs_log("[READDIR] Not a directory");
        return -ENOTDIR;
    }

#ifdef EWSFS_LOG
    if (dir == fact_root) {
        filler(buffer, EWSFS_LOG_FILE_NAME, NULL, 0);
    }
#endif // EWSFS_LOG

    cJSON* dir_contents = cJSON_GetObjectItemCaseSensitive(dir, "contents");
    cJSON* dir_item = NULL;
    // Loop over all the contents and add the items to the buffer using the `filler` callback
    cJSON_ArrayForEach(dir_item, dir_contents) {
        const char* name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(dir_item, "name"));
        filler(buffer, name, NULL, 0);
    }
    return 0;
}

int ewsfs_file_utimens(const char* path, const struct timespec tv[2]) {
#ifdef EWSFS_LOG
    if (strcmp(path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        return -EPERM;
    }
#endif // EWSFS_LOG

    ewsfs_log("[UTIMENS] ewsfs_file_utimens: %s; %ld; %ld", path, tv[0].tv_sec, tv[1].tv_sec);

    cJSON* item = ewsfs_file_get_item(path);
    if (!item) {
        ewsfs_log("[UTIMENS] Item not found");
        return -ENOENT;
    }

    // Set the date_accessed and date_modified attributes
    cJSON* item_attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(item_attributes, "date_accessed"), (double) tv[0].tv_sec);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(item_attributes, "date_modified"), (double) tv[1].tv_sec);

    // Save the FACT to disk
    ewsfs_fact_save_to_disk();
    return 0;
}

static int ewsfs_file_read_from_disk(file_handle_t* file_handle) {
    uint64_t file_size = (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(file_handle->item, "file_size"));
    uint64_t read_size = 0;

    // A temporary buffer for reading blocks
    char temp_buffer[ewsfs_block_get_size()];

    cJSON* allocation = cJSON_GetObjectItemCaseSensitive(file_handle->item, "allocation");
    cJSON* alloc_item = NULL;
    bool done = false;
    cJSON_ArrayForEach(alloc_item, allocation) {
        uint64_t from = (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "from"));
        uint64_t length = (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "length"));

        // Go over all blocks in this allocation item
        for (uint64_t i = from; i < from + length; ++i) {
            // Read this block into the temporary buffer
            int error = ewsfs_block_read(fsfile, i, (uint8_t*) temp_buffer);
            if (error)
                return -error;
            // Copy the temporary buffer into the file_handle buffer, until file_size is reached
            for (size_t j = 0; j < ewsfs_block_get_size(); ++j) {
                done = read_size >= file_size;
                if (done)
                    break;
                if (read_size < file_handle->buffer.count)
                    file_handle->buffer.items[read_size] = temp_buffer[j];
                else
                    da_append(&file_handle->buffer, temp_buffer[j]);
                ++read_size;
            }
            if (done)
                break;
        }
        if (done)
            break;
    }
    return read_size;
}

static int ewsfs_file_write_to_disk(file_handle_t* file_handle) {
    uint8_t temp_buffer[ewsfs_block_get_size()];
    size_t write_size = 0;

    // Add necessary alloc items
    cJSON* allocation = cJSON_GetObjectItemCaseSensitive(file_handle->item, "allocation");
    cJSON* alloc_item = NULL;
    uint64_t alloc_count = 0;
    cJSON_ArrayForEach(alloc_item, allocation) {
        alloc_count += (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "length"));
    }
    bool should_write_fact = false;
    while (alloc_count * ewsfs_block_get_size() < file_handle->buffer.count) {
        should_write_fact = true;
        alloc_item = cJSON_CreateObject();
        uint64_t new_block_index = 0;
        if (!ewsfs_block_get_next_free_index(&used_block_indexes, &new_block_index))
            return -ENOSPC;
        cJSON_AddNumberToObject(alloc_item, "from", (double) new_block_index);
        // TODO: use length properly
        cJSON_AddNumberToObject(alloc_item, "length", 1.0);

        cJSON_AddItemToArray(allocation, alloc_item);
        ++alloc_count;
    }
    if (should_write_fact)
        ewsfs_fact_save_to_disk();

    // Same as in `ewsfs_file_read_from_disk`, but with writing instead
    bool done = false;
    cJSON_ArrayForEach(alloc_item, allocation) {
        uint64_t from = (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "from"));
        uint64_t length = (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "length"));

        for (uint64_t i = from; i < from + length; ++i) {
            for (size_t j = 0; j < ewsfs_block_get_size(); ++j) {
                done = write_size >= file_handle->buffer.count;
                if (done) {
                    temp_buffer[j] = 0;
                    continue;
                }
                temp_buffer[j] = file_handle->buffer.items[write_size];
                ++write_size;
            }
            int error = ewsfs_block_write(fsfile, i, temp_buffer);
            if (error)
                return -error;
            if (done)
                break;
        }
        if (done)
            break;
    }

    // Set the new file size based on the amount of bytes written
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(file_handle->item, "file_size"), (double) write_size);
    // Save the FACT to the disk (this also flushes the device or image file)
    ewsfs_fact_save_to_disk();
    return write_size;
}

int ewsfs_file_mknod(const char* path, mode_t mode, dev_t dev) {
#ifdef EWSFS_LOG
    if (strcmp(path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        return -EEXIST;
    }
#endif // EWSFS_LOG

    ewsfs_log("[MKNOD] ewsfs_file_mknod: %s", path);

    // We don't support creating anything but normal files as of now
    if (!(mode & S_IFREG)) {
        ewsfs_log("[MKNOD] Unsupported file type");
        return -EINVAL;
    }
    (void) dev;

    cJSON* item = ewsfs_file_get_item(path);
    if (!item) {
        int result = 0;

        // Extract the directory and basename from the path
        String_View sv_path = sv_from_cstr(path);
        String_Builder sb_path_basename = {0};
        String_Builder sb_path_dir = {0}; {
            size_t i = sv_path.count - 1;
            while (i != 0 && sv_path.data[i] != '/')
                --i;
            if (i == sv_path.count - 1) {
                ewsfs_log("[MKNOD] Item is a directory");
                return_defer(-EISDIR);
            }
            da_append_many(&sb_path_dir, sv_path.data, i == 0 ? 1 : i);
            sb_append_null(&sb_path_dir);

            da_append_many(&sb_path_basename, &sv_path.data[i+1], sv_path.count - i - 1);
            sb_append_null(&sb_path_basename);
        }

        cJSON* dir = ewsfs_file_get_item(sb_path_dir.items);
        if (!dir) {
            ewsfs_log("[MKNOD] Item %s not found", sb_path_dir.items);
            return_defer(-ENOENT);
        }
        if (dir != fact_root && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dir, "is_dir"))) {
            ewsfs_log("[MKNOD] Item %s not a directory", sb_path_dir.items);
            return_defer(-ENOTDIR);
        }

        // Create the new cJSON object for this new file
        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", sb_path_basename.items);
        cJSON_AddBoolToObject(item, "is_dir", false);
        cJSON_AddNumberToObject(item, "file_size", 0.0);
        // We don't need to add attributes, as they're added during validation if they're missing
        cJSON_AddArrayToObject(item, "allocation");

        // Add it to the directory's `contents` list
        cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(dir, "contents"), item);

        ewsfs_fact_save_to_disk();

    defer:
        // Cleanup
        da_free(sb_path_dir);
        da_free(sb_path_basename);
        return result;
    }
    ewsfs_log("[MKNOD] Item already exists");
    return -EEXIST;
}

int ewsfs_file_unlink(const char* path) {
#ifdef EWSFS_LOG
    if (strcmp(path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        return -EPERM;
    }
#endif // EWSFS_LOG

    ewsfs_log("[UNLINK] ewsfs_file_unlink: %s", path);

    // Check if the file exists and isn't a directory
    cJSON* item = ewsfs_file_get_item(path);
    if (!item) {
        ewsfs_log("[UNLINK] Item not found");
        return -ENOENT;
    }
    if (item == fact_root || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "is_dir"))) {
        ewsfs_log("[UNLINK] Item is a directory");
        return -EISDIR;
    }

    int result = 0;

    // Get the directory path for this file path
    String_View sv_path = sv_from_cstr(path);
    String_Builder sb_path_dir = {0}; {
        size_t i = sv_path.count - 1;
        while (i != 0 && (sv_path.data[i] != '/' || i == sv_path.count - 1))
            --i;
        da_append_many(&sb_path_dir, sv_path.data, i == 0 ? 1 : i);
        sb_append_null(&sb_path_dir);
    }

    cJSON* dir = ewsfs_file_get_item(sb_path_dir.items);
    if (!dir) {
        ewsfs_log("[UNLINK] Item %s not found", sb_path_dir.items);
        return_defer(-ENOENT);
    }
    if (dir != fact_root && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dir, "is_dir"))) {
        ewsfs_log("[UNLINK] Item %s not a directory", sb_path_dir.items);
        return_defer(-ENOTDIR);
    }

    // Go through this directory's `contents` and remove the item
    cJSON* dir_contents = cJSON_GetObjectItemCaseSensitive(dir, "contents");
    cJSON* dir_item = NULL;
    int index = 0;
    cJSON_ArrayForEach(dir_item, dir_contents) {
        if (dir_item == item) {
            cJSON_DeleteItemFromArray(dir_contents, index);

            ewsfs_fact_save_to_disk();
            return_defer(0);
        }
        ++index;
    }
    ewsfs_log("[UNLINK] Item not found after finding it");
    return_defer(-ENOENT);
defer:
    da_free(sb_path_dir);
    return result;
}

int ewsfs_file_rename(const char* src_path, const char* dst_path) {
#ifdef EWSFS_LOG
    if (strcmp(src_path, "/"EWSFS_LOG_FILE_NAME) == 0
     || strcmp(dst_path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        return -EPERM;
    }
#endif // EWSFS_LOG

    ewsfs_log("[RENAME] ewsfs_file_rename: %s; %s", src_path, dst_path);

    // Get the src_item and dst_item and do a lot of checks
    cJSON* src_item = ewsfs_file_get_item(src_path);
    cJSON* dst_item = ewsfs_file_get_item(dst_path);
    // See man page rename(2)
    if (!src_item) {
        ewsfs_log("[RENAME] Item not found");
        return -ENOENT;
    }
    if (src_item == dst_item) {
        ewsfs_log("[RENAME] Source item same as destination item");
        return 0;
    }
    if (dst_item && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(src_item, "is_dir"))
                 &&  cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dst_item, "is_dir"))) {
        ewsfs_log("[RENAME] Source item not a directory, but destination item is");
        return -EISDIR;
    }
    if (dst_item &&  cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(src_item, "is_dir"))
                 && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dst_item, "is_dir"))) {
        ewsfs_log("[RENAME] Source item is a directory, but destination item is not");
        return -ENOTDIR;
    }
    if (dst_item && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dst_item, "is_dir"))
                 && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(dst_item, "contents")) > 0) {
        ewsfs_log("[RENAME] Destination item not empty");
        return -ENOTEMPTY;
    }

    int result = 0;

    String_View sv_src_path = sv_from_cstr(src_path);
    String_Builder sb_src_path_dir = {0}; {
        size_t i = sv_src_path.count - 1;
        while (i != 0 && (sv_src_path.data[i] != '/' || i == sv_src_path.count - 1))
            --i;
        da_append_many(&sb_src_path_dir, sv_src_path.data, i == 0 ? 1 : i);
        sb_append_null(&sb_src_path_dir);
    }

    String_View sv_dst_path = sv_from_cstr(dst_path);
    String_Builder sb_dst_path_basename = {0};
    String_Builder sb_dst_path_dir = {0}; {
        size_t i = sv_dst_path.count - 1;
        while (i != 0 && (sv_dst_path.data[i] != '/' || i == sv_dst_path.count - 1))
            --i;
        da_append_many(&sb_dst_path_dir, sv_dst_path.data, i == 0 ? 1 : i);
        sb_append_null(&sb_dst_path_dir);

        da_append_many(&sb_dst_path_basename, &sv_dst_path.data[i+1], sv_dst_path.count - i - 1);
        sb_append_null(&sb_dst_path_basename);
    }

    // Get the directories for the source and destination files, and do other checks
    cJSON* src_dir = ewsfs_file_get_item(sb_src_path_dir.items);
    cJSON* dst_dir = ewsfs_file_get_item(sb_dst_path_dir.items);
    if (src_dir == NULL || dst_dir == NULL) {
        ewsfs_log("[RENAME] Item %s or %s not found", sb_src_path_dir.items, sb_dst_path_dir.items);
        return_defer(-ENOENT);
    }
    if (src_dir != fact_root && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(src_dir, "is_dir"))) {
        ewsfs_log("[RENAME] Item %s not a directory", sb_src_path_dir.items);
        return_defer(-ENOTDIR);
    }
    if (dst_dir != fact_root && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dst_dir, "is_dir"))) {
        ewsfs_log("[RENAME] Item %s not a directory", sb_dst_path_dir.items);
        return_defer(-ENOTDIR);
    }

    // Change the name of the source item
    cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(src_item, "name"), sb_dst_path_basename.items);

    // If the directories aren't the same, move the source item to the destination directory
    if (src_dir != dst_dir) {
        // Remove from source directory
        cJSON* src_dir_contents = cJSON_GetObjectItemCaseSensitive(src_dir, "contents");
        cJSON* src_dir_item = NULL;
        int index = 0;
        cJSON_ArrayForEach(src_dir_item, src_dir_contents) {
            if (src_item == src_dir_item) {
                src_item = cJSON_DetachItemFromArray(src_dir_contents, index);
                break;
            }
            ++index;
        }

        // Add to destination directory / replace destination item
        cJSON* dst_dir_contents = cJSON_GetObjectItemCaseSensitive(dst_dir, "contents");
        if (dst_item) {
            cJSON* dst_dir_item = NULL;
            int index = 0;
            cJSON_ArrayForEach(dst_dir_item, dst_dir_contents) {
                if (dst_item == dst_dir_item) {
                    cJSON_ReplaceItemInArray(dst_dir_contents, index, src_item);
                    break;
                }
                ++index;
            }
        } else {
            cJSON_AddItemToArray(dst_dir_contents, src_item);
        }
    }

    // Set the date_modified attribute
    cJSON* attributes = cJSON_GetObjectItemCaseSensitive(src_item, "attributes");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(attributes, "date_modified"), (double) time(NULL));

    ewsfs_fact_save_to_disk();

defer:
    da_free(sb_src_path_dir);
    da_free(sb_dst_path_basename);
    da_free(sb_dst_path_dir);
    return result;
}

int ewsfs_file_mkdir(const char* path, mode_t mode) {
#ifdef EWSFS_LOG
    if (strcmp(path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        return -EEXIST;
    }
#endif // EWSFS_LOG

    ewsfs_log("[MKDIR] ewsfs_file_mkdir: %s", path);

    (void) mode;
    cJSON* item = ewsfs_file_get_item(path);
    if (!item) {
        int result = 0;

        String_View sv_path = sv_from_cstr(path);
        String_Builder sb_path_basename = {0};
        String_Builder sb_path_dir = {0}; {
            size_t i = sv_path.count - 1;
            while (i != 0 && (sv_path.data[i] != '/' || i == sv_path.count - 1))
                --i;
            da_append_many(&sb_path_dir, sv_path.data, i == 0 ? 1 : i);
            sb_append_null(&sb_path_dir);

            da_append_many(&sb_path_basename, &sv_path.data[i+1], sv_path.count - i - 1);
            sb_append_null(&sb_path_basename);
        }

        cJSON* dir = ewsfs_file_get_item(sb_path_dir.items);
        if (!dir) {
            ewsfs_log("[MKDIR] Item %s not found", sb_path_dir.items);
            return_defer(-ENOENT);
        }
        if (dir != fact_root && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dir, "is_dir"))) {
            ewsfs_log("[MKDIR] Item %s not a directory", sb_path_dir.items);
            return_defer(-ENOTDIR);
        }

        // Create the new directory item
        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", sb_path_basename.items);
        cJSON_AddBoolToObject(item, "is_dir", true);
        // We don't need to add attributes, as they're added during validation if they're missing
        cJSON_AddArrayToObject(item, "contents");

        // Add it to the `contents` array
        cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(dir, "contents"), item);

        ewsfs_fact_save_to_disk();

    defer:
        da_free(sb_path_dir);
        da_free(sb_path_basename);
        return result;
    }
    ewsfs_log("[MKDIR] Item exists");
    return -EEXIST;
}

int ewsfs_file_rmdir(const char* path) {
#ifdef EWSFS_LOG
    if (strcmp(path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        return -ENOTDIR;
    }
#endif // EWSFS_LOG

    ewsfs_log("[RMDIR] ewsfs_file_rmdir: %s", path);

    // Do the checks specified in rmdir(2)
    cJSON* item = ewsfs_file_get_item(path);
    if (!item) {
        ewsfs_log("[RMDIR] Item exists");
        return -ENOENT;
    }
    if (item != fact_root && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "is_dir"))) {
        ewsfs_log("[RMDIR] Item not a directory");
        return -ENOTDIR;
    }
    if (cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(item, "contents")) > 0) {
        ewsfs_log("[RMDIR] Directory not empty");
        return -ENOTEMPTY;
    }

    int result = 0;

    String_View sv_path = sv_from_cstr(path);
    String_Builder sb_path_dir = {0}; {
        size_t i = sv_path.count - 1;
        while (i != 0 && (sv_path.data[i] != '/' || i == sv_path.count - 1))
            --i;
        da_append_many(&sb_path_dir, sv_path.data, i == 0 ? 1 : i);
        sb_append_null(&sb_path_dir);
    }

    cJSON* dir = ewsfs_file_get_item(sb_path_dir.items);
    if (!dir) {
        ewsfs_log("[RMDIR] Item %s not found", sb_path_dir.items);
        return_defer(-ENOENT);
    }
    if (dir != fact_root && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(dir, "is_dir"))) {
        ewsfs_log("[RMDIR] Item %s not a directory", sb_path_dir.items);
        return_defer(-ENOTDIR);
    }

    // Go through this directory's `contents` and remove the item
    cJSON* dir_contents = cJSON_GetObjectItemCaseSensitive(dir, "contents");
    cJSON* dir_item = NULL;
    int index = 0;
    cJSON_ArrayForEach(dir_item, dir_contents) {
        if (dir_item == item) {
            cJSON_DeleteItemFromArray(dir_contents, index);

            ewsfs_fact_save_to_disk();
            return_defer(0);
        }
        ++index;
    }
    ewsfs_log("[RMDIR] Item not found after finding it");
    return_defer(-ENOENT);
defer:
    da_free(sb_path_dir);
    return result;
}

int ewsfs_file_truncate(const char* path, off_t length) {
#ifdef EWSFS_LOG
    if (strcmp(path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        return -EPERM;
    }
#endif // EWSFS_LOG

    ewsfs_log("[TRUNCATE] ewsfs_file_truncate: %s; %ld", path, length);

    if (length < 0) {
        ewsfs_log("[TRUNCATE] Length is negative");
        return -EINVAL;
    }

    cJSON* item = ewsfs_file_get_item(path);
    if (!item) {
        ewsfs_log("[TRUNCATE] Item not found");
        return -ENOENT;
    }
    if (item == fact_root || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "is_dir"))) {
        ewsfs_log("[TRUNCATE] Item is a directory");
        return -EISDIR;
    }

    int result = 0;

    // Read the file into a temporary file handle
    file_handle_t file_handle = {0};
    file_handle.item = item;
    int error = ewsfs_file_read_from_disk(&file_handle);
    if (error < 0) {
        ewsfs_log("[TRUNCATE] ewsfs_file_read_from_disk failed with error %d", error);
        return_defer(error);
    }

    // Truncate the same way as in ewsfs_fact_file_truncate
    off_t sizediff = length - file_handle.buffer.count;
    for (off_t i = 0; i < sizediff; ++i) {
        da_append(&file_handle.buffer, '\0');
    }
    file_handle.buffer.count = length;

    // [HACK] Also truncate all open files
    for (uint64_t i = 0; i < MAX_FILE_HANDLES; ++i) {
        if (file_handles[i].item != item)
            continue;
        int error = ewsfs_file_ftruncate(length, &((struct fuse_file_info){.fh = i}));
        if (error < 0) {
            ewsfs_log("[TRUNCATE] ewsfs_file_ftruncate failed with error %d", error);
            return error;
        }
    }

    // Write the file back to the disk
    error = ewsfs_file_write_to_disk(&file_handle);
    if (error < 0) {
        ewsfs_log("[TRUNCATE] ewsfs_file_write_to_disk failed with error %d", error);
        return_defer(error);
    }

    // Remove unnecessary alloc items
    cJSON* allocation = cJSON_GetObjectItemCaseSensitive(file_handle.item, "allocation");
    cJSON* alloc_item = NULL;
    uint64_t alloc_count = 0;
    int delete_from = 0;
    cJSON_ArrayForEach(alloc_item, allocation) {
        if (alloc_count * ewsfs_block_get_size() >= (uint64_t) file_handle.buffer.count) {
            break;
        }
        alloc_count += (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "length"));
        ++delete_from;
    }
    for (int i = cJSON_GetArraySize(allocation) - 1; i >= delete_from; --i) {
        cJSON_DeleteItemFromArray(allocation, i);
    }

    // Set the date_modified attribute
    cJSON* attributes = cJSON_GetObjectItemCaseSensitive(file_handle.item, "attributes");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(attributes, "date_modified"), (double) time(NULL));

    ewsfs_fact_save_to_disk();
defer:
    da_free(file_handle.buffer);
    return result;
}

int ewsfs_file_open(const char* path, struct fuse_file_info* fi) {
#ifdef EWSFS_LOG
    if (strcmp(path, "/"EWSFS_LOG_FILE_NAME) == 0) {
        fi->fh = MAX_FILE_HANDLES;
        return 0;
    }
#endif // EWSFS_LOG

    ewsfs_log("[OPEN] ewsfs_file_open: %s", path);

    cJSON* item = ewsfs_file_get_item(path);
    if (!item) {
        // If the file doesn't exist, and O_CREAT is specified, make the file first
        if (!(fi->flags & O_CREAT)) {
            ewsfs_log("[OPEN] Item not found, and O_CREAT not specified");
            return -ENOENT;
        }

        int error = ewsfs_file_mknod(path, S_IFREG, 0);
        if (error) {
            ewsfs_log("[OPEN] ewsfs_file_mknod failed with error %d", error);
            return error;
        }

        item = ewsfs_file_get_item(path);
    } else if (fi->flags & O_CREAT && fi->flags & O_EXCL) {
        ewsfs_log("[OPEN] Item exists, O_CREAT and O_EXCL specified");
        return -EEXIST;
    }
    if (item == fact_root || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "is_dir"))) {
        ewsfs_log("[OPEN] Item is a directory");
        return -EISDIR;
    }

    // Assign a new file handle to this file
    for (uint64_t i = 0; i < MAX_FILE_HANDLES; ++i) {
        if (!file_handles[i].item) {
            fi->fh = i;
            file_handles[i].item = item;
            file_handles[i].flags = fi->flags;
            int error = ewsfs_file_read_from_disk(&file_handles[i]);
            if (error < 0) {
                ewsfs_log("[OPEN] ewsfs_file_read_from_disk failed with error %d", error);
                return error;
            }

            // Set the date_accessed attribute
            cJSON* attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");
            cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(attributes, "date_accessed"), (double) time(NULL));
            ewsfs_fact_save_to_disk();

            ewsfs_log("[OPEN] Opened file handle %"PRIu64, fi->fh);
            return 0;
        }
    }
    ewsfs_log("[OPEN] Too many open files");
    return -EMFILE;
}

int ewsfs_file_ftruncate(off_t length, struct fuse_file_info* fi) {
#ifdef EWSFS_LOG
    if (fi->fh == MAX_FILE_HANDLES) {
        return -EPERM;
    }
#endif // EWSFS_LOG

    ewsfs_log("[FTRUNCATE] ewsfs_file_ftruncate: %"PRIu64", %ld", fi->fh, length);

    if (length < 0) {
        ewsfs_log("[FTRUNCATE] Length is negative");
        return -EINVAL;
    }

    if (fi->fh >= MAX_FILE_HANDLES) {
        ewsfs_log("[FTRUNCATE] File handle too large");
        return -EBADF;
    }
    file_handle_t* file_handle = &file_handles[fi->fh];
    if (file_handle->flags & O_RDONLY) {
        ewsfs_log("[FTRUNCATE] File handle not writable");
        return -EBADF;
    }

    // Same as in `ewsfs_file_truncate`
    off_t sizediff = length - file_handle->buffer.count;
    for (off_t i = 0; i < sizediff; ++i) {
        da_append(&file_handle->buffer, '\0');
    }
    file_handle->buffer.count = length;

    // Set the date_modified attribute
    cJSON* attributes = cJSON_GetObjectItemCaseSensitive(file_handle->item, "attributes");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(attributes, "date_modified"), (double) time(NULL));

    return 0;
}

int ewsfs_file_read(char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
#ifdef EWSFS_LOG
    if (fi->fh == MAX_FILE_HANDLES) {
        size_t log_size = ewsfs_log_size();
        char temp_buffer[log_size];
        size_t index = 0;
        for (size_t i = 0; i < ewsfs_log_list.count; ++i) {
            for (size_t j = 0; j < ewsfs_log_list.items[i].count; ++j) {
                temp_buffer[index] = ewsfs_log_list.items[i].items[j];
                ++index;
            }
            temp_buffer[index] = '\n';
            ++index;
        }

        size_t write_count = 0;
        for (size_t i = offset; i < offset + size && i < log_size; ++i) {
            buffer[write_count] = temp_buffer[i];
            ++write_count;
        }
        return write_count;
    }
#endif // EWSFS_LOG

    ewsfs_log("[READ] ewsfs_file_read: %"PRIu64"; %zu; %ld", fi->fh, size, offset);

    if (fi->fh >= MAX_FILE_HANDLES) {
        ewsfs_log("[READ] File handle too large");
        return -EBADF;
    }
    const file_handle_t file_handle = file_handles[fi->fh];
    if (file_handle.flags & O_WRONLY) {
        ewsfs_log("[READ] File handle not readable");
        return -EBADF;
    }

    // Copy from the buffer in the file_handle to the provided buffer
    size_t read_size = 0;
    for (size_t i = offset; i < offset + size; ++i) {
        if (i >= file_handle.buffer.count)
            break;
        buffer[i - offset] = file_handle.buffer.items[i];
        read_size++;
    }
    ewsfs_log("[READ] Read %zu bytes", read_size);
    return read_size;
}

int ewsfs_file_write(const char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
#ifdef EWSFS_LOG
    if (fi->fh == MAX_FILE_HANDLES) {
        return -EPERM;
    }
#endif // EWSFS_LOG

    ewsfs_log("[WRITE] ewsfs_file_write: %"PRIu64"; %zu; %ld", fi->fh, size, offset);

    if (fi->fh >= MAX_FILE_HANDLES) {
        ewsfs_log("[WRITE] File handle too large");
        return -EBADF;
    }
    file_handle_t* file_handle = &file_handles[fi->fh];
    if (file_handle->flags & O_RDONLY) {
        ewsfs_log("[WRITE] File handle not writable");
        return -EBADF;
    }

    // Copy from the provided buffer to the file_handle buffer
    size_t write_size = 0;
    for (size_t i = offset; i < offset + size; ++i) {
        if (i < file_handle->buffer.count)
            file_handle->buffer.items[i] = buffer[i - offset];
        else
            da_append(&file_handle->buffer, buffer[i - offset]);
        ++write_size;
    }

    // Set the date_modified attribute
    cJSON* attributes = cJSON_GetObjectItemCaseSensitive(file_handle->item, "attributes");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(attributes, "date_modified"), (double) time(NULL));

    ewsfs_log("[WRITE] Wrote %zu bytes", write_size);
    return write_size;
}

int ewsfs_file_flush(struct fuse_file_info* fi) {
#ifdef EWSFS_LOG
    if (fi->fh == MAX_FILE_HANDLES) {
        return 0;
    }
#endif // EWSFS_LOG

    ewsfs_log("[FLUSH] ewsfs_file_flush: %"PRIu64, fi->fh);

    if (fi->fh >= MAX_FILE_HANDLES) {
        ewsfs_log("[FLUSH] File handle too large");
        return -EBADF;
    }
    file_handle_t file_handle = file_handles[fi->fh];
    if (file_handle.flags & O_RDONLY) {
        ewsfs_log("[FLUSH] File handle not writable");
        return -EBADF;
    }

    // Write the file_handle buffer to disk
    int error = ewsfs_file_write_to_disk(&file_handle);
    if (error < 0) {
        ewsfs_log("[FLUSH] ewsfs_file_write_to_disk failed with error %d", error);
        return error;
    }
    return 0;
}

int ewsfs_file_release(struct fuse_file_info* fi) {
#ifdef EWSFS_LOG
    if (fi->fh == MAX_FILE_HANDLES) {
        return 0;
    }
#endif // EWSFS_LOG

    ewsfs_log("[RELEASE] ewsfs_file_release: %"PRIu64, fi->fh);

    if (fi->fh >= MAX_FILE_HANDLES) {
        ewsfs_log("[RELEASE] File handle too large");
        return -EBADF;
    }
    file_handle_t file_handle = file_handles[fi->fh];
    if (!file_handle.item) {
        ewsfs_log("[RELEASE] File handle not found");
        return -EBADF;
    }

    // Free the memory and mark this file handle as unused
    da_free(file_handle.buffer);
    file_handles[fi->fh] = (file_handle_t) {0};
    return 0;
}


bool ewsfs_fact_init(FILE* file) {
    ewsfs_log("[BLOCK] Reset used blocks");
    fact_block_indexes.count = 0;
    used_block_indexes.count = 0;

    fact_file_buffer.count = 0;
    ewsfs_fact_read_from_image(file, &fact_file_buffer);

    // Copy the buffer to the current_file_on_disk buffer as well
    fact_current_file_on_disk.count = 0;
    da_append_many(&fact_current_file_on_disk, fact_file_buffer.items, fact_file_buffer.count);

    fact_root = cJSON_ParseWithLength((char*) fact_file_buffer.items, fact_file_buffer.count);
    if (!fact_root)
        return false;


    if (!ewsfs_fact_validate(fact_root))
        return false;
#ifdef DEBUG
    printf("%s\n", cJSON_Print(fact_root));
#endif

    fsfile = file;

    return true;
}

void ewsfs_fact_uninit() {
    if (fact_root)
        cJSON_Delete(fact_root);
    da_free(fact_block_indexes);
    da_free(used_block_indexes);
    for (size_t i = 0; i < MAX_FILE_HANDLES; ++i) {
        da_free(file_handles[i].buffer);
    }
#ifdef EWSFS_LOG
    for (size_t i = 0; i < ewsfs_log_list.count; ++i) {
        da_free(ewsfs_log_list.items[i]);
    }
    da_free(ewsfs_log_list);
#endif // EWSFS_LOG
}

bool ewsfs_fact_validate(cJSON* root) {
    cJSON* fs_info = cJSON_GetObjectItemCaseSensitive(root, "filesystem_info");
    if (!cJSON_IsObject(fs_info) ||
        !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(fs_info, "size"))) {
        nob_log(ERROR, "Filesystem Info not valid.");
        return false;
    }
    if (!ewsfs_fact_validate_dir(root))
        return false;
    return true;
}

bool ewsfs_fact_validate_attributes(cJSON* item, bool is_dir) {
    cJSON* attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");
    static const char* attribute_names[] = {"date_created", "date_modified", "date_accessed", "permissions"};
    typedef enum {
        ATTRIBUTE_TYPE_TIME,
        ATTRIBUTE_TYPE_STRING,
    } attribute_types_t;
    static const attribute_types_t attribute_types[] = {ATTRIBUTE_TYPE_TIME, ATTRIBUTE_TYPE_TIME, ATTRIBUTE_TYPE_TIME, ATTRIBUTE_TYPE_STRING};
    if (!cJSON_IsObject(attributes)) {
        // We don't want to throw an error when all of the attributes are missing,
        // just use the default values if that's the case.

        if (attributes != NULL)
            cJSON_DeleteItemFromObjectCaseSensitive(item, "attributes");

        attributes = cJSON_CreateObject();
        for (size_t i = 0; i < ARRAY_LEN(attribute_names); ++i) {
            cJSON* attr = NULL;
            switch (attribute_types[i]) {
                case ATTRIBUTE_TYPE_TIME:
                    attr = cJSON_CreateNumber((double) time(NULL));
                    break;
                case ATTRIBUTE_TYPE_STRING:
                    if (strcmp(attribute_names[i], "permissions") == 0)
                        attr = cJSON_CreateString(is_dir ? "755" : "644");
                    else
                        attr = cJSON_CreateString("");
                    break;
            }
            cJSON_AddItemToObject(attributes, attribute_names[i], attr);
        }
        cJSON_AddItemToObject(item, "attributes", attributes);
    } else {
        for (size_t i = 0; i < ARRAY_LEN(attribute_names); ++i) {
            const char* attribute_name = attribute_names[i];
            cJSON_bool is_correct_type = false;
            switch (attribute_types[i]) {
                case ATTRIBUTE_TYPE_TIME:
                    is_correct_type = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(attributes, attribute_name));
                    break;
                case ATTRIBUTE_TYPE_STRING:
                    is_correct_type = cJSON_IsString(cJSON_GetObjectItemCaseSensitive(attributes, attribute_name));
                    break;
            }

            if (!is_correct_type) {
                nob_log(ERROR, "Attribute %s of item %s is not of a valid type.", attribute_names[i], cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "name")));
                return false;
            }
        }
    }
    return true;
}

bool ewsfs_fact_validate_file(cJSON* file) {
    if (!ewsfs_fact_validate_attributes(file, false))
        return false;

    if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(file, "file_size"))) {
        nob_log(ERROR, "File Size of file %s is not a valid number.", cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "name")));
        return false;
    }

    size_t index = 0;
    cJSON* alloc = NULL;
    cJSON_ArrayForEach(alloc, cJSON_GetObjectItemCaseSensitive(file, "allocation")) {
        if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(alloc, "from"))) {
            nob_log(ERROR, "`from` field of allocation at index %zu of file %s is not a valid number", index, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "name")));
            return false;
        }
        if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(alloc, "length"))) {
            nob_log(ERROR, "`length` field of allocation at index %zu of file %s is not a valid number", index, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "name")));
            return false;
        }

        uint64_t from = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc, "from"));
        uint64_t length = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc, "length"));
        for (uint64_t i = from; i < from + length; ++i) {
            da_append(&used_block_indexes, i);
        }

        index++;
    }

    return true;
}

bool ewsfs_fact_validate_dir(cJSON* dir) {
    if (!ewsfs_fact_validate_attributes(dir, true))
        return false;

    cJSON* item = NULL;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(dir, "contents")) {
        if (!ewsfs_fact_validate_item(item))
            return false;
    }
    return true;
}

bool ewsfs_fact_validate_item(cJSON* item) {
    if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "name")))
        return false;
    cJSON* is_dir = cJSON_GetObjectItemCaseSensitive(item, "is_dir");
    if (!cJSON_IsBool(is_dir))
        return false;
    if (cJSON_IsTrue(is_dir))
        return ewsfs_fact_validate_dir(item);
    return ewsfs_fact_validate_file(item);
}
