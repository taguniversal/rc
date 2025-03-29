CC = gcc
CFLAGS = -Wall -Iexternal/cJSON -Iexternal/serd -Iexternal/sqlite -Iexternal/mkrand -Iexternal/tinyosc
LDFLAGS = -lcrypto  -ldl -lpthread

SRC = src/main.c src/rdf.c external/cJSON/cJSON.c src/udp_send.c external/sqlite/sqlite3.c external/mkrand/mkrand.c external/tinyosc/tinyosc.c
OBJ = $(patsubst %.c, build/%.o, $(SRC))
BIN = build/rc

# Ensure build + output directories exist
$(shell mkdir -p build output/time_series output/plotly)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $(BIN) $(LDFLAGS) 

build/%.o: %.c
	mkdir -p $(dir $@)
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
