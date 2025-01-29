#include <sys/stat.h>
#include <errno.h>
#define NOB_STRIP_PREFIX
#include "nob.h"
#include "block.h"

#define BLOCK_SIZE_RESERVED_BYTES 8

off_t get_file_size(FILE* file) {
    struct stat file_stat = {0};
    if (fstat(fileno(file), &file_stat) != 0)
        return -1;
    return file_stat.st_size;
}

uint64_t ewsfs_block_size = 0;
uint64_t ewsfs_block_count = 0;
bool ewsfs_block_read_size(FILE* file) {
    ewsfs_block_size = 0;
    if (fseek(file, 0, SEEK_SET) != 0)
        return false;
    for (char i = BLOCK_SIZE_RESERVED_BYTES-1; i >= 0; --i) {
        ewsfs_block_size |= getc(file) << i*8;
    }
    off_t file_size = get_file_size(file);
    if (file_size < 0)
        return false;
    ewsfs_block_count = file_size / ewsfs_block_size;
    if (ewsfs_block_count * ewsfs_block_size + BLOCK_SIZE_RESERVED_BYTES > (uint64_t) file_size)
        --ewsfs_block_count;
    return true;
}
void ewsfs_block_set_size(uint64_t block_size) {
    ewsfs_block_size = block_size;
}
uint64_t ewsfs_block_get_size() {
    return ewsfs_block_size;
}

void ewsfs_block_set_count(uint64_t block_count) {
    ewsfs_block_count = block_count;
}
uint64_t ewsfs_block_get_count() {
    return ewsfs_block_count;
}

int ewsfs_block_read(FILE* file, uint64_t block_index, uint8_t* buffer) {
    if (block_index >= ewsfs_block_count)
        return EFAULT;
    if (fseek(file, BLOCK_SIZE_RESERVED_BYTES + block_index*EWSFS_BLOCK_SIZE, SEEK_SET) != 0)
        return errno;
    return fread(buffer, EWSFS_BLOCK_SIZE, 1, file) == 1 ? 0 : EFAULT;
}

int ewsfs_block_write(FILE* file, uint64_t block_index, const uint8_t* buffer) {
    if (block_index >= ewsfs_block_count)
        return EFAULT;
    if (fseek(file, BLOCK_SIZE_RESERVED_BYTES + block_index*EWSFS_BLOCK_SIZE, SEEK_SET) != 0)
        return errno;
    return fwrite(buffer, EWSFS_BLOCK_SIZE, 1, file) == 1 ? 0 : EFAULT;
}

bool ewsfs_block_get_next_free_index(ewsfs_block_index_list_t* used_block_indexes, uint64_t* next_free_index) {
    uint64_t index = 0;
    for (size_t i = 0; i < used_block_indexes->count; ++i) {
        if (index == used_block_indexes->items[i]) {
            ++index;
            // If used_block_indexes is out of order (e.g. [0, 1, 3, 2, 4]), we don't want to accidentaly
            // use an index that is actually already in use (in the example, 3 would be chosen because
            // we went past it while index was still 2). Because I'm lazy, I used this inefficient solution.
            // TODO: make this better
            i = 0;
        }
    }
    if (index >= ewsfs_block_count)
        return false;
    *next_free_index = index;
    da_append(used_block_indexes, index);
    return true;
}
