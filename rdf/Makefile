# Define paths
RC_BIN := rc

# JSON to TTL transformation
JSON_FILES := $(wildcard json/*.json)
TTL_FILES := $(patsubst json/%.json, ttl/%.ttl, $(JSON_FILES))
ONTOLOGY_FILES := $(wildcard ontology/*.ttl)

# Output RDF Graph
RDF_GRAPH := graph/reality_graph.rdf

# Default target
all: $(TTL_FILES) $(RDF_GRAPH)

# Convert JSON to TTL
ttl/%.ttl: json/%.json
	@echo "🔄 Transforming JSON to TTL: $<"
	@rc --transform $< $@

$(RDF_GRAPH): $(ONTOLOGY_FILES) $(TTL_FILES)
	@echo "🧠 Building RDF Graph..."
	@echo "Running: $(RC_BIN) --graph $(ONTOLOGY_FILES) $(TTL_FILES) --output $@"
	@$(RC_BIN) --graph $(ONTOLOGY_FILES) $(TTL_FILES) --output $@

# Query RDF Graph
query:
	@echo "🔍 Running SPARQL Query..."
	@$(RC_BIN) --query $(RDF_GRAPH) my_query.sparql

# Clean generated files
clean:
	rm -f ttl/*.ttl graph/*.rdf
