#define FUSE_USE_VERSION 29
#include <fuse.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "block.h"
#include "fact.h"

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"
#undef rename

char* devfile = NULL;
FILE* fsfile = NULL;

static int ewsfs_getattr(const char* path, struct stat* st) {
    if (strcmp(path, "/") == 0) {
        // It's the root directory
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        st->st_size = 4096;
    } else if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        // It's the FACT file
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 2;
        st->st_size = ewsfs_fact_file_size();
    } else {
        // It's neither of the above, so redirect to `ewsfs_file_getattr`
        int result = ewsfs_file_getattr(path, st);
        if (result != 0) return result;
    }
    // User and group. we use the user's id who is executing the FUSE driver
    st->st_uid = getuid();
    st->st_gid = getgid();
    return 0;
}

static int ewsfs_readdir(const char* path, void* buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
    (void) offset;
    (void) fi;
    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

    if (strcmp(path, "/") == 0) {
        filler(buffer, EWSFS_FACT_FILE, NULL, 0);
    }
    return ewsfs_file_readdir(path, buffer, filler);
}

static int ewsfs_utimens(const char* path, const struct timespec tv[2]) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return -EPERM;
    }
    return ewsfs_file_utimens(path, tv);
}

static int ewsfs_read(const char* path, char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        // If path points to the FACT file, redirect to an `ewsfs_fact_file_*` function or return an error
        return ewsfs_fact_file_read(buffer, size, offset);
    }
    // If path doesn't point to the FACT file, redirect to an `ewsfs_file_*` function
    return ewsfs_file_read(buffer, size, offset, fi);
}

static int ewsfs_mknod(const char* path, mode_t mode, dev_t dev) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return -EEXIST;
    }
    return ewsfs_file_mknod(path, mode, dev);
}

static int ewsfs_unlink(const char* path) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return -EPERM;
    }
    return ewsfs_file_unlink(path);
}

static int ewsfs_rename(const char* oldpath, const char* newpath) {
    if (strcmp(oldpath, "/"EWSFS_FACT_FILE) == 0
     || strcmp(newpath, "/"EWSFS_FACT_FILE) == 0) {
        return -EPERM;
    }
    return ewsfs_file_rename(oldpath, newpath);
}

static int ewsfs_mkdir(const char* path, mode_t mode) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return -EEXIST;
    }
    return ewsfs_file_mkdir(path, mode);
}

static int ewsfs_rmdir(const char* path) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return -ENOTDIR;
    }
    return ewsfs_file_rmdir(path);
}

static int ewsfs_open(const char* path, struct fuse_file_info* fi) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return 0;
    }
    return ewsfs_file_open(path, fi);
}

static int ewsfs_truncate(const char* path, off_t length) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return ewsfs_fact_file_truncate(length);
    }
    return ewsfs_file_truncate(path, length);
}

static int ewsfs_ftruncate(const char* path, off_t length, struct fuse_file_info* fi) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return ewsfs_fact_file_truncate(length);
    }
    return ewsfs_file_ftruncate(length, fi);
}

static int ewsfs_write(const char* path, const char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return ewsfs_fact_file_write(buffer, size, offset);
    }
    return ewsfs_file_write(buffer, size, offset, fi);
}

static int ewsfs_flush(const char* path, struct fuse_file_info* fi) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        int result = ewsfs_fact_file_flush(fsfile);
        fflush(fsfile);
        return result;
    }
    return ewsfs_file_flush(fi);
}

static int ewsfs_release(const char* path, struct fuse_file_info* fi) {
    if (strcmp(path, "/"EWSFS_FACT_FILE) == 0) {
        return 0;
    }
    return ewsfs_file_release(fi);
}

static void ewsfs_destroy() {
    ewsfs_fact_uninit();
    fclose(fsfile);
}

static struct fuse_operations ewsfs_ops = {
    .getattr = ewsfs_getattr,
    .readdir = ewsfs_readdir,
    .utimens = ewsfs_utimens,
    .read = ewsfs_read,
    .open = ewsfs_open,
    .mknod = ewsfs_mknod,
    .unlink = ewsfs_unlink,
    .rename = ewsfs_rename,
    .mkdir = ewsfs_mkdir,
    .rmdir = ewsfs_rmdir,
    .truncate = ewsfs_truncate,
    .ftruncate = ewsfs_ftruncate,
    .write = ewsfs_write,
    .flush = ewsfs_flush,
    .release = ewsfs_release,
    .destroy = ewsfs_destroy,
};


int main(int argc, char** argv) {
    int i;

    // Get the device or image filename from the arguments
    for (i = 1; i < argc && argv[i][0] == '-'; ++i);
    if (i < argc) {
        devfile = realpath(argv[i], NULL);
        memcpy(&argv[i], &argv[i+1], (argc-i)*sizeof(argv[0]));
        argc--;
    }

    fsfile = fopen(devfile, "rb+");
    if (fsfile == NULL) {
        nob_log(ERROR, "Couldn't open input file %s", devfile);
        return 1;
    }

    // Initialise the block size and the FACT
    if (!ewsfs_block_read_size(fsfile))
        return 3;
    if (!ewsfs_fact_init(fsfile))
        return 2;

    // Leave the rest to FUSE
    return fuse_main(argc, argv, &ewsfs_ops, NULL);
}
