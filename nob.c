#define NOB_IMPLEMENTATION
#include "src/nob.h"

static char* c_files[] = {
    "src/fuse.c",
    "src/block.c",
    "src/fact.c",

    "src/lib/cJSON.c",
};

void log_usage(Nob_Log_Level log_level, const char* program) {
    nob_log(log_level, "Usage: %s [command] [command args]", program);
    nob_log(log_level, "Possible commands:");
    nob_log(log_level, "    build       Just builds the program.");
    nob_log(log_level, "                This is the default command.");
    nob_log(log_level, "    mount       Build the program and mount the specified file at build/mnt.");
    nob_log(log_level, "                If a filesystem is already mounted at build/mnt, it is unmounted.");
    nob_log(log_level, "                If no mount point is provided, /dev/zero is used.");
    nob_log(log_level, "    umount      Unmount the filesystem mounted using the mount command.");
    nob_log(log_level, "    help        Show this message.");
}

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    Nob_Cmd cmd = {0};

    const char* program = nob_shift_args(&argc, &argv);

    bool should_mount = false;
    char* mount_path = NULL;
    if (argc > 0) {
        const char* command = nob_shift_args(&argc, &argv);
        if (strcmp(command, "build") == 0) {
            should_mount = false;
        } else if (strcmp(command, "mount") == 0) {
            should_mount = true;

            if (argc > 0)
                mount_path = nob_shift_args(&argc, &argv);
        } else if (strcmp(command, "umount") == 0) {
            cmd.count = 0;
            nob_cmd_append(&cmd, "fusermount", "-u", "build/mnt");
            if (!nob_cmd_run_sync(cmd)) return 1;
            return 0;
        } else if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0) {
            log_usage(NOB_INFO, program);
            return 0;
        } else {
            nob_log(NOB_ERROR, "Invalid command '%s'.", command);
            log_usage(NOB_ERROR, program);
            return 1;
        }
    }

    nob_mkdir_if_not_exists("build");

    cmd.count = 0;
    nob_cmd_append(&cmd, "gcc", "-Wall", "-Wextra", "-ggdb");
    nob_cmd_append(&cmd, "-D_FILE_OFFSET_BITS=64");
    nob_cmd_append(&cmd, "-o", "build/ewsfs_fuse");
    for (size_t i = 0; i < NOB_ARRAY_LEN(c_files); ++i) {
        nob_cmd_append(&cmd, c_files[i]);
    }
    nob_cmd_append(&cmd, "-lfuse");
    if (!nob_cmd_run_sync(cmd)) return 1;

    if (should_mount) {
        nob_mkdir_if_not_exists("build/mnt");

        nob_log(NOB_INFO, "Trying to unmount build/mnt in case it's already mounted...");
        cmd.count = 0;
        nob_cmd_append(&cmd, "fusermount", "-u", "build/mnt");
        nob_cmd_run_sync(cmd);
        nob_log(NOB_INFO, "Done trying to unmount. You can ignore any errors fusermount spits out.");
        nob_log(NOB_INFO, "");

        cmd.count = 0;
        nob_cmd_append(&cmd, "./build/ewsfs_fuse", mount_path ? mount_path : "/dev/zero", "build/mnt");
        if (!nob_cmd_run_sync(cmd)) return 1;
    }

    return 0;
}
