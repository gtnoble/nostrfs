
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <fuse.h>

#include "path.h"


static const size_t k_initial_num_path_components = 20;

static const char *k_events_dir_name = "e";
static const char *k_pubkeys_dir_name = "p";

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


bool is_root_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && path.num_components == 0;
}

bool is_events_dir(Path path) {
    assert(is_valid_path(path));
    return 
        path_exists(path) && 
        is_root_dir(dirpath(path)) && 
        strcmp(path_filename(path), k_events_dir_name) == 0;
}

bool is_event_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_events_dir(dirpath(path));
}

bool is_pubkeys_dir(Path path) {
    assert(is_valid_path(path));
    return 
        path_exists(path) && 
        is_root_dir(dirpath(path)) && 
        strcmp(path_filename(path), k_pubkeys_dir_name) == 0;
}

static bool is_in_event_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_event_dir(dirpath(path));
}

bool is_content_file(Path path) {
    assert(is_valid_path(path));
    return 
        is_in_event_dir(path) && 
        strcmp(path_filename(path), k_content_filename) == 0;
}

bool is_kind_file(Path path) {
    assert(is_valid_path(path));
    return 
        is_in_event_dir(path) && 
        strcmp(path_filename(path), k_kind_filename) == 0;
}

bool is_pubkey_file(Path path) {
    assert(is_valid_path(path));
    return 
        is_in_event_dir(path) && 
        strcmp(path_filename(path), k_pubkey_filename) == 0;
}

bool is_tags_dir(Path path) {
    assert(is_valid_path(path));
    return 
        path_exists(path) && 
        is_in_event_dir(path) && 
        strcmp(path_filename(path), k_tags_filename) == 0;
}

bool is_tag_key_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_tags_dir(dirpath(path));
}

bool is_tag_dir(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_tag_key_dir(dirpath(path));
}

bool is_tag_value_file(Path path) {
    assert(is_valid_path(path));
    return path_exists(path) && is_tag_dir(dirpath(path));
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

int fill_event_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    (void) path;

    filler(buffer, k_content_filename, NULL, 0);
    filler(buffer, k_kind_filename, NULL, 0);
    filler(buffer, k_pubkey_filename, NULL, 0);
    filler(buffer, k_tags_filename, NULL, 0);
    return 0;
}

int fill_root_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    (void) path;

    filler(buffer, k_events_dir_name, NULL, 0);
    filler(buffer, k_pubkeys_dir_name, NULL, 0);
    return 0;
}