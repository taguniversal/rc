#!/bin/bash
DB=state/db.sqlite3

echo "=== Checking Seed Wiring: CA:X → rule_30_row:X ==="
missing=()

for i in $(seq 0 127); do
  result=$(sqlite3 "$DB" "SELECT subject FROM triples WHERE predicate = 'inv:Inputs' AND object LIKE '%CA:$i%';")
  echo "   ➤ Used by: $result"
  if [ -z "$result" ]; then
    missing+=($i)
  fi
done

if [ ${#missing[@]} -eq 0 ]; then
  echo "✅ All seed cells CA:0..127 are referenced as inputs."
else
  echo "❌ Missing references for these seed cells (not used as inputs):"
  printf "   %s\n" "${missing[@]}"
fi
