#ifndef NOSTRFS_DB
#define NOSTRFS_DB

#include <fuse.h>
#include "path.h"


int fill_events_dir(Path path, void *buffer, fuse_fill_dir_t filler);
int get_event_content_data(Path path, char **ret_file_data);
int get_event_kind_data(Path path, char **ret_file_data);
int get_event_pubkey_data(Path path, char **ret_file_data);
long event_creation_time(const char *event_id);

int fill_pubkeys_dir(Path path, void *buffer, fuse_fill_dir_t filler);
int fill_pubkey_events_dir(Path path, void *buffer, fuse_fill_dir_t filler);
int fill_pubkey_kinds_dir(Path path, void *buffer, fuse_fill_dir_t filler);

int fill_tags_dir(Path path, void *buffer, fuse_fill_dir_t filler);
int fill_tag_key_dir(Path path, void *buffer, fuse_fill_dir_t filler);
int fill_tag_values_dir(Path path, void *buffer, fuse_fill_dir_t filler);
int get_tag_value(Path path, char **ret_file_data);

void initialize_db(char *db_file_path);

#endif