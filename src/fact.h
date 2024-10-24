#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint8_t* items;
    size_t count;
    size_t capacity;
} ewsfs_fact_buffer_t;

bool ewsfs_fact_read(FILE* file, ewsfs_fact_buffer_t* buffer);
bool ewsfs_fact_write(FILE* file, const ewsfs_fact_buffer_t* buffer);

