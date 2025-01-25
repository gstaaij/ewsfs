#include "fact.h"
#include "block.h"
#define NOB_STRIP_PREFIX
#include "nob.h"
#include <fuse.h>
#include <string.h>

#define FACT_END_ADDRESS_SIZE 4

ewsfs_block_index_list_t fact_block_indexes = {0};
cJSON* fact_root;
ewsfs_fact_buffer_t fact_current_file_on_disk = {0};
ewsfs_fact_buffer_t fact_file_buffer = {0};
static FILE* fsfile;

bool ewsfs_fact_read_from_image(FILE* file, ewsfs_fact_buffer_t* buffer) {
    uint8_t temp_buffer[EWSFS_BLOCK_SIZE];
    uint64_t current_block_index = 0;
    do {
        // Read the next block
        if (!ewsfs_block_read(file, current_block_index, temp_buffer))
            return false;
        // Add the current block index to the list of used FACT indexes
        da_append(&fact_block_indexes, current_block_index);
        
        // Get the next block index
        current_block_index = 0;
        // TODO: FACT_END_ADDRESS_SIZE was probably meant to be in bytes
        for (int i = 0; i < FACT_END_ADDRESS_SIZE; ++i) {
            int buffer_index = EWSFS_BLOCK_SIZE - i - 1;
            current_block_index |= temp_buffer[buffer_index] << i;
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
            if (!ewsfs_block_get_next_free_index(fact_block_indexes, &new_block_index))
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
                current_block[buffer_index] = (uint8_t) (next_block_index >> j);
            }
        }

        // Write current_block to the file
        if (!ewsfs_block_write(file, current_block_index, current_block))
            return false;
    }
    return true;
}

int ewsfs_fact_file_truncate(off_t length) {
    long sizediff = length - fact_file_buffer.count;
    for (long i = 0; i < sizediff; ++i) {
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

    cJSON_free(fact_root);
    fact_root = new_root;
    return 0;
}

long ewsfs_fact_file_size() {
    return fact_current_file_on_disk.count;
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

                // ...and if it's a file, we get the attributes to said file and return out of the function

                // If the directory structure wasn't fully traversed,
                // this wasn't the file the user was looking for.
                if (sv_path.count != 0) return NULL;

                return item;
            }
        }

        if (!found) return NULL; // We couldn't find the file, so return NULL
    }

    return item;
}

int ewsfs_file_getattr(const char* path, struct stat* st) {
    cJSON* item = ewsfs_file_get_item(path);
    if (!item) return -ENOENT;
    char* perms_str = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(item, "attributes"), "permissions"));
    int perms_int;
    sscanf(perms_str, "%o", &perms_int);
    if (perms_int == EOF) return -ENOENT;

    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "is_dir"))) {
        st->st_mode = S_IFDIR | perms_int;
        st->st_nlink = 2;
        st->st_size = 4096;
        return 0;
    }
    st->st_mode = S_IFREG | perms_int; // TODO: make permissions writable
    st->st_nlink = 2;
    st->st_size = (off_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(item, "file_size"));
    // TODO: access, modification and creation dates
    return 0;
}

static int ewsfs_file_read_from_disk(file_handle_t* file_handle) {
    size_t file_size = (size_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(file_handle->item, "file_size"));
    size_t read_size = 0;

    char temp_buffer[ewsfs_block_get_size()];

    cJSON* allocation = cJSON_GetObjectItemCaseSensitive(file_handle->item, "allocation");
    cJSON* alloc_item = NULL;
    bool done = false;
    cJSON_ArrayForEach(alloc_item, allocation) {
        uint64_t from = (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "from"));
        uint64_t length = (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "length"));

        for (uint64_t i = from; i < from + length; ++i) {
            ewsfs_block_read(fsfile, from, (uint8_t*) temp_buffer);
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

int ewsfs_file_open(const char* path, struct fuse_file_info* fi) {
    cJSON* item = ewsfs_file_get_item(path);
    if (!item) return -ENOENT;
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "is_dir"))) return -EISDIR;
    
    for (uint64_t i = 0; i < MAX_FILE_HANDLES; ++i) {
        if (!file_handles[i].item) {
            fi->fh = i;
            file_handles[i].item = item;
            file_handles[i].flags = fi->flags;
            ewsfs_file_read_from_disk(&file_handles[i]);
            return 0;
        }
    }
    return -EMFILE;
}

