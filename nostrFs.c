#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <libgen.h>
#include <stdbool.h>
#include <string.h>

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <stdlib.h>
#include <stdio.h>
#include <sqlite3.h>
#include <assert.h>

enum file_type {
    UNKNOWN_FILE_TYPE,
    EVENT_DIR,
    EVENT_FILE,
    EVENTS_DIR,
    ROOT_DIR,
    TAGS_FILE,
    CONTENT_FILE,
    KIND_FILE,
    PUBKEY_FILE,
};

#define ERROR_MISSING_EVENT -1

const char *k_events_dir_name = "/e";

const char *k_tags_filename = "tags";
const char *k_content_filename = "content";
const char *k_kind_filename = "kind";
const char *k_pubkey_filename = "pubkey";

const char k_tag_delimiter = ',';

sqlite3 *db;

char *all_event_ids_query_template = "SELECT id FROM nostrEvents;";
sqlite3_stmt *all_event_ids_query;

char *get_event_query_template = "SELECT * FROM nostrEvents WHERE id = ?;";
sqlite3_stmt *get_event_query;

char *get_event_created_at_template = "SELECT created_at FROM nostrEvents WHERE id = ?;";
sqlite3_stmt *get_created_at_query;

char *get_event_content_query_template = "SELECT content FROM nostrEvents WHERE id = ?;";
sqlite3_stmt *get_content_query;

char *get_event_kind_query_template = "SELECT kind FROM nostrEvents WHERE id = ?;";
sqlite3_stmt *get_kind_query;

char *get_event_pubkey_query_template = "SELECT pubkey FROM nostrEvents WHERE id = ?;";
sqlite3_stmt *get_pubkey_query;

char *get_tags_query_template = 
    "SELECT tag_sequence, tag_value_index, tag_value " 
    "FROM tags "
    "WHERE id = ? "
    "ORDER BY tag_sequence, tag_value_index";
sqlite3_stmt *get_tags_query;

void initialize_db(char *db_file_path);
void parent_dir_name(const char *path, char *ret, int ret_length);
enum file_type getFileType(const char *path);
static bool is_directory(const char *path);
static bool is_event_file(const char *path);
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
static int read_event_file(
    const char *id, 
    char *buffer, 
    size_t size, 
    off_t offset, 
    sqlite3_stmt *read_event_field
);
static int read_tags_file(const char *id, char *buffer, size_t size, off_t offset);
void prepare_query(char *template, sqlite3_stmt **query);

static struct fuse_operations operations = {
    .getattr = nostrfs_getattr,
    .readdir = nostrfs_readdir,
    .read = nostrfs_read,
    .open = nostrfs_open
};


void parent_dir_name(const char *path, char *ret, int ret_length) {
    char path_copy[strlen(path)];
    strcpy(path_copy, path);
    char *parent_dir = basename(dirname(path_copy));
    strncpy(ret, parent_dir, ret_length);
}

void dirname_safe(const char *path, char *ret, int ret_length) {
    char path_copy[strlen(path)];
    strcpy(path_copy, path);
    char *dir = dirname(path_copy);
    strncpy(ret, dir, ret_length);
}

void basename_safe(const char *path, char *ret, int ret_length) {
    char path_copy[strlen(path)];
    strcpy(path_copy, path);
    char *dir = basename(path_copy);
    strncpy(ret, dir, ret_length);
}

enum file_type getFileType(const char *path) {

    int path_length = strlen(path);
    char directory_path[path_length];
    dirname_safe(path, directory_path, path_length);

    if (strcmp(path, k_events_dir_name) == 0)
        return EVENTS_DIR;
    else if (strcmp(path, "/") == 0)
        return ROOT_DIR;
    else if (strcmp(directory_path, k_events_dir_name) == 0) {
        return EVENT_DIR;
    }
    else if (getFileType(directory_path) == EVENT_DIR)  {
        char filename[path_length];
        basename_safe(path, filename, path_length);
        if (strcmp(filename, k_tags_filename) == 0) {
            return TAGS_FILE;
        }
        else if (strcmp(filename, k_content_filename) == 0) {
            return CONTENT_FILE;
        }
        else if (strcmp(filename, k_kind_filename) == 0) {
            return KIND_FILE;
        }
        else if (strcmp(filename, k_pubkey_filename) == 0) {
            return PUBKEY_FILE;
        }
        else
            return UNKNOWN_FILE_TYPE;
    }
    else 
        return UNKNOWN_FILE_TYPE;
}

static bool is_directory(const char *path) {
    enum file_type filetype = getFileType(path);
    return 
        filetype == EVENT_DIR ||
        filetype == ROOT_DIR ||
        filetype == EVENTS_DIR;
}

