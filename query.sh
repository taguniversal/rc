#!/bin/sh
sqlite3 state/db.sqlite3 <<EOF
SELECT * FROM triples WHERE subject LIKE 'row_driver:cond%'
EOF

