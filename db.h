#ifndef NOSTRFS_DB
#define NOSTRFS_DB

#include <fuse.h>


int fill_events_dir(void *buffer, fuse_fill_dir_t filler);
int fill_pubkeys_dir(void *buffer, fuse_fill_dir_t filler);
int get_event_content_data(const char *id, char **ret_file_data);
int get_event_kind_data(const char *id, char **ret_file_data);
int get_event_pubkey_data(const char *id, char **ret_file_data);

long event_creation_time(const char *event_id);

int fill_tags_dir(void *buffer, fuse_fill_dir_t filler, const char *id);
int fill_tag_key_dir(void *buffer, fuse_fill_dir_t filler, const char *id, const char *key);
int fill_tag_values_dir(void *buffer, fuse_fill_dir_t filler, const char *id, int tag_index);
int get_tag_value(const char *id, int tag_index, int value_index, char **ret_file_data);

void initialize_db(char *db_file_path);

#endif