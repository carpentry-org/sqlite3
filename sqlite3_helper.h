#include "sqlite3.h"

typedef struct {
  sqlite3* handle;
} SQLite;

typedef struct {
  int tag;
  int blob_len;
  union {
    int64_t i;
    double f;
    char* s;
  };
} SQLiteColumn;

int SQLiteColumn_tag(SQLiteColumn* col) {
  return col->tag;
}

int64_t SQLiteColumn_from_int(SQLiteColumn col) {
  return col.i;
}

double SQLiteColumn_from_float(SQLiteColumn col) {
  return col.f;
}

char* SQLiteColumn_from_str(SQLiteColumn col) {
  return col.s;
}

SQLiteColumn SQLiteColumn_nil() {
  SQLiteColumn res;
  res.tag = SQLITE_NULL;
  return res;
}

SQLiteColumn SQLiteColumn_int(int64_t i) {
  SQLiteColumn res;
  res.tag = SQLITE_INTEGER;
  res.i = i;
  return res;
}

SQLiteColumn SQLiteColumn_float(double f) {
  SQLiteColumn res;
  res.tag = SQLITE_FLOAT;
  res.f = f;
  return res;
}

SQLiteColumn SQLiteColumn_text(char* s) {
  SQLiteColumn res;
  res.tag = SQLITE_TEXT;
  res.s = s;
  return res;
}

SQLiteColumn SQLiteColumn_blob(Array a) {
  SQLiteColumn res;
  res.tag = SQLITE_BLOB;
  res.blob_len = a.len;
  res.s = a.data;
  return res;
}

Array SQLiteColumn_from_blob(SQLiteColumn col) {
  Array res;
  res.len = col.blob_len;
  res.capacity = col.blob_len;
  res.data = col.s;
  return res;
}

/* delete/copy implement the Carp interfaces of the same name, so they carry the
 * mangled path of the Carp binding (SQLite3.SQLiteColumn.delete) rather than the
 * SQLiteColumn_* convention used by the directly-called bindings above. */
void SQLite3_SQLiteColumn_delete(SQLiteColumn col) {
  if ((col.tag == SQLITE_TEXT || col.tag == SQLITE_BLOB) && col.s) {
    CARP_FREE(col.s);
  }
}

SQLiteColumn SQLite3_SQLiteColumn_copy(SQLiteColumn* col) {
  SQLiteColumn res = *col;
  if (col->s && (col->tag == SQLITE_TEXT || col->tag == SQLITE_BLOB)) {
    int len = col->tag == SQLITE_TEXT ? (int)strlen(col->s) + 1 : col->blob_len;
    res.s = CARP_MALLOC(len);
    memcpy(res.s, col->s, len);
  }
  return res;
}

typedef struct {
  int columns;
  SQLiteColumn* data;
} SQLiteRow;

int SQLiteRow_length(SQLiteRow* row) {
  return row->columns;
}

SQLiteColumn SQLiteRow_nth(SQLiteRow* row, int i) {
  return row->data[i];
}

typedef struct {
  int capacity;
  int len;
  SQLiteRow* rows;
} SQLiteRows;

SQLiteRow* SQLiteRows_next_row(SQLiteRows* rows) {
  SQLiteRow* res;
  if (rows->capacity <= rows->len) {
    if (!(rows->capacity)) rows->capacity = 10;
    else rows->capacity *= 2;
    rows->rows = realloc(rows->rows, (rows->capacity)*sizeof(SQLiteRow));
  }
  res = (rows->rows)+(rows->len);
  rows->len++;
  return res;
}

void SQLiteRows_finalize(SQLiteRows* rows) {
  rows->capacity = rows->len;
  rows->rows = realloc(rows->rows, (rows->capacity)*sizeof(SQLiteRow));
}

SQLiteRows SQLiteRows_new_rows() {
  SQLiteRows res;
  res.len = 0;
  res.capacity = 0;
  res.rows = NULL;
  return res;
}

/* Frees only the container arrays: the per-row column array and the row array
 * itself. The per-column text/blob buffers are left alone — when a result is
 * handed back to Carp those buffers are moved into Carp values. The row array
 * is grown with realloc, so it is released with the matching free. */
static void SQLiteRows_free_containers(SQLiteRows* rows) {
  for (int i = 0; i < rows->len; i++) CARP_FREE(rows->rows[i].data);
  free(rows->rows);
}

