
#include <fuse.h>
#include <assert.h>
#include <string.h>

#include "db.h"
#include "path.h"
#include "synthetic_file.h"

const char * const k_events_dir_name = "e";
const char * const k_tags_dir_name = "tags";
const char * const k_content_filename = "content";
const char * const k_kind_filename = "kind";
const char * const k_pubkey_filename = "pubkey";

static const char *k_event_dir_contents_filenames[] = {
    k_tags_dir_name,
    k_content_filename,
    k_kind_filename,
    k_pubkey_filename,
    NULL
};

const char * const k_pubkeys_dir_name = "p";
const char * const k_pubkeys_events_dir_name = "e";
const char * const k_pubkey_kinds_dir_name = "kind";

static const char *k_pubkey_dir_contents_filenames[] = {
    k_pubkeys_events_dir_name,
    k_pubkey_kinds_dir_name, 
    NULL
};

static const char *k_root_dir_contents_filenames[] = {
    k_events_dir_name,
    k_pubkeys_dir_name,
    NULL
};

#define NULL_FILE {.type = NULL_FILE_TYPE}

SyntheticFile k_null_file = NULL_FILE;

#define TAGS(...)  { __VA_ARGS__, NULL_TAG }

const SyntheticFile *find_file(FileTag tag);

int fill_pubkey_dir(Path path, void *buffer, fuse_fill_dir_t filler);
int fill_event_dir(Path path, void *buffer, fuse_fill_dir_t filler);
int fill_root_dir(Path path, void *buffer, fuse_fill_dir_t filler);

//@todo add creation time fields
SyntheticFile files[] = {
    {.tag = ROOT_DIR_TAG, .parent_tags = {NULL_TAG}, .filename = NULL, .fill = fill_root_dir,  .type = DIRECTORY_FILE},
    
    {.tag = EVENTS_DIR_TAG, .parent_tags = TAGS(ROOT_DIR_TAG), .filename = k_events_dir_name, .fill = fill_events_dir, .type = DIRECTORY_FILE},
    {.tag = PUBKEYS_DIR_TAG, .parent_tags = TAGS(ROOT_DIR_TAG), .filename = k_pubkeys_dir_name, .fill = fill_pubkeys_dir, .type = DIRECTORY_FILE},

    {.tag = EVENT_DIR_TAG, .parent_tags = TAGS(EVENTS_DIR_TAG, PUBKEY_EVENTS_DIR_TAG), .fill = fill_event_dir, .type = DIRECTORY_FILE},

    {.tag = CONTENT_FILE_TAG, .parent_tags = TAGS(EVENT_DIR_TAG), .filename = k_content_filename, .fetch_data = get_event_content_data, .type = DATA_FILE},
    {.tag = KIND_FILE_TAG, .parent_tags = TAGS(EVENT_DIR_TAG), .filename = k_kind_filename, .fetch_data = get_event_kind_data, .type = DATA_FILE},
    {.tag = PUBKEY_FILE_TAG, .parent_tags = TAGS(EVENT_DIR_TAG), .filename = k_pubkey_filename, .fetch_data = get_event_pubkey_data, .type = DATA_FILE},
    {.tag = TAGS_DIR_TAG, .parent_tags = TAGS(EVENT_DIR_TAG), .filename = k_tags_dir_name, .fill = fill_tags_dir, .type = DIRECTORY_FILE},

    {.tag = TAG_KEY_DIR_TAG, .parent_tags = TAGS(TAGS_DIR_TAG), .fill = fill_tag_key_dir, .type = DIRECTORY_FILE},

    {.tag = TAG_DIR_TAG, .parent_tags = TAGS(TAG_KEY_DIR_TAG), .fill = fill_tag_values_dir, .type = DIRECTORY_FILE},

    {.tag = TAG_VALUE_FILE_TAG, .parent_tags = TAGS(TAG_DIR_TAG), .fetch_data = get_tag_value, .type = DATA_FILE},

    {.tag = PUBKEY_DIR_TAG, .parent_tags = TAGS(PUBKEYS_DIR_TAG), .fill = fill_pubkey_dir, .type = DIRECTORY_FILE},

    {.tag = PUBKEY_EVENTS_DIR_TAG, .parent_tags = TAGS(PUBKEY_DIR_TAG), .filename = k_pubkeys_events_dir_name, .fill = fill_pubkey_events_dir, .type = DIRECTORY_FILE},
    {.tag = PUBKEY_KINDS_DIR_TAG, .parent_tags = TAGS(PUBKEY_DIR_TAG), .filename = k_pubkey_kinds_dir_name, .fill = fill_pubkey_kinds_dir, .type = DIRECTORY_FILE},
    NULL_FILE
};

