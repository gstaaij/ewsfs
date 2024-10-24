#include "fact.h"
#include "block.h"
#include "nob.h"

#define FACT_END_ADDRESS_SIZE 4

bool ewsfs_fact_read(FILE* file, ewsfs_fact_buffer_t* buffer) {
    uint8_t temp_buffer[EWSFS_BLOCK_SIZE];
    uint64_t current_block_index = 0;
    do {
        // Read the next block
        if (!ewsfs_block_read(file, current_block_index, temp_buffer))
            return false;
        
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

bool ewsfs_fact_write(FILE* file, const ewsfs_fact_buffer_t* buffer) {
    assert(0 && "TODO: not implemented");
    (void) file;
    (void) buffer;
}