/* Frees a result set that never reaches Carp (the error paths): the column
 * buffers as well as the containers. */
static void SQLiteRows_free_all(SQLiteRows* rows) {
  for (int i = 0; i < rows->len; i++) {
    SQLiteRow* row = rows->rows + i;
    for (int j = 0; j < row->columns; j++) {
      int tag = row->data[j].tag;
      if ((tag == SQLITE_TEXT || tag == SQLITE_BLOB) && row->data[j].s) {
        CARP_FREE(row->data[j].s);
      }
    }
  }
  SQLiteRows_free_containers(rows);
}

typedef struct {
  int is;
  union {
    const char* err;
    SQLiteRows rows;
  };
} SQLiteRes;
#define OK  0
#define ERR 1

int SQLiteRes_length(SQLiteRes* r) {
  return r->rows.len;
}

SQLiteRow SQLiteRes_nth(SQLiteRes* r, int i) {
  return r->rows.rows[i];
}

bool SQLiteRes_is_ok(SQLiteRes* r) {
  return r->is == OK;
}

char* SQLiteRes_error(SQLiteRes r) {
  return (char*)r.err;
}

/* Called by Carp once a successful result has been turned into Carp values by
 * to-array; the column buffers are owned by Carp at that point, so only the
 * container arrays are freed here. */
void SQLite3_SQLiteRes_delete(SQLiteRes r) {
  if (r.is == OK) SQLiteRows_free_containers(&r.rows);
}

/* sqlite3_errmsg returns memory owned by SQLite that is invalidated by the next
 * call into the library (finalize, reset, …). Copy it into a Carp-owned string
 * so it stays valid after we tear the statement down. */
static char* SQLite3_copy_errmsg(const char* msg) {
  if (!msg) msg = "";
  size_t len = strlen(msg);
  char* copy = CARP_MALLOC(len + 1);
  memcpy(copy, msg, len + 1);
  return copy;
}

SQLite SQLite3_init() {
  SQLite res;
  res.handle = NULL;
  return res;
}

int SQLite3_open_c(SQLite* db, const char* filename) {
  sqlite3* c;
  int res = sqlite3_open(filename, &c);
  db->handle = c;
  return res;
}

const char* SQLite3_exec_internal(sqlite3_stmt* s, SQLiteRows* rows) {
  int status;
  int len;
  const char* err = NULL;
  int count = sqlite3_column_count(s);

  do {
    status = sqlite3_step(s);

    if (status == SQLITE_ROW) {
      SQLiteRow* row = SQLiteRows_next_row(rows);
      row->columns = count;
      row->data = CARP_MALLOC(count*sizeof(SQLiteColumn));

      for (int i = 0; i < count; i++) {
        SQLiteColumn* c = row->data+i;
        c->tag = sqlite3_column_type(s, i);
        switch(c->tag) {
          case SQLITE_INTEGER:
            c->i = sqlite3_column_int64(s, i);
            break;
          case SQLITE_FLOAT:
            c->f = sqlite3_column_double(s, i);
            break;
          case SQLITE_TEXT: {
            len = sqlite3_column_bytes(s, i);
            c->s = CARP_MALLOC(len+1);
            memcpy(c->s, sqlite3_column_text(s, i), len);
            c->s[len] = '\0';
            break;
          }
          case SQLITE_BLOB: {
            len = sqlite3_column_bytes(s, i);
            c->blob_len = len;
            c->s = CARP_MALLOC(len);
            memcpy(c->s, sqlite3_column_blob(s, i), len);
            break;
          }
          case SQLITE_NULL:
            break;
        }
      }
    }
  } while (status == SQLITE_ROW);

  if (status != SQLITE_DONE) {
      sqlite3* db = sqlite3_db_handle(s);
      err = sqlite3_errmsg(db);
  }

  SQLiteRows_finalize(rows);

  return err;
}

static const char* SQLite3_exec_ignore(sqlite3_stmt* s) {
    int status;
    const char* ret = NULL;
    do { status = sqlite3_step(s); } while (status == SQLITE_ROW);

    /* Check for errors */
    if (status != SQLITE_DONE) {
        sqlite3* db = sqlite3_db_handle(s);
        ret = sqlite3_errmsg(db);
    }
    return ret;
}