static bool is_event_file(const char *path) {
    enum file_type filetype = getFileType(path);
    switch (filetype) {
        case TAGS_FILE:
        case CONTENT_FILE:
        case KIND_FILE:
        case PUBKEY_FILE:
            return true;
        default:
            return false;
    }
}

static void db_bind_id_query(sqlite3_stmt *query, const char *event_id) {
    assert(sqlite3_bind_text(query, 1, event_id, -1, NULL) == SQLITE_OK);
}

static long event_creation_time(const char *event_id) {
    db_bind_id_query(get_created_at_query, event_id);
    long time;
    int step_status = sqlite3_step(get_created_at_query);
    if (step_status == SQLITE_ROW) {
        time = sqlite3_column_int64(get_created_at_query, 0);
    }
    else if (step_status == SQLITE_DONE) {
        time = ERROR_MISSING_EVENT;
    }
    else {
        fprintf(stderr, "Could not query event creation time: %s", sqlite3_errmsg(db));
        exit(1);
    }
    sqlite3_reset(get_created_at_query);
    return time;
}

static int nostrfs_getattr(const char *path, struct stat *st) {


    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = time( NULL );

    switch(getFileType(path)) {
        case TAGS_FILE:
        case CONTENT_FILE:
        case KIND_FILE:
        case PUBKEY_FILE: {
            char event_id[strlen(path) + 1];
            parent_dir_name(path, event_id, strlen(path) + 1);
            time_t event_created_at = event_creation_time(event_id);
            if (event_created_at == ERROR_MISSING_EVENT) {
                errno = ENOENT;
                return -1;
            }
            st->st_mtime = event_created_at;
            st->st_ctime = event_created_at;
            st->st_mode = S_IFREG | S_IRUSR;
            st->st_nlink = 1;
            //@todo need to add file size
        }
        break;
        case EVENT_DIR: {
            char path_copy[strlen(path) + 1];
            strcpy(path_copy, path);
            time_t event_created_at = event_creation_time(basename(path_copy));
            st->st_mtime = event_created_at;
            st->st_ctime = event_created_at;
        }
        case ROOT_DIR:
        case EVENTS_DIR:
            st->st_nlink = 2;
            st->st_mode = S_IFDIR | S_IRUSR;
            break;
        default:
            errno = ENOENT;
            return -1;
    }

    return 0;
}

static int nostrfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {


    int read_dir_status;
    switch (getFileType(path)) {
        case ROOT_DIR:
            filler(buffer, "e", NULL, 0);
            read_dir_status = 0;
            break;
        case EVENT_DIR:
            filler(buffer, k_content_filename, NULL, 0);
            filler(buffer, k_kind_filename, NULL, 0);
            filler(buffer, k_pubkey_filename, NULL, 0);
            filler(buffer, k_tags_filename, NULL, 0);
            read_dir_status = 0;
            break;
        case EVENTS_DIR: {
            int stepstatus;
            while ((stepstatus = sqlite3_step(all_event_ids_query)) == SQLITE_ROW) {
                const char *event_id;
                event_id = (const char *) sqlite3_column_text(all_event_ids_query, 0);
                filler(buffer, event_id, NULL, 0);
            }

            if (stepstatus != SQLITE_DONE) {
                fprintf(
                    stderr, 
                    "Error reading events directory: error code %d: error message: %s", 
                    sqlite3_extended_errcode(db), 
                    sqlite3_errmsg(db)
                );
                errno = EINVAL;
                read_dir_status = -1;
            }
            else {
                read_dir_status = 0;
            }

            assert(sqlite3_reset(all_event_ids_query) == SQLITE_OK);
            break;
        }
        case CONTENT_FILE:
        case KIND_FILE:
        case PUBKEY_FILE:
        case TAGS_FILE:
            errno = ENOTDIR;
            read_dir_status = -1;
        default:
            errno = ENOENT;
            read_dir_status = -1;
    }

    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

    return read_dir_status;
}

static int nostrfs_open(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int nostrfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    if (is_directory(path)) {
        errno = EISDIR;
        return -1;
    }

    int read_status;
    if (is_event_file(path)) {
        size_t path_size = strlen(path) + 1;
        char *id = malloc(path_size);
        assert(id != NULL);
        parent_dir_name(path, id, path_size);

        switch (getFileType(path)) {
            case CONTENT_FILE:
                read_status = read_event_file(id, buffer, size, offset, get_content_query);
                break;
            case KIND_FILE:
                read_status = read_event_file(id, buffer, size, offset, get_kind_query);
                break;
            case PUBKEY_FILE:
                read_status = read_event_file(id, buffer, size, offset, get_pubkey_query);
                break;
            case TAGS_FILE:
                read_status = read_tags_file(id, buffer, size, offset);
                break;
            default:
                errno = ENOENT;
                read_status = -1;
        }
        free(id);
    }
    return read_status;

}

