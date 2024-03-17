
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <fuse.h>

#include "path.h"


static const size_t k_initial_num_path_components = 20;

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

bool is_root_path(Path path) {
    return path.num_components == 0;
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
