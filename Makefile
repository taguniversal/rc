CC = gcc
CFLAGS = -Wall -Wextra -Werror=missing-include-dirs -O2 \
         -I/usr/local/include/libmxml4 \
         -Iexternal/cJSON -Iexternal/serd -Iexternal/mkrand -Iexternal/tinyosc \
		 -I/usr/include/libxml2

LDFLAGS = -lcrypto -ldl -lpthread -lsqlite3 -lm -lxml2 -L/usr/local/lib

# Source and object files
rcnode_SRC = src/util.c src/main.c src/eval.c external/cJSON/cJSON.c \
             src/udp_send.c external/mkrand/mkrand.c external/tinyosc/tinyosc.c \
             src/osc.c src/graph.c src/invocation.c src/wiring.c

rcnode_OBJ = $(patsubst %.c, build/%.o, $(rcnode_SRC))
rcnode_BIN = build/rcnode

.PHONY: all clean check-ttl build_dirs
# Default target
all: $(rcnode_BIN)
	@echo "✅ Build completed successfully."



# Create build directories automatically
$(rcnode_OBJ): | build

build:
	mkdir -p build/src 

# Link the final binary
$(rcnode_BIN): $(rcnode_OBJ)
	$(CC) $(rcnode_OBJ) -o $@ $(LDFLAGS)

# Compile each object file
build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -rf build 

# TTL checker
check-ttl:
	@echo "🧠 Validating Turtle files in ontology..."
	@find ontology -name "*.ttl" | while read file; do \
		echo "🔍 Checking $$file..."; \
		rapper -i turtle -c "$$file" || exit 1; \
	done
	@echo "✅ All TTL files are valid."


