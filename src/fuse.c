#define FUSE_USE_VERSION 29
#include <fuse.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// Example from https://wiki.osdev.org/FUSE

static int ewsfs_getattr(const char* path, struct stat* st) {
    if (strcmp(path, "/") == 0) {
        // It's the root directory
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        st->st_size = 4096;
    } else if (strcmp(path, "/ewsfs.txt") == 0) {
        // It's the `ewsfs.txt` file
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 2;
        st->st_size = 13;
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
    filler(buffer, "ewsfs.txt", NULL, 0);
    return 0;
}

static int ewsfs_read(const char* path, char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) offset;
    (void) fi;
    if (strcmp(path, "/ewsfs.txt") == 0) {
        const char* text = "Hello, World!";
        for (size_t i = 0; i < size && i < 13; ++i) {
            buffer[i] = text[i];
        }
        return 13;
    }
    return -1;
}

static struct fuse_operations ewsfs_ops = {
    .getattr = ewsfs_getattr,
    .readdir = ewsfs_readdir,
    .read = ewsfs_read,
};

char* devfile = NULL;

int main(int argc, char** argv) {
    int i;

    // Get the device or image filename from arguments
    for (i = 1; i < argc && argv[i][0] == '-'; ++i);
    if (i < argc) {
        devfile = realpath(argv[i], NULL);
        memcpy(&argv[i], &argv[i+1], (argc-i)*sizeof(argv[0]));
        argc--;
    }

    // leave the rest to FUSE
    return fuse_main(argc, argv, &ewsfs_ops, NULL);
}
