#include "block.h"
#include <assert.h>

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

bool ewsfs_block_read(FILE* file, size_t block_index, uint8_t* buffer) {
    fseek(file, BLOCK_SIZE_RESERVED_BYTES + block_index*EWSFS_BLOCK_SIZE, SEEK_SET);
    return fread(buffer, EWSFS_BLOCK_SIZE, 1, file) == 1;
}

bool ewsfs_block_write(FILE* file, size_t block_index, const uint8_t* buffer) {
    fseek(file, BLOCK_SIZE_RESERVED_BYTES + block_index*EWSFS_BLOCK_SIZE, SEEK_SET);
    return fwrite(buffer, EWSFS_BLOCK_SIZE, 1, file) == 1;
}