const char* SQLite3_bind(sqlite3_stmt* s, Array* p) {
  int res;
  const char* err = NULL;

  for (int i = 0; i < p->len; i++) {
    SQLiteColumn val = ((SQLiteColumn*)p->data)[i];

    switch (val.tag) {
      case SQLITE_NULL:
        res = sqlite3_bind_null(s, i+1);
        break;
      case SQLITE_INTEGER:
        res = sqlite3_bind_int64(s, i+1, (sqlite3_int64)val.i);
        break;
      case SQLITE_FLOAT:
        res = sqlite3_bind_double(s, i+1, val.f);
        break;
      case SQLITE_TEXT:
        res = sqlite3_bind_text(s, i+1, val.s, strlen(val.s), SQLITE_STATIC);
        break;
      case SQLITE_BLOB:
        res = sqlite3_bind_blob(s, i+1, val.s, val.blob_len, SQLITE_STATIC);
        break;
    }
    if (res != SQLITE_OK) {
      sqlite3* db = sqlite3_db_handle(s);
      err = sqlite3_errmsg(db);
    }
    if (err) break;
  }

  return err;
}

SQLiteRes SQLite3_exec_c(SQLite* db, const char* stmt, Array* p) {
  sqlite3_stmt* s = NULL;
  sqlite3_stmt* n = NULL;
  const char* err;
  SQLiteRes res;
  res.is = OK;
  res.rows = SQLiteRows_new_rows();

  do {
    if (sqlite3_prepare_v2(db->handle, stmt, -1, &n, &stmt) != SQLITE_OK) {
      err = sqlite3_errmsg(db->handle);
      goto err;
    } else {
      if (n) {
        err = SQLite3_bind(n, p);
        if (err) goto err;
      }
    }

    if (s) {
      err = n ? SQLite3_exec_ignore(s) : SQLite3_exec_internal(s, &(res.rows));
      if (err) goto err;
    }
    if (s) sqlite3_finalize(s);
    s = n;
    n = NULL;
  } while (s);

  return res;
err:
  SQLiteRows_free_all(&res.rows);
  res.is = ERR;
  res.err = SQLite3_copy_errmsg(err);
  if (s) sqlite3_finalize(s);
  if (n) sqlite3_finalize(n);
  return res;
}

void SQLite3_close_c(SQLite db) {
  sqlite3_close_v2(db.handle);
}

int64_t SQLite3_last_insert_rowid(SQLite* db) {
  return (int64_t)sqlite3_last_insert_rowid(db->handle);
}

int SQLite3_changes(SQLite* db) {
  return sqlite3_changes(db->handle);
}

char* SQLite3_error_and_close(SQLite db) {
  char* copy = SQLite3_copy_errmsg(sqlite3_errmsg(db.handle));
  sqlite3_close_v2(db.handle);
  return copy;
}

typedef struct {
  sqlite3_stmt* handle;
} Stmt;

Stmt SQLite3_stmt_init() {
  Stmt res;
  res.handle = NULL;
  return res;
}

int SQLite3_prepare_c(SQLite* db, const char* sql, Stmt* stmt) {
  return sqlite3_prepare_v2(db->handle, sql, -1, &stmt->handle, NULL);
}

char* SQLite3_errmsg_c(SQLite* db) {
  return SQLite3_copy_errmsg(sqlite3_errmsg(db->handle));
}

SQLiteRes SQLite3_exec_prepared_c(Stmt* stmt, Array* p) {
  SQLiteRes res;
  res.is = OK;
  res.rows = SQLiteRows_new_rows();

  const char* err = SQLite3_bind(stmt->handle, p);
  if (err) goto fail;

  err = SQLite3_exec_internal(stmt->handle, &res.rows);
  if (err) goto fail;

  sqlite3_reset(stmt->handle);
  sqlite3_clear_bindings(stmt->handle);
  return res;

fail:
  SQLiteRows_free_all(&res.rows);
  res.is = ERR;
  res.err = SQLite3_copy_errmsg(err);
  sqlite3_reset(stmt->handle);
  sqlite3_clear_bindings(stmt->handle);
  return res;
}

void SQLite3_reset_stmt_c(Stmt* stmt) {
  sqlite3_reset(stmt->handle);
  sqlite3_clear_bindings(stmt->handle);
}

void SQLite3_finalize_stmt_c(Stmt stmt) {
  if (stmt.handle) sqlite3_finalize(stmt.handle);
}
