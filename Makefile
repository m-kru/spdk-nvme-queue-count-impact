PRJ_NAME ?= spdk-nvme-queues

default: build

.PHONY: help
help:
	@echo "Build targets:"
	@echo "  all      Run tidy and build."
	@echo "  build    Build binary."
	@echo "  debug    Build binary with debug symbols."
	@echo "  default  Run build."
	@echo "Quality targets:"
	@echo "  tidy  Lint project with clang-tidy."
	@echo "Other targets:"
	@echo "  clean  Remove all build products."
	@echo "  help   Print help message."
	@echo "  sync   Synchronize files on the server."

CC = gcc

INC_DIRS = $(SRC_DIR) \
	/home/mkru/spdk/build/include/
_INC_FLAGS = $(addprefix -I,$(INC_DIRS))

CFLAGS = $(_INC_FLAGS) -DLOG_COLOR

PKG_CONFIG_PATH = /home/mkru/spdk/build/lib/pkgconfig
SPDK_DPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_nvme spdk_env_dpdk)
SYS_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs --static spdk_syslibs)

LDFLAGS = -lrt \
	-Wl,--whole-archive -Wl,-Bstatic \
	$(SPDK_DPDK_LIB) \
	-Wl,-Bdynamic -Wl,--no-whole-archive \
	$(SYS_LIB)

BUILD_DIR ?= build

SRC_DIR ?= src
SRC_DIRS = $(SRC_DIR) \
	src/args
_SRCS = $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.c))

BIN_DIR ?= $(BUILD_DIR)/bin
OBJ_DIR ?= $(BUILD_DIR)/obj
OBJ_DIRS = $(subst $(SRC_DIR),$(OBJ_DIR),$(SRC_DIRS))
OBJS = $(subst $(SRC_DIR),$(OBJ_DIR),$(subst .c,.o,$(_SRCS)))

# Build targets
all: tidy build

build: $(BIN_DIR)/$(PRJ_NAME)

debug: CFLAGS += -g -O0 -D DEBUG
debug: $(BIN_DIR)/$(PRJ_NAME)

$(BIN_DIR)/$(PRJ_NAME): $(OBJS) | .bin-dir
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)%.o: $(SRC_DIR)%.c | .obj-dirs
	$(CC) -c -Wall $< $(CFLAGS) -o $@

.bin-dir:
	@mkdir -p $(BIN_DIR)

.obj-dirs:
	@mkdir -p $(OBJ_DIRS)

# Quality targets
.PHONY: tidy
tidy:
	clang-tidy $(_SRCS) -- $(_INC_FLAGS)

# Other targets
.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR)

.PHONY: sync
sync:
	./scripts/sync.sh
