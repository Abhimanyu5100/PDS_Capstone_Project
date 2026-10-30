# Plutus Fincorp - OS edition
#   make            build everything into bin/
#   make test       build and run the unit + integration tests
#   make debug      build with AddressSanitizer + UndefinedBehaviorSanitizer
#   make tsan       build with ThreadSanitizer
#   make clean

CC      ?= cc
CSTD    := -std=c11 -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE
WARN    := -Wall -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes -Wno-unused-parameter
OPT     ?= -O2 -g
CFLAGS  += $(CSTD) $(WARN) $(OPT) -pthread
LDFLAGS += -pthread
LDLIBS  += -lm

BUILD := build
BIN   := bin

# everything except the program entry points goes into libplutus.a
LIB_SRC := $(wildcard src/common/*.c src/mem/*.c src/sched/*.c src/deadlock/*.c \
                      src/storage/*.c src/bank/*.c) \
           $(filter-out src/server/main.c,$(wildcard src/server/*.c))
LIB_OBJ := $(LIB_SRC:%.c=$(BUILD)/%.o)
LIB     := $(BUILD)/libplutus.a

PROGS := $(BIN)/plutusd $(BIN)/plutus $(BIN)/plutus-admin $(BIN)/plutus-load
TESTS := $(patsubst tests/%.c,$(BIN)/%,$(wildcard tests/test_*.c))

.PHONY: all test unit itest debug tsan clean
all: $(PROGS) $(TESTS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJ)
	@rm -f $@
	ar rcs $@ $^

$(BIN)/plutusd: $(BUILD)/src/server/main.o $(LIB)
	@mkdir -p $(BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/plutus: $(BUILD)/src/client/plutus.o $(LIB)
	@mkdir -p $(BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/plutus-admin: $(BUILD)/src/client/plutus_admin.o $(LIB)
	@mkdir -p $(BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/plutus-load: $(BUILD)/src/client/plutus_load.o $(LIB)
	@mkdir -p $(BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/test_%: $(BUILD)/tests/test_%.o $(BUILD)/tests/testlib.o $(LIB)
	@mkdir -p $(BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

unit: $(TESTS)
	@set -e; for t in $(filter-out $(BIN)/test_integration,$(TESTS)); do $$t; done

itest: all
	@$(BIN)/test_integration

test: unit itest

debug:
	$(MAKE) OPT="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
	        LDFLAGS="-pthread -fsanitize=address,undefined" BUILD=build-asan BIN=bin-asan

tsan:
	$(MAKE) OPT="-O1 -g -fsanitize=thread" LDFLAGS="-pthread -fsanitize=thread" \
	        BUILD=build-tsan BIN=bin-tsan

clean:
	rm -rf build build-asan build-tsan bin bin-asan bin-tsan

-include $(LIB_OBJ:.o=.d) $(BUILD)/src/server/main.d $(BUILD)/src/client/*.d $(BUILD)/tests/*.d
