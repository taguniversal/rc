CC = gcc
CFLAGS = -Wall -Iexternal/cJSON -Iexternal/serd -Iexternal/mkrand -Iexternal/tinyosc
LDFLAGS = -lcrypto -ldl -lpthread -lsqlite3 -lm 

# For rcnode
rcnode_SRC = src/main.c src/eval.c src/rdf.c external/cJSON/cJSON.c \
   src/udp_send.c external/mkrand/mkrand.c external/tinyosc/tinyosc.c \
   src/osc.c  src/graph.c  src/invocation.c 
rcnode_OBJ = $(patsubst src/%.c, build/src/%.o, $(rcnode_SRC))
rcnode_BIN = build/rcnode

# Ensure build + output directories exist
$(shell mkdir -p build/src output/time_series output/plotly)

all: $(rcnode_BIN) $(serdtest_BIN)

$(serdtest_BIN): $(serdtest_OBJ)
	$(CC) $(serdtest_OBJ) -o $(serdtest_BIN) $(LDFLAGS)

$(rcnode_BIN): $(rcnode_OBJ)
	$(CC) $(rcnode_OBJ) -o $(rcnode_BIN) $(LDFLAGS)

# Ensure each object file is created in the correct directory
build/src/%.o: src/%.c
	mkdir -p $(dir $@)  # Ensure the directory exists
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build output hugo-site/static/plots/*.html

# Time series + Plotly chart generation for multiple components
generate-plotly:
	@echo "📊 Generating time series CSV + Plotly charts..."
	@components="\
		/radar1:hasX \
		/radar1:hasY \
		/xy1:positionX \
		/xy1:positionY \
		/button1:pressedValue \
		/fader5:faderValue \
		/encoder1:rotatedTo \
		/pager1:selectedPage \
		/radio1:selectedOption"; \
	for item in $$components; do \
		subj=$${item%%:*}; \
		pred=$${item##*:}; \
		echo "🔄 Processing: subject=$$subj, predicate=$$pred"; \
		./time_series_query.sh "$$subj" "$$pred"; \
		python3 plot.py "$$pred" "$$subj"; \
	done
	@echo "✅ All plots generated in hugo-site/static/plots/"

# Validate all Turtle RDF files
check-ttl:
	@echo "🧠 Validating Turtle files in ontology..."
	@find ontology -name "*.ttl" | while read file; do \
		echo "🔍 Checking $$file..."; \
		rapper -i turtle -c "$$file" || exit 1; \
	done
	@echo "✅ All TTL files are valid."

.PHONY: all clean generate-plotly check-ttl
