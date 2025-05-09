CC = gcc
CFLAGS = -Wall -Wextra -Werror=missing-include-dirs -O2 \
         -Iexternal/cJSON -Iexternal/mkrand -Iexternal/tinyosc \
         -I/usr/include/libxml2

LDFLAGS = -lcrypto -ldl -lpthread -lsqlite3 -lm -lxml2 -L/usr/local/lib

rcnode_SRC = \
  src/main.c \
  src/eval.c \
  src/util.c \
  src/osc.c \
  src/graph.c \
  src/invocation.c \
  src/wiring.c \
  src/udp_send.c \
  src/spirv.c \
  external/cJSON/cJSON.c \
  external/mkrand/mkrand.c \
  external/tinyosc/tinyosc.c

rcnode_OBJ = $(patsubst %.c, build/%.o, $(rcnode_SRC))
rcnode_BIN = build/rcnode

SPIRV_OUT = build/spirv_out
SPIRV_SRC := $(wildcard $(SPIRV_OUT)/*.spvasm)
SPIRV_BIN := $(SPIRV_SRC:.spvasm=.spv)
SPIRV_VALIDATE := $(SPIRV_BIN:.spv=.validated)

.PHONY: all clean check-ttl build_dirs fpga flash spirv spirv-emit validate-spirv

all: $(rcnode_BIN)
	@echo "✅ Build completed successfully."
	@echo "📦 Syncing inv/ snippets to DigitalBlockchain project..."
	cp -r inv /Users/eflores/src/digitalblockchain/

$(rcnode_OBJ): | build

build:
	mkdir -p build/src build/external/cJSON build/external/mkrand build/external/tinyosc $(SPIRV_OUT)

$(rcnode_BIN): $(rcnode_OBJ)
	$(CC) $(rcnode_OBJ) -o $@ $(LDFLAGS)

build/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/external/%.o: external/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# --- SPIR-V compilation pipeline ---

spirv: spirv-emit $(SPIRV_BIN)
	@echo "✅ All SPIR-V binaries assembled."

spirv-emit: $(rcnode_BIN)
	@echo "🔧 Generating SPIR-V assembly from XML..."
	@$(rcnode_BIN) --compile --inv inv --spirv_dir $(SPIRV_OUT)

$(SPIRV_OUT)/%.spv: $(SPIRV_OUT)/%.spvasm
	@echo "⚙️  Assembling $< -> $@"
	spirv-as $< -o $@

validate-spirv: $(SPIRV_VALIDATE)
	@echo "✅ All SPIR-V binaries validated."

$(SPIRV_OUT)/%.validated: $(SPIRV_OUT)/%.spv
	spirv-val $< && touch $@

# --- FPGA flow ---

fpga: build/top.bin
	iceprog build/top.bin

flash:
	iceprog build/top.bin

build/top.json: src/rtl/top.v
	yosys -p "synth_ice40 -top top_module -json build/top.json" $<

build/top.asc: build/top.json
	nextpnr-ice40 --hx8k --package ct256 --json build/top.json --asc build/top.asc

build/top.bin: build/top.asc
	icepack build/top.asc build/top.bin

# --- Cleanup ---

clean:
	rm -rf build
