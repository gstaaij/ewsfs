#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define EWSFS_BLOCK_SIZE ewsfs_block_get_size()

typedef struct {
    uint64_t* items;
    size_t count;
    size_t capacity;
} ewsfs_block_index_list_t;

bool ewsfs_block_read_size(FILE* file);
void ewsfs_block_set_size(uint64_t block_size);
uint64_t ewsfs_block_get_size();
int ewsfs_block_read(FILE* file, uint64_t block_index, uint8_t* buffer);
int ewsfs_block_write(FILE* file, uint64_t block_index, const uint8_t* buffer);

bool ewsfs_block_get_next_free_index(ewsfs_block_index_list_t* free_block_indices, uint64_t* next_free_index);
