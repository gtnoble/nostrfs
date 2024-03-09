
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <sqlite3.h>

#include "db.h"

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

static void db_bind_id_query(sqlite3_stmt *query, const char *event_id) {
    assert(sqlite3_bind_text(query, 1, event_id, -1, NULL) == SQLITE_OK);
}

long event_creation_time(const char *event_id) {
    db_bind_id_query(get_created_at_query.statement, event_id);
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

    assert(sqlite3_reset(statement) == SQLITE_OK);

    return fill_dir_status;
}

int fill_events_dir(void *buffer, fuse_fill_dir_t filler) {
    return fill_dir(buffer, filler, get_event_ids_query.statement);
}

int fill_tags_dir(void *buffer, fuse_fill_dir_t filler, const char *id) {
    sqlite3_stmt *statement = get_unique_tag_keys_query.statement;
    sqlite3_bind_text(statement, 1, id, -1, NULL);
    return fill_dir(buffer, filler, statement);
}

int fill_tag_key_dir(void *buffer, fuse_fill_dir_t filler, const char *id, const char *key) {
    sqlite3_stmt *statement = get_tag_indices_with_key_query.statement;
    sqlite3_bind_text(statement, 1, id, -1, NULL);
    sqlite3_bind_text(statement, 2, key, -1, NULL);
    return fill_dir(buffer, filler, statement);
}

int fill_tag_values_dir(void *buffer, fuse_fill_dir_t filler, const char *id, int tag_index) {
    sqlite3_stmt *statement = get_tag_value_indices_query.statement;
    sqlite3_bind_text(statement, 1, id, -1, NULL);
    sqlite3_bind_int(statement, 2, tag_index);
    return fill_dir(buffer, filler, statement);
}

int fill_pubkeys_dir(void *buffer, fuse_fill_dir_t filler) {
    sqlite3_stmt *statement = get_pubkeys_query.statement;
    return fill_dir(buffer, filler, statement);
}

int get_tag_value(const char *id, int tag_index, int value_index, char **ret_file_data) {
    sqlite3_stmt *statement = get_tag_value_query.statement;
    sqlite3_bind_text(statement, 1, id, -1, NULL);
    sqlite3_bind_int(statement, 2, tag_index);
    sqlite3_bind_int(statement, 3, value_index);
    return get_file_data(statement, ret_file_data);
}

int get_event_content_data(const char *id, char **ret_file_data) {
    sqlite3_stmt *statement = get_content_query.statement;
    db_bind_id_query(statement, id);
    return get_file_data(statement, ret_file_data);
}

int get_event_kind_data(const char *id, char **ret_file_data) {
    sqlite3_stmt *statement = get_event_kind_query.statement;
    db_bind_id_query(statement, id);
    return get_file_data(statement, ret_file_data);
}

int get_event_pubkey_data(const char *id, char **ret_file_data) {
    sqlite3_stmt *statement = get_event_pubkey_query.statement;
    db_bind_id_query(statement, id);
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
    assert(sqlite3_reset(statement) == SQLITE_OK);
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