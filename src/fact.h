#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include "lib/cJSON.h"

typedef struct {
    uint8_t* items;
    size_t count;
    size_t capacity;
} ewsfs_fact_buffer_t;

bool ewsfs_fact_read(FILE* file, ewsfs_fact_buffer_t* buffer);
// Always call this function AFTER reading the FACT at least once
bool ewsfs_fact_write(FILE* file, const ewsfs_fact_buffer_t buffer);


bool ewsfs_fact_init(FILE* file);
void ewsfs_fact_uninit();
bool ewsfs_fact_validate_attributes(cJSON* item);
bool ewsfs_fact_validate_file(cJSON* file);
bool ewsfs_fact_validate_dir(cJSON* dir);
bool ewsfs_fact_validate_item(cJSON* item);
