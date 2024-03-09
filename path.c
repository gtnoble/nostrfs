
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <fuse.h>

#include "path.h"

static const size_t k_initial_num_path_components = 20;

static const char *k_events_dir_name = "e";

static const char *k_tags_filename = "tags";
static const char *k_content_filename = "content";
static const char *k_kind_filename = "kind";
static const char *k_pubkey_filename = "pubkey";

bool is_valid_path(Path path);

Path *parse_path(const char *raw_path) {

    Path *parsed_path = malloc(sizeof(Path));
    if (parsed_path == NULL)
        goto parsed_path_alloc_failure;

    size_t raw_path_size = sizeof(char) * (strlen(raw_path) + 1);
    parsed_path->raw_path = malloc(raw_path_size);
    if (parsed_path->raw_path == NULL)
        goto raw_path_alloc_failure;

    memcpy(parsed_path->raw_path, raw_path, raw_path_size);
    parsed_path->max_num_components = k_initial_num_path_components;

    parsed_path->path_components = malloc(sizeof(char *) * parsed_path->max_num_components);
    if (parsed_path->path_components == NULL)
        goto components_alloc_failure;

    char *saveptr;
    for (int i = 0;; i++) {
        if (i + 1 > parsed_path->max_num_components) {
            parsed_path->max_num_components *= 2;
            parsed_path->path_components = realloc(
                parsed_path->path_components, 
                sizeof(char *) * parsed_path->max_num_components
            );
            if (parsed_path->path_components == NULL)
                goto enlarge_components_failure;
        }
        parsed_path->path_components[i] = strtok_r(i == 0 ? parsed_path->raw_path : NULL, "/", &saveptr);
        if (parsed_path->path_components[i] == NULL) {
            parsed_path->num_components = i;
            return parsed_path;
        }
    }

    enlarge_components_failure:
        free(parsed_path->path_components);
    components_alloc_failure:
        free(parsed_path->raw_path);
    raw_path_alloc_failure:
        free(parsed_path);
    parsed_path_alloc_failure:
        return NULL;
}

void free_path(Path *path) {
    assert(is_valid_path(*path));
    free(path->path_components);
    free(path->raw_path);
    free(path);
}

bool path_exists(Path path) {
    return path.num_components >= 0;
}

bool is_valid_path(Path path) {
    return 
        path.max_num_components >= 0 &&
        path.max_num_components >= path.num_components &&
        path.raw_path != NULL &&
        path.path_components != NULL;
}

Path parent_dir(Path path, unsigned int num_dirs_ascended) {
    assert(is_valid_path(path));
    path.num_components -= num_dirs_ascended;
    return path;
}

char *parent_dirname(Path path, unsigned int num_dirs_ascended) {
    assert(is_valid_path(path));
    return path_filename(parent_dir(path, num_dirs_ascended));
}

Path dirpath(Path path) {
    assert(is_valid_path(path));
    return parent_dir(path, 1);
}

char *path_filename(Path path) {
    assert(is_valid_path(path));
    assert(path.num_components > 0);
    return path.path_components[path.num_components - 1];
}


static bool is_root_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && path.num_components == 0;
}

static bool is_events_dir(Path path) {
    assert(is_valid_path(path));
    return 
        path_exists(path) && 
        is_root_dir(dirpath(path)) && 
        strcmp(path_filename(path), k_events_dir_name) == 0;
}

static bool is_event_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_events_dir(dirpath(path));
}

static bool is_in_event_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_event_dir(dirpath(path));
}

static bool is_tags_dir(Path path) {
    assert(is_valid_path(path));
    return 
        path_exists(path) && 
        is_in_event_dir(path) && 
        strcmp(path_filename(path), k_tags_filename) == 0;
}

static bool is_tag_key_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_tags_dir(dirpath(path));
}

static bool is_tag_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_tag_key_dir(dirpath(path));
}

static bool is_tag_value_file(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_tag_dir(dirpath(path));
}

enum file_type get_file_type(Path path) {
    assert(is_valid_path(path));

    if (is_root_dir(path))
        return ROOT_DIR;
    else if (is_events_dir(path))
        return EVENTS_DIR;
    else if (is_event_dir(path)) {
        return EVENT_DIR;
    }
    else if (is_tags_dir(path)) {
        return TAGS_DIR;
    }
    else if (is_tag_key_dir(path)) {
        return TAG_KEY_DIR;
    }
    else if (is_tag_dir(path)) {
        return TAG_DIR;
    }
    else if (is_tag_value_file(path)) {
        return TAG_VALUE_FILE;
    }
    else if (is_in_event_dir(path))  {
        char *filename = path_filename(path);
        if (strcmp(filename, k_content_filename) == 0) {
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

bool is_directory(enum file_type filetype) {
    return 
        filetype == EVENT_DIR ||
        filetype == ROOT_DIR ||
        filetype == EVENTS_DIR ||
        filetype == TAGS_DIR ||
        filetype == TAG_DIR ||
        filetype == TAG_KEY_DIR;
}

bool is_event_file(enum file_type filetype) {
    switch (filetype) {
        case CONTENT_FILE:
        case KIND_FILE:
        case PUBKEY_FILE:
            return true;
        default:
            return false;
    }
}

bool is_data_file(enum file_type filetype) {
    return is_event_file(filetype) || filetype == TAG_VALUE_FILE;
}

static char *locate_value_in_path(Path path, bool (*path_check)(Path)) {
    if (! path_exists(path)) {
        return NULL;
    }
    else if (path_check(path)) {
        return path_filename(path);
    }
    else {
        return locate_value_in_path(parent_dir(path, 1), path_check);
    }
}

char *event_id_from_path(Path path) {
    return locate_value_in_path(path, is_event_dir);
}

char *tag_key_from_path(Path path) {
    return locate_value_in_path(path, is_tag_key_dir);
}

char *tag_index_from_path(Path path) {
    return locate_value_in_path(path, is_tag_dir);
}

char *tag_value_index_from_path(Path path) {
    return locate_value_in_path(path, is_tag_value_file);
}

bool is_normal_file(enum file_type filetype) {
    return is_event_file(filetype) || filetype == TAG_VALUE_FILE;
}

int fill_event_dir(void *buffer, fuse_fill_dir_t filler) {

    filler(buffer, k_content_filename, NULL, 0);
    filler(buffer, k_kind_filename, NULL, 0);
    filler(buffer, k_pubkey_filename, NULL, 0);
    filler(buffer, k_tags_filename, NULL, 0);
    return 0;
}

int fill_root_dir(void *buffer, fuse_fill_dir_t filler) {
    filler(buffer, k_events_dir_name, NULL, 0);
    return 0;
}