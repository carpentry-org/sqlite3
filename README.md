# sqlite3

is a simple high-level wrapper around SQLite3. It doesn’t intend to wrap
everything, but it tries to be useful.

## Installation

```clojure
(load "git@git.veitheller.de:carpentry/sqlite3.git@0.1.0")
```

## Usage

The module `SQLite3` provides facilities for opening, closing, and querying
databases.

```clojure
(load "git@git.veitheller.de:carpentry/sqlite3.git@0.1.0")

; opening DBs can fail, for the purposes of this example we
; ignore that
(let-do [db (Result.unsafe-from-success (SQLite3.open "db"))]
  ; we can prepare statements
  (println* &(SQLite3.query &db "INSERT INTO mytable VALUES (?1, ?2);"
                            &[(to-sqlite3 @"hello") (to-sqlite3 100)]))
  ; and query things
  (println* &(SQLite3.query &db "SELECT * from mytable;" &[]))
  (SQLite3.close db)
```

Because `open` and `query` return `Result` types, we could also use
combinators!

### Prepared statements

For a statement you run many times, prepare it once and reuse it. The
`with-prepared` macro handles the whole lifecycle: it prepares the statement,
binds it for the body, and finalizes it on every exit path — even when the body
short-circuits. The `params` macro builds the parameter array so you don't have
to wrap each value in `to-sqlite3` by hand.

```clojure
(let-do [db (Result.unsafe-from-success (SQLite3.open "db"))]
  (ignore
    (SQLite3.with-prepared [stmt &db "INSERT INTO mytable VALUES (?1, ?2);"]
      (do
        (for [i 0 100]
          (ignore (SQLite3.exec-prepared &stmt (SQLite3.params i @"row"))))
        (Result.Success ()))))
  (SQLite3.close db))
```

The body must evaluate to a `Result` (like [`with-transaction`](#usage)):
`with-prepared` returns the prepare error if the statement can't be prepared,
otherwise the `Result` the body produced.

For more information, check out [the
documentation](https://veitheller.de/sqlite3)!

<hr/>

Have fun!