void link_files(void) {
    for (int i = 0; files[i].type != NULL_FILE_TYPE; i++) {
        int tags_count = 0;
        const FileTag *parent_tags = files[i].parent_tags;
        for (int j = 0; parent_tags[j] != NULL_TAG; j++) {
            tags_count++;
        }
        if (tags_count != 0) {
            const SyntheticFile **parent_files = malloc(sizeof(SyntheticFile *) * tags_count);
            assert(parent_files != NULL);
            files[i].num_parents = tags_count;

            for (int j = 0; j < tags_count; j++) {
                parent_files[j] = find_file(parent_tags[j]);
                assert(parent_files[j] != NULL);
            }
            files[i].parents = parent_files;
        }
        else {
            files[i].parents = NULL;
        }
    }
}

bool file_matches_path(const SyntheticFile *file, Path path) {
    if (file->parents == NULL) {
        if (is_root_path(path)) {
            return true;
        }
        else {
            return false;
        }
    }
    bool has_expected_parent = false;
    for (int i = 0; i < file->num_parents && has_expected_parent == false; i++) {
        has_expected_parent = file_matches_path(file->parents[i], dirpath(path));
    }
    return 
        has_expected_parent && 
        ((file->filename == NULL) ? true : (strcmp(path_filename(path), file->filename) == 0));
}

const SyntheticFile *find_file(FileTag tag) {
    for (int i = 0; files[i].type != NULL_FILE_TYPE; i++) {
        if (files[i].tag == tag)
            return &files[i];
    }
    return NULL;
}

SyntheticFile *path_to_file(Path path) {
    for (int i = 0; files[i].type != NULL_FILE_TYPE; i++) {
        if (file_matches_path(&files[i], path)) {
            return &files[i];
        }
    }
    return &k_null_file;
}

char *filename_from_path(FileTag tag, Path path) {
    const SyntheticFile *file = find_file(tag);
    if (is_root_path(path)) {
        return NULL;
    }
    if (file_matches_path(file, path)) {
        return path_filename(path);
    }
    else {
        return filename_from_path(tag, dirpath(path));
    }
}

char *pubkey_from_path(Path path) {
    return filename_from_path(PUBKEY_DIR_TAG, path);
}

char *event_id_from_path(Path path) {
    return filename_from_path(EVENT_DIR_TAG, path);
}

char *tag_key_from_path(Path path) {
    return filename_from_path(TAG_KEY_DIR_TAG, path);
}

char *tag_index_from_path(Path path) {
    return filename_from_path(TAG_DIR_TAG, path);
}

char *tag_value_index_from_path(Path path) {
    return filename_from_path(TAG_VALUE_FILE_TAG, path);
}

static int fill_constant_dir(const char *dirnames[], void *buffer, fuse_fill_dir_t filler) {
    for (int i = 0; dirnames[i] != NULL; i++) {
        filler(buffer, dirnames[i], NULL, 0);
    }
    return 0;
}

int fill_pubkey_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    (void) path;

    return fill_constant_dir(k_pubkey_dir_contents_filenames, buffer, filler);
}

int fill_event_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    (void) path;

    return fill_constant_dir(k_event_dir_contents_filenames, buffer, filler);
}

int fill_root_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    (void) path;

    return fill_constant_dir(k_root_dir_contents_filenames, buffer, filler);
}
