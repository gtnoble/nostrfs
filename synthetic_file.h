#ifndef NOSTRFS_SYNTHETIC_FILE
#define NOSTRFS_SYNTHETIC_FILE

#include <fuse.h>

#include "path.h"

#define MAX_PARENT_TAGS 8

typedef int (* DirFiller)(Path path, void *buffer, fuse_fill_dir_t filler);
typedef bool (* FileDetector)(Path path);
typedef int (* FileDataFetcher)(Path path, char **out_data);
typedef time_t (* CreationTime)(Path path);

typedef enum {
    DATA_FILE,
    DIRECTORY_FILE,
    NULL_FILE_TYPE
} FileType;

typedef enum {
    NULL_TAG,
    CONTENT_FILE_TAG,
    KIND_FILE_TAG,
    TAG_VALUE_FILE_TAG,
    EVENT_DIR_TAG,
    EVENTS_DIR_TAG,
    PUBKEY_FILE_TAG,
    ROOT_DIR_TAG,
    TAGS_DIR_TAG,
    TAG_KEY_DIR_TAG,
    TAG_DIR_TAG,
    PUBKEYS_DIR_TAG,
    PUBKEY_DIR_TAG,
    PUBKEY_EVENTS_DIR_TAG,
    PUBKEY_KINDS_DIR_TAG
} FileTag;

typedef struct SyntheticFile SyntheticFile;

typedef struct SyntheticFile {
    const char *filename;
    const FileTag tag;
    const SyntheticFile **parents;
    int num_parents;
    const FileType type;
    const FileDataFetcher fetch_data;
    const DirFiller fill;
    const FileTag parent_tags[MAX_PARENT_TAGS];
} SyntheticFile;

void link_files(void);

SyntheticFile *path_to_file(Path path);
char *event_id_from_path(Path path);
char *tag_key_from_path(Path path);
char *tag_index_from_path(Path path);
char *pubkey_from_path(Path path);
char *tag_value_index_from_path(Path path);

#endif