static int read_event_file(
    const char *id, 
    char *buffer, 
    size_t size, 
    off_t offset, 
    sqlite3_stmt *read_event_field
) {

    int num_bytes_read;
    db_bind_id_query(get_content_query, id);
    int stepstatus = sqlite3_step(get_content_query);
    if (stepstatus == SQLITE_ROW) {
        const char *content = (const char *) sqlite3_column_text(get_content_query, 0);
        size_t data_length = strlen(content);

        if (offset >= data_length) {
            num_bytes_read = 0;
        }
        else {
            size_t num_bytes_to_copy = strnlen(content + offset, size);
            strncpy(buffer, content + offset, size);
            num_bytes_read = num_bytes_to_copy;
        }
    }
    else if (stepstatus == SQLITE_DONE) {
        errno = ENOENT;
        num_bytes_read = -1;
    }
    else {
        fprintf(stderr, "Error reading content file: %s", sqlite3_errmsg(db));
        errno = EINVAL;
        num_bytes_read = -1;
    }
    assert(sqlite3_reset(get_content_query) == SQLITE_OK);
    return num_bytes_read;
    
}

static int read_tags_file(const char *id, char *buffer, size_t size, off_t offset) {
    db_bind_id_query(get_tags_query, id);

    size_t data_length = 100;
    char *file_data = malloc(data_length);
    assert(file_data != NULL);

    int stepstatus;
    int current_row_index = 0;
    size_t current_data_position = 0;
    while (
        (stepstatus = (sqlite3_step(get_tags_query) == SQLITE_ROW) && 
        current_data_position <= offset + size)
    ) {
        int next_row_index = sqlite3_column_int(get_content_query, 2);
        bool add_newline = next_row_index != current_row_index;
        current_row_index = next_row_index;

        const char *tag_value = (const char *) sqlite3_column_text(get_tags_query, 4);

        size_t next_write_size = 
            strlen(tag_value) + sizeof(k_tag_delimiter) + add_newline * sizeof('\n');
        if (current_data_position + next_write_size >= data_length) {
            file_data = realloc(file_data, data_length *= 2);
            assert(file_data != NULL);
        }

        int num_chars_written;
        if (add_newline) {
            num_chars_written = sprintf(
                file_data + current_data_position, "\n%c%s", k_tag_delimiter, tag_value
            );
        }
        else {
            num_chars_written = sprintf(
                file_data + current_data_position, "%c%s", k_tag_delimiter, tag_value
            );
        }
        assert(num_chars_written == next_write_size);
        current_data_position += next_write_size;
    }

    int num_bytes_read;
    if (stepstatus != SQLITE_DONE) {
        fprintf(stderr, "Failed to read tags: %s", sqlite3_errmsg(db));
        num_bytes_read = -1;
    }
    if (current_data_position == 0) {
        errno = ENOENT;
        num_bytes_read = -1;
    }
    else {
        size_t num_bytes_to_copy = strnlen(file_data + offset, size);
        strncpy(buffer, file_data + offset, size);
        num_bytes_read = num_bytes_to_copy;
    }

    assert(sqlite3_reset(get_content_query) == SQLITE_OK);
    free(file_data);
    return num_bytes_read;
}


void prepare_query(char *template, sqlite3_stmt **query) {
    int template_size = strlen(template) + 1;
    assert(
        sqlite3_prepare_v2(
            db, 
            template, 
            template_size, 
            query, 
            NULL
        ) == SQLITE_OK
    );
}

void initialize_db(char *db_file_path) {
    if (sqlite3_open(db_file_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Failed to open database file \"%s\": %s\n", db_file_path, sqlite3_errmsg(db));
        exit(EXIT_FAILURE);
    }

    sqlite3_extended_result_codes(db, 1);

    prepare_query(
        all_event_ids_query_template, 
        &all_event_ids_query
    );

    prepare_query(
        get_event_content_query_template, 
        &get_content_query
    );

    prepare_query(
        get_event_query_template,
        &get_event_query
    );
    prepare_query(
        get_event_kind_query_template,
        &get_kind_query
    );

    prepare_query(
        get_event_pubkey_query_template,
        &get_pubkey_query
    );

    prepare_query(
        get_tags_query_template,
        &get_tags_query
    );

    prepare_query(
        get_event_created_at_template,
        &get_created_at_query
    );

}

int main(int argc, char *argv[]) {
    initialize_db("./test.sqlite3");
    return fuse_main(argc, argv, &operations, NULL);
}





