
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <sqlite3.h>

#include "db.h"
#include "path.h"
#include "synthetic_file.h"

typedef struct {
    const char *template;
    sqlite3_stmt *statement;
} Query;

sqlite3 *db;

#define NEW_QUERY(template) {template, 0}

static Query get_event_ids_query = NEW_QUERY("SELECT id FROM nostrEvents;");
static Query get_event_query = NEW_QUERY("SELECT * FROM nostrEvents WHERE id = ?;");
static Query get_created_at_query = NEW_QUERY("SELECT created_at FROM nostrEvents WHERE id = ?;");
static Query get_content_query = NEW_QUERY("SELECT content FROM nostrEvents WHERE id = ?;");
static Query get_event_kind_query = NEW_QUERY("SELECT kind FROM nostrEvents WHERE id = ?;");
static Query get_event_pubkey_query = NEW_QUERY("SELECT pubkey FROM nostrEvents WHERE id = ?;");
static Query get_unique_tag_keys_query = NEW_QUERY("SELECT DISTINCT key FROM tags WHERE id = ?;");
static Query get_tag_indices_with_key_query = NEW_QUERY("SELECT DISTINCT tag_index FROM tags WHERE id = ? AND key = ?;");
static Query get_tag_value_indices_query = NEW_QUERY("SELECT value_index FROM tags WHERE id = ? AND tag_index = ?;");
static Query get_tag_value_query = NEW_QUERY("SELECT value FROM tags WHERE id = ? AND tag_index = ? AND value_index = ?;");

static Query get_pubkeys_query = NEW_QUERY("SELECT DISTINCT pubkey FROM nostrEvents;");
static Query get_pubkey_event_ids_query = NEW_QUERY("SELECT id FROM nostrEvents WHERE pubkey = ?;");
static Query get_pubkey_event_kinds_query = NEW_QUERY("SELECT DISTINCT kind FROM nostrEvents WHERE pubkey = ?;");
static Query get_pubkey_kind_events_query = NEW_QUERY("SELECT id FROM nostrEvents WHERE pubkey = ? AND kind = ?;");

static Query *all_queries[] = {
    &get_event_ids_query,
    &get_event_query,
    &get_created_at_query,
    &get_content_query,
    &get_event_kind_query,
    &get_event_pubkey_query,
    &get_unique_tag_keys_query,
    &get_tag_indices_with_key_query,
    &get_tag_value_indices_query,
    &get_tag_value_query,
    &get_pubkeys_query,
    &get_pubkey_event_ids_query,
    &get_pubkey_event_kinds_query,
    &get_pubkey_kind_events_query,
    NULL
};

static int get_file_data(sqlite3_stmt *statement, char **ret_file_data);

static void statement_bind_text(sqlite3_stmt *statement, int index, const char *text) {
    assert(statement != NULL);
    assert(text != NULL);
    const bool binding_successful = sqlite3_bind_text(statement, index, text, -1, NULL) == SQLITE_OK;
    assert(binding_successful);
}

static void statement_bind_number(sqlite3_stmt *statement, int index, char *number) {
    assert(statement != NULL);
    assert(number != NULL);
    const bool binding_successful = sqlite3_bind_int(statement, index, atoi(number)) == SQLITE_OK;
    assert(binding_successful);
}

static void statement_bind_id(sqlite3_stmt *query, Path path) {
    statement_bind_text(query, 1, event_id_from_path(path));
}

long event_creation_time(const char *event_id) {
    statement_bind_text(get_created_at_query.statement, 1, event_id);
    long time;
    int step_status = sqlite3_step(get_created_at_query.statement);
    if (step_status == SQLITE_ROW) {
        time = sqlite3_column_int64(get_created_at_query.statement, 0);
    }
    else if (step_status == SQLITE_DONE) {
        time = ENOENT;
    }
    else {
        fprintf(stderr, "Could not query event creation time: %s", sqlite3_errmsg(db));
        exit(1);
    }
    sqlite3_reset(get_created_at_query.statement);
    return time;
}

void prepare_query(Query *query) {
    if (sqlite3_prepare_v2(
        db, 
        query->template, 
        -1,
        &query->statement, 
        NULL
    ) != SQLITE_OK) {
        fprintf(
            stderr,
            "Error preparing statement: \"%s\" error message: \"%s\"\n",
            query->template,
            sqlite3_errmsg(db)
        );
        exit(1);
    }
}

