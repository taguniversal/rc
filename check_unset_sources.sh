#!/bin/bash
DB=state/db.sqlite3

echo "=== Finding SourcePlaces with (unset) content ==="
sqlite3 -readonly $DB "
  SELECT DISTINCT subject 
  FROM triples 
  WHERE predicate = 'rdf:type' 
    AND object = 'inv:SourcePlace'
    AND subject NOT IN (
      SELECT subject FROM triples WHERE predicate = 'inv:hasContent'
    );
"
