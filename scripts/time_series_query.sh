#!/bin/bash

# ========== Inputs ==========
DB_PATH="state/db.sqlite3"
SUBJECT="$1"
PREDICATE="$2"
# ============================

if [[ -z "$SUBJECT" || -z "$PREDICATE" ]]; then
  echo "❌ Usage: $0 <subject> <predicate>"
  exit 1
fi

SAFE_SUBJECT="${SUBJECT//\//_}"
OUTPUT_FILE="output/${PREDICATE}_${SAFE_SUBJECT}.csv"

mkdir -p output

SQL="
.headers on
.mode csv
.output $OUTPUT_FILE
SELECT t2.object AS timestamp, t1.object AS value
FROM triples t1
JOIN triples t2 ON t1.psi = t2.psi
WHERE t1.subject = '$SUBJECT' AND t1.predicate = '$PREDICATE'
  AND t2.predicate = 'timestamp' AND t2.subject = t1.subject
ORDER BY t2.object ASC;
"

echo "📊 Exporting CSV for subject: $SUBJECT, predicate: $PREDICATE"
echo "📄 Output: $OUTPUT_FILE"
echo

sqlite3 "$DB_PATH" <<< "$SQL"

echo "✅ Done!"
