#pragma once
#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>

#define EWSFS_BLOCK_SIZE ewsfs_block_get_size()

void ewsfs_block_read_size(FILE* file);
void ewsfs_block_set_size(uint64_t block_size);
uint64_t ewsfs_block_get_size();
bool ewsfs_block_read(FILE* file, size_t block_index, uint8_t* buffer);
bool ewsfs_block_write(FILE* file, size_t block_index, const uint8_t* buffer);
