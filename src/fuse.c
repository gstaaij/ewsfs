#define FUSE_USE_VERSION 29
#include <fuse.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "block.h"
#include "fact.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

#define HELLOWORLD_FILE "ewsfs.txt"
static Nob_String_Builder ewsfs_filecontents = {0};

char* devfile = NULL;
FILE* fsfile = NULL;

static int ewsfs_getattr(const char* path, struct stat* st) {
    if (strcmp(path, "/") == 0) {
        // It's the root directory
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        st->st_size = 4096;
    } else if (strcmp(path, "/"HELLOWORLD_FILE) == 0) {
        // It's the `ewsfs.txt` file
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 2;
        st->st_size = ewsfs_filecontents.count;
    } else {
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 2;
        st->st_size = 4096;
    }
    // User and group. we use the user's id who is executing the FUSE driver
    st->st_uid = getuid();
    st->st_gid = getgid();
    return 0;
}

static int ewsfs_readdir(const char* path, void* buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
    (void) path;
    (void) offset;
    (void) fi;
    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);
    filler(buffer, HELLOWORLD_FILE, NULL, 0);
    return 0;
}

static int ewsfs_read(const char* path, char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) offset;
    (void) fi;
    if (strcmp(path, "/"HELLOWORLD_FILE) == 0) {
        size_t bytecount = 0;
        for (size_t i = 0; i < size && i < ewsfs_filecontents.count; ++i) {
            buffer[i] = ewsfs_filecontents.items[i];
            bytecount++;
        }
        return bytecount;
    }
    return -1;
}

static int ewsfs_open(const char* path, struct fuse_file_info* fi) {
    (void) fi;
    if (strcmp(path, "/"HELLOWORLD_FILE) == 0) {
        return 0;
    }
    return -1;
}

static int ewsfs_truncate(const char* path, off_t length) {
    if (strcmp(path, "/"HELLOWORLD_FILE) == 0) {
        long sizediff = length - ewsfs_filecontents.count;
        for (long i = 0; i < sizediff; ++i) {
            nob_da_append(&ewsfs_filecontents, '\0');
        }
        ewsfs_filecontents.count = length;
        // nob_sb_append_cstr(&ewsfs_filecontents, nob_temp_sprintf("\ntruncate{offset: %ld}", length));
        return 0;
    }
    return -1;
}

static int ewsfs_write(const char* path, const char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) fi;
    if (strcmp(path, "/"HELLOWORLD_FILE) == 0) {
        // nob_sb_append_cstr(&ewsfs_filecontents, nob_temp_sprintf("\nwrite{size: %zu, offset: %ld}", size, offset));
        size_t bytecount = 0;
        for (size_t i = offset; i < offset + size; ++i) {
            if (i < ewsfs_filecontents.count)
                ewsfs_filecontents.items[i] = buffer[i - offset];
            else
                nob_da_append(&ewsfs_filecontents, buffer[i - offset]);
            bytecount++;
        }
        return bytecount;
    }
    return -1;
}

static void ewsfs_destroy() {
    ewsfs_fact_uninit();
    nob_da_free(ewsfs_filecontents);
    fclose(fsfile);
}

static struct fuse_operations ewsfs_ops = {
    .getattr = ewsfs_getattr,
    .readdir = ewsfs_readdir,
    .read = ewsfs_read,
    .open = ewsfs_open,
    .truncate = ewsfs_truncate,
    .write = ewsfs_write,
    .destroy = ewsfs_destroy,
};


int main(int argc, char** argv) {
    int i;
    nob_sb_append_cstr(&ewsfs_filecontents, "Hello, World!");

    // Get the device or image filename from arguments
    for (i = 1; i < argc && argv[i][0] == '-'; ++i);
    if (i < argc) {
        devfile = realpath(argv[i], NULL);
        memcpy(&argv[i], &argv[i+1], (argc-i)*sizeof(argv[0]));
        argc--;
    }

    fsfile = fopen(devfile, "rb+");
    if (fsfile == NULL) {
        nob_log(NOB_ERROR, "Couldn't open input file %s", devfile);
        return 1;
    }

    ewsfs_block_read_size(fsfile);
    if (!ewsfs_fact_init(fsfile))
        return 2;

    // ewsfs_fact_buffer_t buffer = {0};
    // if (!ewsfs_fact_read(fsfile, &buffer))
    //     return 42;
    
    // for (size_t i = 0; i < buffer.count; ++i) {
    //     printf("%c", buffer.items[i]);
    // }
    // printf("\n");

    // nob_da_append_many(&buffer, "\nHello, World!", 14);
    // if (!ewsfs_fact_write(fsfile, buffer))
    //     return 43;

    // fclose(fsfile);
    // return 0;

    // leave the rest to FUSE
    return fuse_main(argc, argv, &ewsfs_ops, NULL);
}
