#!/bin/bash
DB=state/db.sqlite3
SEED_BIT=64

# Optional UUID inspection
if [ "$1" ]; then
  UUID="$1"
  echo "=== Inspecting invocation UUID: $UUID ==="

  echo -e "\n🔎 inv:name:"
  sqlite3 $DB "SELECT object FROM triples WHERE subject = '$UUID' AND predicate = 'inv:name';"

  echo -e "\n🔎 inv:ofDefinition:"
  sqlite3 $DB "SELECT object FROM triples WHERE subject = '$UUID' AND predicate = 'inv:ofDefinition';"

  echo -e "\n🔗 SourceList:"
  sqlite3 $DB "SELECT object FROM triples WHERE subject = '$UUID' AND predicate = 'inv:SourceList';" | while read -r src; do
    val=$(sqlite3 $DB "SELECT object FROM triples WHERE subject = '$src' AND predicate = 'inv:hasContent';")
    [ -z "$val" ] && val="NULL"
    echo "  - $src = $val"
  done

  echo -e "\n🎯 DestinationList:"
  sqlite3 $DB "SELECT object FROM triples WHERE subject = '$UUID' AND predicate = 'inv:DestinationList';" | while read -r dst; do
    val=$(sqlite3 $DB "SELECT object FROM triples WHERE subject = '$dst' AND predicate = 'inv:hasContent';")
    [ -z "$val" ] && val="NULL"
    echo "  - $dst = $val"
  done

  echo -e "\n📄 Context (raw JSON):"
  sqlite3 $DB "SELECT object FROM triples WHERE subject = '$UUID' AND predicate = 'context';"

  echo -e "\n🧪 Type Check:"
  sqlite3 $DB "SELECT object FROM triples WHERE subject = '$UUID' AND predicate = 'rdf:type';"

  exit 0
fi

# === Normal inspection begins here ===

echo "=== Injecting signal CA:$SEED_BIT = 1 ==="
sqlite3 $DB "INSERT INTO triples(subject, predicate, object) VALUES('CA:$SEED_BIT', 'inv:hasContent', '1');"

echo "=== Waiting for OSC loop to evaluate... ==="
sleep 0.5

echo "=== Triple Count ==="
sqlite3 $DB 'SELECT COUNT(*) FROM triples;'

echo -e "\n=== Distinct signal values in system (inv:hasContent) ==="
sqlite3 $DB "SELECT DISTINCT object FROM triples WHERE predicate = 'inv:hasContent';"

echo -e "\n=== Seed Row (CA:0..127) ==="
for i in $(seq 0 127); do
  value=$(sqlite3 $DB "SELECT object FROM triples WHERE subject = 'CA:$i' AND predicate = 'inv:hasContent';")
  [ "$value" == "1" ] && echo -n "1" || echo -n "0"
done
echo

echo -e "\n=== Rule 30 Row Output (row_driver:CA_out:0..127) ==="
for i in $(seq 0 127); do
  value=$(sqlite3 $DB "SELECT object FROM triples WHERE subject = 'row_driver:CA_out:$i' AND predicate = 'inv:hasContent';")
  [ "$value" == "1" ] && echo -n "1" || echo -n "0"
done
echo

echo -e "\n=== Indices with Output=1 (row_driver:CA_out:X) ==="
for i in $(seq 0 127); do
  value=$(sqlite3 $DB "SELECT object FROM triples WHERE subject = 'row_driver:CA_out:$i' AND predicate = 'inv:hasContent';")
  [ "$value" == "1" ] && echo -n "$i "
done
echo
echo -e "\n=== Execution Status and Source Content ==="
for gate in AND OR XOR NOT NOR; do
  echo -e "\n🔍 $gate:"

  uuid=$(sqlite3 $DB "SELECT subject FROM triples WHERE predicate = 'inv:name' AND object = '$gate' AND subject != '$gate' LIMIT 1;")

  if [ -z "$uuid" ]; then
    echo "  ⚠️  No active invocation found."
    continue
  fi

  exec=$(sqlite3 $DB "SELECT 1 FROM triples WHERE subject = '$uuid' AND predicate = 'inv:Executing' LIMIT 1;")
  [ "$exec" == "1" ] && echo "  ▶️  Executing: YES" || echo "  ⏸️  Executing: NO"

  echo "  Source Places:"
  sqlite3 $DB "SELECT object FROM triples WHERE subject = '$uuid' AND predicate = 'inv:SourceList';" | while read -r src; do
    val=$(sqlite3 $DB "SELECT object FROM triples WHERE subject = '$src' AND predicate = 'inv:hasContent';")
    [ -z "$val" ] && val="NULL"
    echo "    - $src = $val"
  done
done

