#pragma once
#include <fuse.h>
#include <stdint.h>
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
int ewsfs_file_readdir(const char* path, void* buffer, fuse_fill_dir_t filler);
int ewsfs_file_utimens(const char* path, const struct timespec tv[2]);
int ewsfs_file_mknod(const char* path, mode_t mode, dev_t dev);
int ewsfs_file_unlink(const char* path);
int ewsfs_file_rename(const char* oldpath, const char* newpath);
int ewsfs_file_mkdir(const char* path, mode_t mode);
int ewsfs_file_rmdir(const char* path);
int ewsfs_file_truncate(const char* path, off_t length);
int ewsfs_file_open(const char* path, struct fuse_file_info* fi);
int ewsfs_file_ftruncate(off_t length, struct fuse_file_info* fi);
int ewsfs_file_read(char* buffer, size_t size, off_t offset, struct fuse_file_info* fi);
int ewsfs_file_write(const char* buffer, size_t size, off_t offset, struct fuse_file_info* fi);
int ewsfs_file_flush(struct fuse_file_info* fi);
int ewsfs_file_release(struct fuse_file_info* fi);

// FACT intialisation and validation functions
bool ewsfs_fact_init(FILE* file);
void ewsfs_fact_uninit();
bool ewsfs_fact_validate(cJSON* root);
bool ewsfs_fact_validate_attributes(cJSON* item);
bool ewsfs_fact_validate_file(cJSON* file);
bool ewsfs_fact_validate_dir(cJSON* dir);
bool ewsfs_fact_validate_item(cJSON* item);