static int fill_dir(void *buffer, fuse_fill_dir_t filler, sqlite3_stmt *statement) {
    int stepstatus;
    while ((stepstatus = sqlite3_step(statement)) == SQLITE_ROW) {
        const char *event_id;
        event_id = (const char *) sqlite3_column_text(statement, 0);
        filler(buffer, event_id, NULL, 0);
    }

    int fill_dir_status;
    if (stepstatus != SQLITE_DONE) {
        fprintf(
            stderr, 
            "Error reading directory: error code %d: error message: %s", 
            sqlite3_extended_errcode(db), 
            sqlite3_errmsg(db)
        );
        fill_dir_status = -EINVAL;
    }
    else {
        fill_dir_status = 0;
    }

    const bool reset_successful = sqlite3_reset(statement) == SQLITE_OK;
    assert(reset_successful);

    return fill_dir_status;
}

int fill_events_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    (void) path;

    return fill_dir(buffer, filler, get_event_ids_query.statement);
}

int fill_tags_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    sqlite3_stmt *statement = get_unique_tag_keys_query.statement;
    statement_bind_text(statement, 1, event_id_from_path(path));

    return fill_dir(buffer, filler, statement);
}

int fill_tag_key_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    sqlite3_stmt *statement = get_tag_indices_with_key_query.statement;
    statement_bind_text(statement, 1, event_id_from_path(path));
    statement_bind_text(statement, 2, tag_key_from_path(path));

    return fill_dir(buffer, filler, statement);
}

int fill_tag_values_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    sqlite3_stmt *statement = get_tag_value_indices_query.statement;
    statement_bind_text(statement, 1, event_id_from_path(path));
    statement_bind_number(statement, 2, tag_index_from_path(path));

    return fill_dir(buffer, filler, statement);
}

int fill_pubkeys_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    (void) path;

    sqlite3_stmt *statement = get_pubkeys_query.statement;
    return fill_dir(buffer, filler, statement);
}

int fill_pubkey_events_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    sqlite3_stmt *statement = get_pubkey_event_ids_query.statement;
    statement_bind_text(statement, 1, pubkey_from_path(path));

    return fill_dir(buffer, filler, statement);
}

int fill_pubkey_kinds_dir(Path path, void *buffer, fuse_fill_dir_t filler) {
    sqlite3_stmt *statement = get_pubkey_event_kinds_query.statement;
    statement_bind_text(statement, 1, pubkey_from_path(path));

    return fill_dir(buffer, filler, statement);
}

int get_tag_value(Path path, char **ret_file_data) {
    sqlite3_stmt *statement = get_tag_value_query.statement;

    statement_bind_text(statement, 1, event_id_from_path(path));
    statement_bind_number(statement, 2, tag_index_from_path(path));
    statement_bind_number(statement, 3, tag_value_index_from_path(path));

    return get_file_data(statement, ret_file_data);
}

int get_event_content_data(Path path, char **ret_file_data) {
    sqlite3_stmt *statement = get_content_query.statement;
    statement_bind_id(statement, path);
    return get_file_data(statement, ret_file_data);
}

int get_event_kind_data(Path path, char **ret_file_data) {
    sqlite3_stmt *statement = get_event_kind_query.statement;
    statement_bind_id(statement, path);
    return get_file_data(statement, ret_file_data);
}

int get_event_pubkey_data(Path path, char **ret_file_data) {
    sqlite3_stmt *statement = get_event_pubkey_query.statement;
    statement_bind_id(statement, path);
    return get_file_data(statement, ret_file_data);
}

static int get_file_data(sqlite3_stmt *statement, char **ret_file_data) {
    assert(statement != NULL);

    int stepstatus = sqlite3_step(statement);
    int readstatus;
    if (stepstatus == SQLITE_ROW) {
        const char *file_data = (const char *) sqlite3_column_text(statement, 0);
        size_t data_length = strlen(file_data);
        *ret_file_data = calloc(data_length + 1, sizeof(char));
        assert(*ret_file_data != NULL);
        strcpy(*ret_file_data, file_data);
        readstatus = 0;
    }
    else if (stepstatus == SQLITE_DONE) {
        readstatus = ENOENT;
    }
    else {
        fprintf(stderr, "Error opening event file: %s", sqlite3_errmsg(db));
        readstatus = EINVAL;
    }
    const bool reset_successful = sqlite3_reset(statement) == SQLITE_OK;
    assert(reset_successful);
    return readstatus;
}


void initialize_db(char *db_file_path) {
    if (sqlite3_open(db_file_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Failed to open database file \"%s\": %s\n", db_file_path, sqlite3_errmsg(db));
        exit(EXIT_FAILURE);
    }

    sqlite3_extended_result_codes(db, 1);

    for (int i = 0; all_queries[i] != NULL; i++) {
        prepare_query(all_queries[i]);
    }

}
