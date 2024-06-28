#define NOB_IMPLEMENTATION
#include "src/nob.h"

static char* c_files[] = {
    "src/fuse.c",

    "src/lib/cJSON.c",
};

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    nob_mkdir_if_not_exists("build");

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "gcc", "-Wall", "-Wextra", "-ggdb");
    nob_cmd_append(&cmd, "-D_FILE_OFFSET_BITS=64");
    nob_cmd_append(&cmd, "-o", "build/ewsfs_fuse");
    for (size_t i = 0; i < NOB_ARRAY_LEN(c_files); ++i) {
        nob_cmd_append(&cmd, c_files[i]);
    }
    nob_cmd_append(&cmd, "-lfuse");
    if (!nob_cmd_run_sync(cmd)) return 1;

    return 0;
}
