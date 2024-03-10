#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L

#define FUSE_USE_VERSION 26

#include <libgen.h>
#include <stdbool.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "db.h"
#include "path.h"

typedef int (* DirFiller)(Path path, void *buffer, fuse_fill_dir_t filler);
typedef bool (* FileDetector)(Path path);
typedef int (* FileDataFetcher)(Path path, char **out_data);
typedef time_t (* CreationTime)(Path path);

typedef enum {
    DATA_FILE,
    DIRECTORY_FILE,
    NULL_FILE_CLASS
} FileClass;

typedef struct {
    const char *name;
    FileClass class;
    FileDetector detect;
    FileDataFetcher fetch_data;
    DirFiller fill;
    CreationTime get_creation_time;
} SyntheticFile;

static int nostrfs_getattr(const char *path, struct stat *st);
static int nostrfs_readdir(
    const char *path, 
    void *buffer, 
    fuse_fill_dir_t filler, 
    off_t offset, 
    struct fuse_file_info *fi
);
static int nostrfs_open(const char *path, struct fuse_file_info *fi);
static int nostrfs_read(
    const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi
);
static int nostrfs_release(const char *path, struct fuse_file_info *fi);

static struct fuse_operations operations = {
    .getattr = nostrfs_getattr,
    .readdir = nostrfs_readdir,
    .read = nostrfs_read,
    .open = nostrfs_open,
    .release = nostrfs_release
};

#define NULL_FILE {.class = NULL_FILE_CLASS}

SyntheticFile k_null_file = NULL_FILE;

//@todo add creation time fields
const SyntheticFile files[] = {
    {.name = "content", .detect = is_content_file, .fetch_data = get_event_content_data, .class = DATA_FILE},
    {.name = "kind", .detect = is_kind_file, .fetch_data = get_event_kind_data, .class = DATA_FILE},
    {.name = "pubkey", .detect = is_pubkey_file, .fetch_data = get_event_pubkey_data, .class = DATA_FILE},
    {.name = "tag value", .detect = is_tag_value_file, .fetch_data = get_tag_value, .class = DATA_FILE},
    {.name = "event dir", .detect = is_event_dir, .fill = fill_event_dir, .class = DIRECTORY_FILE},
    {.name = "events dir", .detect = is_events_dir, .fill = fill_events_dir, .class = DIRECTORY_FILE},
    {.name = "pubkeys dir", .detect = is_pubkeys_dir, .fill = fill_pubkeys_dir, .class = DIRECTORY_FILE},
    {.name = "root dir", .detect = is_root_dir, .fill = fill_root_dir,  .class = DIRECTORY_FILE},
    {.name = "tags dir", .detect = is_tags_dir, .fill = fill_tags_dir, .class = DIRECTORY_FILE},
    {.name = "tag key dir", .detect = is_tag_key_dir, .fill = fill_tag_key_dir, .class = DIRECTORY_FILE},
    {.name = "tag dir", .detect = is_tag_dir, .fill = fill_tag_values_dir, .class = DIRECTORY_FILE},
    NULL_FILE
};

SyntheticFile path_to_file(Path path) {
    for (int i = 0; files[i].class != NULL_FILE_CLASS; i++) {
        if (files[i].detect(path)) {
            return files[i];
        }
    }
    return k_null_file;
}


static int nostrfs_getattr(const char *raw_path, struct stat *st) {

    Path *path = parse_path(raw_path);
    assert(path != NULL);

    SyntheticFile file = path_to_file(*path);

    int return_code = 0;
    if (file.class == NULL_FILE_CLASS) {
        free_path(path);
        return -ENOENT;
    }

    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = time( NULL );

    if (file.get_creation_time != NULL) {
        time_t file_created_at = file.get_creation_time(*path);
        st->st_mtime = file_created_at;
        st->st_ctime = file_created_at;
    }

    if (file.class == DATA_FILE) {
        char *event_data;
        assert(file.fetch_data(*path, &event_data) == 0);

        st->st_mode = S_IFREG | S_IRUSR;
        st->st_nlink = 1;
        st->st_size = 1;

        assert(event_data != NULL);
        st->st_size = strlen(event_data);

        free(event_data);
    }
    else {
        st->st_nlink = 2;
        st->st_mode = S_IFDIR | S_IRUSR;
    }

    free_path(path);
    return return_code;
}

static int nostrfs_readdir(const char *raw_path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    (void)(fi);
    (void)(offset);

    Path *path = parse_path(raw_path);
    assert(path != NULL);

    SyntheticFile file = path_to_file(*path);

    int read_dir_status;

    switch (file.class) {
        case NULL_FILE_CLASS:
            read_dir_status = -ENOENT;
            break;
        case DIRECTORY_FILE:
            file.fill(*path, buffer, filler);

            filler(buffer, ".", NULL, 0);
            filler(buffer, "..", NULL, 0);
            break;
        case DATA_FILE:
            read_dir_status = -ENOTDIR;
            break;
        default:
            assert(false);
    }

    free_path(path);

    return read_dir_status;
}

static int nostrfs_open(const char *raw_path, struct fuse_file_info *fi) {

    Path *path = parse_path(raw_path);
    assert(path != NULL);
    SyntheticFile file = path_to_file(*path);

    int open_status;
    switch (file.class) {
        case DIRECTORY_FILE:
            open_status = -EISDIR;
            break;
        case DATA_FILE:
            fi->keep_cache = true;
            assert(file.fetch_data(*path, (char **) &fi->fh) == 0);
            open_status = 0;
            break;
        case NULL_FILE_CLASS:
            open_status = -ENOENT;
            break;
        default:
            assert(false);
    }

    free_path(path);
    return open_status;
}

static int nostrfs_read(
    const char *raw_path, 
    char *buffer, 
    size_t size, 
    off_t offset, 
    struct fuse_file_info *fi
) {

    Path *path = parse_path(raw_path);
    assert(path != NULL);
    SyntheticFile file = path_to_file(*path);

    int read_length;
    switch (file.class) {
        case DIRECTORY_FILE:
            read_length = -EISDIR;
            break;
        case DATA_FILE: {
            size_t file_length = strlen((char *) fi->fh);
            if (offset < (off_t) file_length) {
                if (offset + size > file_length) {
                    size = file_length - offset;
                }
                memcpy(buffer, (char *) fi->fh + offset, size);
            }
            else {
                size = 0;
            }
            read_length = size;
            break;
        }
        case NULL_FILE_CLASS:
            read_length = -ENOENT;
            break;
        default:
            assert(false);
    }

    free_path(path);
    return read_length;
}

static int nostrfs_release(const char *path, struct fuse_file_info *fi) {
    (void)(path);

    free((char *) fi->fh);
    return 0;
}


int main(int argc, char *argv[]) {
    initialize_db("./test.sqlite3");
    return fuse_main(argc, argv, &operations, NULL);
}





