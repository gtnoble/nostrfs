#ifndef NOSTRFS_PATH
#define NOSTRFS_PATH

#include <stdlib.h>

enum file_type {
    UNKNOWN_FILE_TYPE,
    EVENT_DIR,
    EVENT_FILE,
    EVENTS_DIR,
    PUBKEYS_DIR,
    ROOT_DIR,
    TAGS_DIR,
    TAG_KEY_DIR,
    TAG_DIR,
    TAG_VALUE_FILE,
    CONTENT_FILE,
    KIND_FILE,
    PUBKEY_FILE,
    NULL_FILE_TYPE
};

static const enum file_type k_data_files[] = {
    EVENT_FILE,
    CONTENT_FILE,
    KIND_FILE,
    PUBKEY_FILE,
    TAG_VALUE_FILE,
    NULL_FILE_TYPE
};

static const enum file_type k_directory_files[] = {
    EVENT_DIR,
    EVENTS_DIR,
    PUBKEYS_DIR,
    ROOT_DIR,
    TAG_DIR,
    TAG_KEY_DIR,
    TAGS_DIR,
    NULL_FILE_TYPE
};

typedef struct {
    int max_num_components;
    int num_components;
    char **path_components;
    char *raw_path;
} Path;

int fill_event_dir(void *buffer, fuse_fill_dir_t filler);
int fill_root_dir(void *buffer, fuse_fill_dir_t filler);

Path *parse_path(const char *raw_path);
void free_path(Path *path);

bool is_directory(enum file_type filetype);
bool is_event_file(enum file_type filetype);
bool is_normal_file(enum file_type filetype);
bool is_data_file(enum file_type filetype);

char *event_id_from_path(Path path);
char *tag_key_from_path(Path path);
char *tag_index_from_path(Path path);
char *tag_value_index_from_path(Path path);

enum file_type get_file_type(Path path);
char *path_filename(Path path);
Path dirpath(Path path);
Path parent_dir(Path path, unsigned int num_dirs_ascended);
char *parent_dirname(Path path, unsigned int num_dirs_ascended);

#endif