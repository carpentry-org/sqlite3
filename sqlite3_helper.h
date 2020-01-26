#include "sqlite3.h"

// --- BEGIN HELPERS ---

typedef struct {
  sqlite3* handle;
} SQLite;

typedef struct {
  int tag;
  union {
    int i;
    double f;
    char* s;
  };
} SQLiteColumn;

int SQLiteColumn_tag(SQLiteColumn* col) {
  return col->tag;
}

int SQLiteColumn_from_int(SQLiteColumn col) {
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

SQLiteColumn SQLiteColumn_int(int i) {
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

SQLiteColumn SQLiteColumn_blob(char* s) {
  SQLiteColumn res;
  res.tag = SQLITE_BLOB;
  res.s = s;
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

// --- END HELPERS ---

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
            c->i = sqlite3_column_int(s, i);
            break;
          case SQLITE_FLOAT:
            c->f = sqlite3_column_double(s, i);
            break;
          case SQLITE_TEXT: {
            len = sqlite3_column_bytes(s, i);
            c->s = CARP_MALLOC(len);
            memcpy(c->s, sqlite3_column_text(s, i), len);
            c->s[len] = '\0';
            break;
          }
          case SQLITE_BLOB: {
            len = sqlite3_column_bytes(s, i);
            c->s = CARP_MALLOC(len);
            memcpy(c->s, sqlite3_column_blob(s, i), len);
            c->s[len] = '\0';
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

const char* SQLite3_bind(sqlite3_stmt* s, Array p) {
  int res;
  const char* err = NULL;

  for (int i = 0; i < p.len; i++) {
    SQLiteColumn val = ((SQLiteColumn*)p.data)[i];

    switch (val.tag) {
      case SQLITE_NULL:
        res = sqlite3_bind_null(s, i+1);
        break;
      case SQLITE_INTEGER:
        res = sqlite3_bind_int(s, i+1, val.i);
        break;
      case SQLITE_FLOAT:
        res = sqlite3_bind_double(s, i+1, val.f);
        break;
      case SQLITE_TEXT:
        res = sqlite3_bind_text(s, i+1, val.s, strlen(val.s), SQLITE_STATIC);
        break;
      case SQLITE_BLOB:
        res = sqlite3_bind_blob(s, i+1, val.s, strlen(val.s), SQLITE_STATIC);
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

SQLiteRes SQLite3_exec_c(SQLite* db, const char* stmt, Array p) {
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
  if (s) sqlite3_finalize(s);
  if (n) sqlite3_finalize(n);
  res.is = ERR;
  res.err = err;
  return res;
}

void SQLite3_close_c(SQLite db) {
  sqlite3_close_v2(db.handle);
}

char* SQLite3_error(SQLite db) {
  return (char*)sqlite3_errmsg(db.handle);
}
