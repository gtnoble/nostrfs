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

Path *parse_path(const char *raw_path);
void free_path(Path *path);
bool is_root_path(Path path);

char *path_filename(Path path);
Path dirpath(Path path);
Path parent_dir(Path path, unsigned int num_dirs_ascended);
char *parent_dirname(Path path, unsigned int num_dirs_ascended);

#endif
