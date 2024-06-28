#define FUSE_USE_VERSION 29
#include <fuse.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "lib/cJSON.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

#define HELLOWORLD_FILE "ewsfs.txt"
static Nob_String_Builder ewsfs_filecontents = {0};

#define DEBUG

char* devfile = NULL;

cJSON* root;

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
        return 0;
    }
    return -1;
}

static int ewsfs_write(const char* path, const char* buffer, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) fi;
    if (strcmp(path, "/"HELLOWORLD_FILE) == 0) {
        size_t bytecount = 0;
        for (size_t i = offset; i < size; ++i) {
            if (i < ewsfs_filecontents.count)
                ewsfs_filecontents.items[i] = buffer[i];
            else
                nob_da_append(&ewsfs_filecontents, buffer[i]);
            bytecount++;
        }
        ewsfs_filecontents.count = bytecount;
        return bytecount;
    }
    return -1;
}

static void ewsfs_destroy() {
    if (root)
        cJSON_Delete(root);
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

static bool validate_fact_item(cJSON* item);

static bool validate_fact_attributes(cJSON* item) {
    cJSON* attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");
    if (!cJSON_IsObject(attributes)) {
        if (attributes != NULL)
            cJSON_DeleteItemFromObjectCaseSensitive(item, "attributes");
        attributes = cJSON_CreateObject();
        cJSON* date_created = cJSON_CreateNumber(0);
        cJSON* date_modified = cJSON_CreateNumber(0);
        cJSON* date_accessed = cJSON_CreateNumber(0);
        cJSON_AddItemToObject(attributes, "date_created", date_created);
        cJSON_AddItemToObject(attributes, "date_modified", date_modified);
        cJSON_AddItemToObject(attributes, "date_accessed", date_accessed);
        cJSON_AddItemToObject(item, "attributes", attributes);
    } else {
        const char* attribute_names[] = {"date_created", "date_modified", "date_accessed"};
        for (size_t i = 0; i < NOB_ARRAY_LEN(attribute_names); ++i) {
            if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(attributes, attribute_names[i]))) {
                nob_log(NOB_ERROR, "Attribute %s of item %s is not a valid number.", attribute_names[i], cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "name")));
                return false;
            }
        }
    }
    return true;
}

static bool validate_fact_file(cJSON* file) {
    if (!validate_fact_attributes(file))
        return false;

    if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(file, "file_size"))) {
        nob_log(NOB_ERROR, "File Size of file %s is not a valid number.", cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "name")));
        return false;
    }
    
    size_t index = 0;
    cJSON* alloc = NULL;
    cJSON_ArrayForEach(alloc, cJSON_GetObjectItemCaseSensitive(file, "allocation")) {
        if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(alloc, "from"))) {
            nob_log(NOB_ERROR, "`from` field of allocation at index %zu of file %s is not a valid number", index, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "name")));
            return false;
        }
        if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(alloc, "length"))) {
            nob_log(NOB_ERROR, "`length` field of allocation at index %zu of file %s is not a valid number", index, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "name")));
            return false;
        }
        index++;
    }

    return true;
}

static bool validate_fact_dir(cJSON* dir) {
    if (!validate_fact_attributes(dir))
        return false;

    cJSON* item = NULL;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(dir, "contents")) {
        if (!validate_fact_item(item))
            return false;
    }
    return true;
}

static bool validate_fact_item(cJSON* item) {
    if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "name")))
        return false;
    cJSON* is_dir = cJSON_GetObjectItemCaseSensitive(item, "is_dir");
    if (!cJSON_IsBool(is_dir))
        return false;
    if (cJSON_IsTrue(is_dir))
        return validate_fact_dir(item);
    return validate_fact_file(item);
}


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
    Nob_String_Builder fact = {0};
    if (!nob_read_entire_file("build/fact.json", &fact))
        return 1;
    nob_sb_append_null(&fact);
    root = cJSON_Parse(fact.items);
    if (!root)
        return 2;
    

    cJSON* fs_info = cJSON_GetObjectItemCaseSensitive(root, "filesystem_info");
    if (!cJSON_IsObject(fs_info) ||
        !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(fs_info, "block_size"))) {
        nob_log(NOB_ERROR, "Filesystem Info not valid.");
        return 3;
    }
    if (!validate_fact_dir(root))
        return 3;
#ifdef DEBUG
    printf("%s", cJSON_Print(root));
#endif
    // leave the rest to FUSE
    return fuse_main(argc, argv, &ewsfs_ops, NULL);
}
