#!/bin/bash
DB=state/db.sqlite3

echo "=== ConditionalInvocation Output Places ==="

sqlite3 $DB "
  SELECT t.subject, t.object
  FROM triples t
  WHERE t.predicate = 'inv:Output'
    AND t.subject LIKE 'rule_30_row:%:cond';
" | while read -r subject output; do
  value=$(sqlite3 "$DB" "SELECT object FROM triples WHERE subject = '$output' AND predicate = 'inv:hasContent';")
  echo -n "$output = "
  if [ "$value" ]; then
    echo "$value"
  else
    echo "(unset)"
  fi
done
