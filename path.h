#ifndef NOSTRFS_PATH
#define NOSTRFS_PATH

#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    int max_num_components;
    int num_components;
    char **path_components;
    char *raw_path;
} Path;

int fill_event_dir(Path path, void *buffer, fuse_fill_dir_t filler);
int fill_root_dir(Path path, void *buffer, fuse_fill_dir_t filler);

Path *parse_path(const char *raw_path);
void free_path(Path *path);

bool is_event_dir(Path path);
bool is_events_dir(Path path);
bool is_pubkeys_dir(Path path);
bool is_root_dir(Path path);
bool is_tags_dir(Path path);
bool is_tag_key_dir(Path path);
bool is_tag_dir(Path path);

bool is_content_file(Path path);
bool is_kind_file(Path path);
bool is_pubkey_file(Path path);
bool is_tag_value_file(Path path);

char *event_id_from_path(Path path);
char *tag_key_from_path(Path path);
char *tag_index_from_path(Path path);
char *tag_value_index_from_path(Path path);

char *path_filename(Path path);
Path dirpath(Path path);
Path parent_dir(Path path, unsigned int num_dirs_ascended);
char *parent_dirname(Path path, unsigned int num_dirs_ascended);

#endif