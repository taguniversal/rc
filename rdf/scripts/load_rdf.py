import sys
from rdflib import Graph, Namespace

# Define namespace
EX = Namespace("http://example.org/reality-compiler#")

def load_ttl(files):
    g = Graph()
    g.bind("ex", EX)

    # Load each TTL file
    for file in files:
        print(f"📂 Loading {file}...")
        g.parse(file, format="turtle")

    print("✅ RDF Graph loaded with", len(g), "triples.")

    return g

def run_query(graph, query):
    print("\n🔍 Running SPARQL Query...")
    results = graph.query(query)

    for row in results:
        print(row)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python load_rdf.py <file1.ttl> <file2.ttl> ...")
        sys.exit(1)

    graph = load_ttl(sys.argv[1:])

    # Example Query: Get all entities
    sample_query = """
    PREFIX ex: <http://example.org/reality-compiler#>
    SELECT ?subject ?predicate ?object
    WHERE { ?subject ?predicate ?object }
    """

    run_query(graph, sample_query)
