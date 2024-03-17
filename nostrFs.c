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
#include "synthetic_file.h"

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


static int nostrfs_getattr(const char *raw_path, struct stat *st) {

    Path *path = parse_path(raw_path);
    assert(path != NULL);

    SyntheticFile *file = path_to_file(*path);

    int return_code = 0;
    if (file->type == NULL_FILE_TYPE) {
        free_path(path);
        return -ENOENT;
    }

    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = time( NULL );

    const char *event_id = event_id_from_path(*path);
    if (event_id != NULL) {
        time_t file_created_at = event_creation_time(event_id);
        st->st_mtime = file_created_at;
        st->st_ctime = file_created_at;
    }

    if (file->type == DATA_FILE) {
        char *event_data;
        const bool data_exists = file->fetch_data(*path, &event_data) == 0;
        assert(data_exists);


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

    SyntheticFile *file = path_to_file(*path);

    int read_dir_status;

    switch (file->type) {
        case NULL_FILE_TYPE:
            read_dir_status = -ENOENT;
            break;
        case DIRECTORY_FILE:
            read_dir_status = 0;
            file->fill(*path, buffer, filler);

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
    SyntheticFile *file = path_to_file(*path);

    int open_status;
    switch (file->type) {
        case DIRECTORY_FILE:
            open_status = -EISDIR;
            break;
        case DATA_FILE:
            fi->keep_cache = true;
            const bool data_exists = file->fetch_data(*path, (char **) &fi->fh) == 0;
            assert(data_exists);
            open_status = 0;
            break;
        case NULL_FILE_TYPE:
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
    SyntheticFile *file = path_to_file(*path);

    int read_length;
    switch (file->type) {
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
        case NULL_FILE_TYPE:
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
    link_files();
    return fuse_main(argc, argv, &operations, NULL);
}





