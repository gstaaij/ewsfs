#define FUSE_USE_VERSION 29
#include <fuse.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// Example from https://wiki.osdev.org/FUSE

static struct fuse_operations ewsfsOps = {
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
    return fuse_main(argc, argv, &ewsfsOps, NULL);
}
