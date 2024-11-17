#pragma once
#include <fuse.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include "lib/cJSON.h"

#define EWSFS_FACT_FILE "fact.json"

typedef struct {
    uint8_t* items;
    size_t count;
    size_t capacity;
} ewsfs_fact_buffer_t;

// I can't come up with a better name for these functions, they're the functions
// that should be called when operations are performed on the FACT file
int ewsfs_fact_call_read(char* buffer, size_t size, off_t offset);
int ewsfs_fact_call_write(const char* buffer, size_t size, off_t offset);
int ewsfs_fact_call_flush(FILE* file);

int ewsfs_fact_file_getattr(const char* path, struct stat* st);


bool ewsfs_fact_init(FILE* file);
void ewsfs_fact_uninit();
bool ewsfs_fact_validate(cJSON* root);
bool ewsfs_fact_validate_attributes(cJSON* item);
bool ewsfs_fact_validate_file(cJSON* file);
bool ewsfs_fact_validate_dir(cJSON* dir);
bool ewsfs_fact_validate_item(cJSON* item);
