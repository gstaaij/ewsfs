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

// fact.json file operations
int ewsfs_fact_file_truncate(off_t length);
int ewsfs_fact_file_read(char* buffer, size_t size, off_t offset);
int ewsfs_fact_file_write(const char* buffer, size_t size, off_t offset);
int ewsfs_fact_file_flush(FILE* file);
long ewsfs_fact_file_size();

// All other file operations
cJSON* ewsfs_file_get_item(const char* path);
int ewsfs_file_getattr(const char* path, struct stat* st);
int ewsfs_file_open(const char* path, struct fuse_file_info* fi);
int ewsfs_file_read(char* buffer, size_t size, off_t offset, struct fuse_file_info* fi);
int ewsfs_file_write(const char* buffer, size_t size, off_t offset, struct fuse_file_info* fi);


bool ewsfs_fact_init(FILE* file);
void ewsfs_fact_uninit();
bool ewsfs_fact_validate(cJSON* root);
bool ewsfs_fact_validate_attributes(cJSON* item);
bool ewsfs_fact_validate_file(cJSON* file);
bool ewsfs_fact_validate_dir(cJSON* dir);
bool ewsfs_fact_validate_item(cJSON* item);