int ewsfs_file_read(char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
    if (fi->fh >= MAX_FILE_HANDLES)
        return -EBADF;
    const file_handle_t file_handle = file_handles[fi->fh];
    if (file_handle.flags & O_WRONLY)
        return -EBADF;
    
    size_t read_size = 0;
    for (size_t i = offset; i < offset + size; ++i) {
        if (i >= file_handle.buffer.count)
            break;
        buffer[i - offset] = file_handle.buffer.items[i];
        read_size++;
    }
    return read_size;
}

int ewsfs_file_write(const char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
    if (fi->fh >= MAX_FILE_HANDLES)
        return -EBADF;
    file_handle_t* file_handle = &file_handles[fi->fh];
    if (file_handle->flags & O_RDONLY)
        return -EBADF;
    
    size_t write_size = 0;
    for (size_t i = offset; i < offset + size; ++i) {
        if (write_size < file_handle->buffer.count)
            file_handle->buffer.items[i] = buffer[i - offset];
        else
            da_append(&file_handle->buffer, buffer[i - offset]);
        ++write_size;
    }
    return write_size;
}

int ewsfs_file_flush(struct fuse_file_info* fi) {
    if (fi->fh >= MAX_FILE_HANDLES)
        return -EBADF;
    file_handle_t file_handle = file_handles[fi->fh];
    if (file_handle.flags & O_RDONLY)
        return -EBADF;
    
    uint8_t temp_buffer[ewsfs_block_get_size()];
    size_t write_size = 0;

    cJSON* allocation = cJSON_GetObjectItemCaseSensitive(file_handle.item, "allocation");
    cJSON* alloc_item = NULL;
    bool done = false;
    cJSON_ArrayForEach(alloc_item, allocation) {
        uint64_t from = (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "from"));
        uint64_t length = (uint64_t) cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(alloc_item, "length"));

        for (uint64_t i = from; i < from + length; ++i) {
            for (size_t j = 0; j < ewsfs_block_get_size(); ++j) {
                done = write_size >= file_handle.buffer.count;
                if (done) {
                    temp_buffer[j] = 0;
                    continue;
                }
                temp_buffer[j] = file_handle.buffer.items[write_size];
                ++write_size;
            }
            ewsfs_block_write(fsfile, i, temp_buffer);
            if (done)
                break;
        }
        if (done)
            break;
    }
    return 0;
}

int ewsfs_file_release(struct fuse_file_info* fi) {
    if (fi->fh >= MAX_FILE_HANDLES)
        return -EBADF;
    file_handle_t file_handle = file_handles[fi->fh];
    da_free(file_handle.buffer);
    file_handles[fi->fh] = (file_handle_t) {0};
    return 0;
}


bool ewsfs_fact_init(FILE* file) {
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
    printf("%s", cJSON_Print(fact_root));
#endif

    fsfile = file;

    return true;
}

void ewsfs_fact_uninit() {
    if (fact_root)
        cJSON_Delete(fact_root);
    da_free(fact_block_indexes);
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

bool ewsfs_fact_validate_attributes(cJSON* item) {
    cJSON* attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");
    static const char* attribute_names[] = {"date_created", "date_modified", "date_accessed", "permissions"};
    typedef enum {
        ATTRIBUTE_TYPE_NUMBER,
        ATTRIBUTE_TYPE_STRING,
    } attribute_types_t;
    static const attribute_types_t attribute_types[] = {ATTRIBUTE_TYPE_NUMBER, ATTRIBUTE_TYPE_NUMBER, ATTRIBUTE_TYPE_NUMBER, ATTRIBUTE_TYPE_STRING};
    if (!cJSON_IsObject(attributes)) {
        // We don't want to throw an error when all of the attributes are missing,
        // just use the default values if that's the case.

        if (attributes != NULL)
            cJSON_DeleteItemFromObjectCaseSensitive(item, "attributes");
        
        attributes = cJSON_CreateObject();
        for (size_t i = 0; i < ARRAY_LEN(attribute_names); ++i) {
            cJSON* attr = NULL;
            switch (attribute_types[i]) {
                case ATTRIBUTE_TYPE_NUMBER:
                    attr = cJSON_CreateNumber(0);
                    break;
                case ATTRIBUTE_TYPE_STRING:
                    if (strcmp(attribute_names[i], "permissions") == 0)
                        attr = cJSON_CreateString("755");
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
                case ATTRIBUTE_TYPE_NUMBER:
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
    if (!ewsfs_fact_validate_attributes(file))
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
        index++;
    }

    return true;
}

bool ewsfs_fact_validate_dir(cJSON* dir) {
    if (!ewsfs_fact_validate_attributes(dir))
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
