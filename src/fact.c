#include "fact.h"
#include "block.h"
#include "nob.h"
#include <string.h>

#define FACT_END_ADDRESS_SIZE 4

ewsfs_block_index_list_t fact_block_indexes = {0};

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
