#!/bin/bash
DB=state/db.sqlite3

echo "🔍 Detecting first PSI block..."

# Grab first PSI-style subject
PSI=$(sqlite3 "$DB" "SELECT subject FROM triples WHERE subject LIKE '[<:%>' LIMIT 1;")

if [[ -z "$PSI" ]]; then
  echo "❌ Could not detect any PSI block. Aborting."
  exit 1
fi

echo "✅ Found PSI block: $PSI"
echo "⚙️  Seeding missing SourcePlaces with inv:hasContent = 0..."

# Get list of SourcePlaces that are missing content
MISSING=$(sqlite3 "$DB" "
  SELECT subject FROM triples
  WHERE predicate = 'rdf:type' AND object = 'inv:SourcePlace'
  EXCEPT
  SELECT subject FROM triples WHERE predicate = 'inv:hasContent';
")

for src in $MISSING; do
  echo "➕ Seeding $src with 0"
  sqlite3 "$DB" "INSERT INTO triples VALUES('$PSI', '$src', 'inv:hasContent', '0');"
done

echo "✅ Done."
