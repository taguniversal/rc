import json
import sys
from rdflib import Graph, URIRef, Literal, Namespace

# Define a namespace
EX = Namespace("http://example.org/reality-compiler#")

def json_to_ttl(input_json, output_ttl):
    # Load JSON
    with open(input_json, "r") as f:
        data = json.load(f)

    g = Graph()
    g.bind("ex", EX)

    # Process JSON structure
    for entry in data.get("field", []):
        definition = entry.get("definition", {})
        name = definition.get("name", "Unknown")

        # Create subject node
        node = URIRef(EX[name])

        # Add properties
        for key, values in definition.items():
            if isinstance(values, list):
                for value in values:
                    g.add((node, URIRef(EX[key]), Literal(value)))
            else:
                g.add((node, URIRef(EX[key]), Literal(values)))

    # Save as TTL
    g.serialize(destination=output_ttl, format="turtle")
    print(f"✅ Converted {input_json} -> {output_ttl}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python json_to_ttl.py <input.json> <output.ttl>")
        sys.exit(1)

    json_to_ttl(sys.argv[1], sys.argv[2])
