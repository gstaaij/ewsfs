#include "block.h"

#define BLOCK_SIZE_RESERVED_BYTES 8

uint64_t ewsfs_block_size;
void ewsfs_block_read_size(FILE* file) {
    ewsfs_block_size = 0;
    for (char i = BLOCK_SIZE_RESERVED_BYTES-1; i >= 0; --i) {
        ewsfs_block_size |= getc(file) << i*8;
    }
}
void ewsfs_block_set_size(uint64_t block_size) {
    ewsfs_block_size = block_size;
}
uint64_t ewsfs_block_get_size() {
    return ewsfs_block_size;
}

bool ewsfs_block_read(FILE* file, uint64_t block_index, uint8_t* buffer) {
    fseek(file, BLOCK_SIZE_RESERVED_BYTES + block_index*EWSFS_BLOCK_SIZE, SEEK_SET);
    return fread(buffer, EWSFS_BLOCK_SIZE, 1, file) == 1;
}

bool ewsfs_block_write(FILE* file, uint64_t block_index, const uint8_t* buffer) {
    fseek(file, BLOCK_SIZE_RESERVED_BYTES + block_index*EWSFS_BLOCK_SIZE, SEEK_SET);
    return fwrite(buffer, EWSFS_BLOCK_SIZE, 1, file) == 1;
}

bool ewsfs_block_get_next_free_index(const ewsfs_block_index_list_t used_block_indexes, uint64_t* next_free_index) {
    uint64_t index = 0;
    for (size_t i = 0; i < used_block_indexes.count; ++i) {
        if (index == used_block_indexes.items[i]) {
            ++index;
            // If used_block_indexes is out of order (e.g. [0, 1, 3, 2, 4]), we don't want to accidentaly
            // use an index that is actually already in use (in the example, 3 would be chosen because
            // we went past it while index was still 2). Because I'm lazy, I used this inefficient solution.
            // TODO: make this better
            i = 0;
        }
    }
    // TODO: safety checks
    *next_free_index = index;
    return true;
}
