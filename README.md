# sqlite3

is a simple high-level wrapper around SQLite3. It doesn’t intend to wrap
everything, but it tries to be useful.

## Installation

```clojure
(load "https://veitheller.de/git/carpentry/sqlite3@0.0.1")
```

## Usage

The module `SQLite3` provides facilities for opening, closing, and querying
databases.

```clojure
(load "https://veitheller.de/git/carpentry/sqlite3@0.0.1")

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

For more information, check out [the
documentation](https://veitheller.de/sqlite3)!

<hr/>

Have fun!
