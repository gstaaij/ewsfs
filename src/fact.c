#include "fact.h"
#include "block.h"
#include "nob.h"
#include <fuse.h>
#include <string.h>

#define FACT_END_ADDRESS_SIZE 4

ewsfs_block_index_list_t fact_block_indexes = {0};
cJSON* fact_root;
ewsfs_fact_buffer_t // TODO
ewsfs_fact_buffer_t fact_file_buffer = {0};

bool ewsfs_fact_read(FILE* file, ewsfs_fact_buffer_t* buffer) {
    uint8_t temp_buffer[EWSFS_BLOCK_SIZE];
    uint64_t current_block_index = 0;
    do {
        // Read the next block
        if (!ewsfs_block_read(file, current_block_index, temp_buffer))
            return false;
        // Add the current block index to the list of used FACT indexes
        nob_da_append(&fact_block_indexes, current_block_index);
        
        // Get the next block index
        current_block_index = 0;
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
        nob_da_append_many(buffer, temp_buffer, EWSFS_BLOCK_SIZE - end_trim);
        // Repeat until there aren't any blocks anymore
    } while (current_block_index != 0);
    return true;
}

// Always call this function AFTER reading the FACT at least once
bool ewsfs_fact_write(FILE* file, const ewsfs_fact_buffer_t buffer) {
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
            nob_da_append(&fact_block_indexes, new_block_index);
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

int ewsfs_fact_call_read(char* buffer, size_t size, off_t offset) {
    size_t bytecount = 0;
    for (size_t i = offset; i < offset + size && i < fact_file_buffer.count; ++i) {
        buffer[i] = fact_file_buffer.items[i];
        bytecount++;
    }
    return bytecount;
}

int ewsfs_fact_call_write(const char* buffer, size_t size, off_t offset) {
    size_t bytecount = 0;
    for (size_t i = offset; i < offset + size; ++i) {
        if (i < fact_file_buffer.count)
            fact_file_buffer.items[i] = buffer[i - offset];
        else
            nob_da_append(&fact_file_buffer, buffer[i - offset]);
        bytecount++;
    }
    return bytecount;
}

int ewsfs_fact_call_flush(FILE* file) {
    cJSON* new_root = cJSON_ParseWithLength((char*) fact_file_buffer.items, fact_file_buffer.count);
    if (!new_root || !ewsfs_fact_validate(new_root))
        return EOF;
    if (!ewsfs_fact_write(file, fact_file_buffer))
        return EOF;
    cJSON_free(fact_root);
    fact_root = new_root;
    return 0;
}

bool ewsfs_fact_init(FILE* file) {
    fact_file_buffer.count = 0;
    ewsfs_fact_read(file, &fact_file_buffer);
    fact_root = cJSON_ParseWithLength((char*) fact_file_buffer.items, fact_file_buffer.count);
    if (!fact_root)
        return false;
    

    if (!ewsfs_fact_validate(fact_root))
        return false;
#ifdef DEBUG
    printf("%s", cJSON_Print(fact_root));
#endif

    return true;
}

void ewsfs_fact_uninit() {
    if (fact_root)
        cJSON_Delete(fact_root);
    nob_da_free(fact_block_indexes);
}

bool ewsfs_fact_validate(cJSON* root) {
    cJSON* fs_info = cJSON_GetObjectItemCaseSensitive(root, "filesystem_info");
    if (!cJSON_IsObject(fs_info) ||
        !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(fs_info, "size"))) {
        nob_log(NOB_ERROR, "Filesystem Info not valid.");
        return false;
    }
    if (!ewsfs_fact_validate_dir(root))
        return false;
    return true;
}

bool ewsfs_fact_validate_attributes(cJSON* item) {
    cJSON* attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");
    static const char* attribute_names[] = {"date_created", "date_modified", "date_accessed"};
    if (!cJSON_IsObject(attributes)) {
        // We don't want to throw an error when all of the attributes are missing,
        // just use the default values if that's the case.

        if (attributes != NULL)
            cJSON_DeleteItemFromObjectCaseSensitive(item, "attributes");
        
        attributes = cJSON_CreateObject();
        for (size_t i = 0; i > NOB_ARRAY_LEN(attribute_names); ++i) {
            cJSON* attr = cJSON_CreateNumber(0);
            cJSON_AddItemToObject(attributes, attribute_names[i], attr);
        }
        cJSON_AddItemToObject(item, "attributes", attributes);
    } else {
        for (size_t i = 0; i < NOB_ARRAY_LEN(attribute_names); ++i) {
            if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(attributes, attribute_names[i]))) {
                nob_log(NOB_ERROR, "Attribute %s of item %s is not a valid number.", attribute_names[i], cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "name")));
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
        nob_log(NOB_ERROR, "File Size of file %s is not a valid number.", cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "name")));
        return false;
    }
    
    size_t index = 0;
    cJSON* alloc = NULL;
    cJSON_ArrayForEach(alloc, cJSON_GetObjectItemCaseSensitive(file, "allocation")) {
        if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(alloc, "from"))) {
            nob_log(NOB_ERROR, "`from` field of allocation at index %zu of file %s is not a valid number", index, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "name")));
            return false;
        }
        if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(alloc, "length"))) {
            nob_log(NOB_ERROR, "`length` field of allocation at index %zu of file %s is not a valid number", index, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "name")));
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
