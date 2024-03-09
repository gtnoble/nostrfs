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

static int get_file_data(Path path, char **data) {
    enum file_type filetype = get_file_type(path);
    assert(is_data_file(filetype));

    char *event_id = event_id_from_path(path);
    int read_status;
    assert(event_id != NULL);
    switch (filetype) {
        case CONTENT_FILE:
            read_status = get_event_content_data(event_id, data);
            break;
        case KIND_FILE:
            read_status = get_event_kind_data(event_id, data);
            break;
        case PUBKEY_FILE:
            read_status = get_event_pubkey_data(event_id, data);
            break;
        case TAG_VALUE_FILE: {
            char *tag_index = tag_index_from_path(path);
            assert(tag_index != NULL);
            char *value_index = tag_value_index_from_path(path);
            assert(value_index != NULL);
            read_status = get_tag_value(event_id, atoi(tag_index), atoi(value_index), data);
            break;
        }
        default:
            assert(false);
    }

    return read_status;
}

static int nostrfs_getattr(const char *raw_path, struct stat *st) {

    Path *path = parse_path(raw_path);
    assert(path != NULL);

    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = time( NULL );

    int return_code = 0;

    enum file_type filetype = get_file_type(*path);

    if (is_data_file(filetype)) {
        char *event_id = event_id_from_path(*path);
        assert(event_id != NULL);
        char *event_data;
        assert(get_file_data(*path, &event_data) == 0);

        time_t event_created_at = event_creation_time(event_id);
        if (event_created_at == -ENOENT) {
            return_code = -ENOENT;
        }
        else {
            st->st_mtime = event_created_at;
            st->st_ctime = event_created_at;
            st->st_mode = S_IFREG | S_IRUSR;
            st->st_nlink = 1;
            st->st_size = 1;

            assert(event_data != NULL);
            st->st_size = strlen(event_data);
        }
        free(event_data);
    }
    else {
        switch(filetype) {
            //@todo add tags file handling
            case TAGS_DIR:
            case TAG_KEY_DIR:
            case TAG_DIR:
            case EVENT_DIR: {
                time_t event_created_at = event_creation_time(path_filename(*path));
                st->st_mtime = event_created_at;
                st->st_ctime = event_created_at;
            }
            // fall through
            case ROOT_DIR:
            case EVENTS_DIR:
                st->st_nlink = 2;
                st->st_mode = S_IFDIR | S_IRUSR;
                break;
            default:
                return_code = -ENOENT;
        }
    }

    free_path(path);
    return return_code;
}

static int nostrfs_readdir(const char *raw_path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    (void)(fi);
    (void)(offset);

    Path *path = parse_path(raw_path);
    assert(path != NULL);

    enum file_type filetype = get_file_type(*path);

    int read_dir_status;
    if (is_directory(filetype)) {
        char *id = event_id_from_path(*path);
        char *tag_key = tag_key_from_path(*path);
        char *tag_index = tag_index_from_path(*path);
        switch (get_file_type(*path)) {
            case ROOT_DIR:
                read_dir_status = -fill_root_dir(buffer, filler);
                break;
            case EVENT_DIR:
                read_dir_status = -fill_event_dir(buffer, filler);
                break;
            case EVENTS_DIR:
                read_dir_status = -fill_events_dir(buffer, filler);
                break;
            case TAGS_DIR: {
                char *id = parent_dirname(*path, 1);
                read_dir_status = -fill_tags_dir(buffer, filler, id);
                break;
            }
            case TAG_KEY_DIR:{
                assert(id != NULL);
                assert(tag_key != NULL);
                read_dir_status = -fill_tag_key_dir(buffer, filler, id, tag_key);
                break;
            }
            case TAG_DIR: {
                assert(id != NULL);
                assert(tag_index != NULL);
                read_dir_status = 
                    -fill_tag_values_dir(buffer, filler, id, atoi(tag_index));
                break;
            }
            default:
                assert(false);
            }

        filler(buffer, ".", NULL, 0);
        filler(buffer, "..", NULL, 0);
    }
    else if (is_data_file(filetype)) {
        read_dir_status = -ENOTDIR;
    }
    else {
        read_dir_status = -ENOENT;
    }


    free_path(path);

    return read_dir_status;
}

static int nostrfs_open(const char *raw_path, struct fuse_file_info *fi) {

    Path *path = parse_path(raw_path);
    assert(path != NULL);
    enum file_type filetype = get_file_type(*path);

    int open_status;
    if (is_directory(filetype)) {
        open_status = -EISDIR;
    }
    else if (is_data_file(filetype)) {
        char *id =  event_id_from_path(*path);
        assert(id != NULL);
        fi->keep_cache = true;
        assert(get_file_data(*path, (char **) &fi->fh) == 0);
        open_status = 0;
    }
    else {
        open_status = -ENOENT;
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
    enum file_type filetype = get_file_type(*path);

    int read_length;
    if (is_directory(filetype)) {
        read_length = -EISDIR;
    }
    else if (is_data_file(filetype)) {
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
    }
    else {
        read_length = -ENOENT;
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





