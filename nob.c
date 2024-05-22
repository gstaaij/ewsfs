#define NOB_IMPLEMENTATION
#include "src/nob.h"

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    nob_mkdir_if_not_exists("build");

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "gcc", "-Wall", "-Wextra", "-ggdb");
    nob_cmd_append(&cmd, "-D_FILE_OFFSET_BITS=64");
    nob_cmd_append(&cmd, "-o", "build/ewsfs_fuse");
    nob_cmd_append(&cmd, "src/fuse.c");
    nob_cmd_append(&cmd, "-lfuse");
    nob_cmd_run_sync(cmd);

    return 0;
}
