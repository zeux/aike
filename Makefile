.SUFFIXES:
MAKEFLAGS+=-r

config=debug

BUILD=build/$(config)

COMPILER_SRC=$(wildcard compiler/*.cpp)
COMPILER_BIN=$(BUILD)/aikec
COMPILER_OBJ=$(COMPILER_SRC:%=$(BUILD)/%.o)

$(COMPILER_OBJ): CXXFLAGS=-g -std=c++11 -fno-rtti
$(COMPILER_BIN): LDFLAGS=

RUNTIME_SRC=$(wildcard runtime/*.cpp)
RUNTIME_BIN=$(BUILD)/aike-runtime.so
RUNTIME_OBJ=$(RUNTIME_SRC:%=$(BUILD)/%.o)

$(RUNTIME_OBJ): CXXFLAGS=-g -std=c++11 -fno-rtti -fno-exceptions -fPIC
$(RUNTIME_BIN): LDFLAGS=-shared -ldl

RUNNER_SRC=tests/runner.cpp
RUNNER_BIN=$(BUILD)/runner
RUNNER_OBJ=$(RUNNER_SRC:%=$(BUILD)/%.o)

ifeq ($(LLVMCONFIG),)
LLVMCONFIG:=$(firstword $(shell which llvm-config llvm-config-3.6 /usr/local/opt/llvm/bin/llvm-config))
endif

$(COMPILER_OBJ): CXXFLAGS+=$(shell $(LLVMCONFIG) --cppflags)
$(COMPILER_BIN): LDFLAGS+=$(shell $(LLVMCONFIG) --ldflags)
$(COMPILER_BIN): LDFLAGS+=$(shell $(LLVMCONFIG) --libs all)

ifneq ($(wildcard $(shell $(LLVMCONFIG) --includedir)/lld/.),)
$(COMPILER_OBJ): CXXFLAGS+=-DAIKE_USE_LLD
$(COMPILER_BIN): LDFLAGS+=-llldConfig -llldCore -llldDriver -llldNative -llldPasses -llldReaderWriter -llldYAML
$(COMPILER_BIN): LDFLAGS+=-llldMachO
$(COMPILER_BIN): LDFLAGS+=-llldELF -llldAArch64ELFTarget -llldHexagonELFTarget -llldMipsELFTarget -llldPPCELFTarget -llldX86_64ELFTarget -llldX86ELFTarget
$(COMPILER_BIN): LDFLAGS+=-llldPECOFF
endif

$(COMPILER_BIN): LDFLAGS+=-lz -lcurses -lpthread -ldl

ifeq ($(config),release)
$(COMPILER_OBJ): CXXFLAGS+=-O3

$(RUNTIME_OBJ): CXXFLAGS+=-O3
endif

ifeq ($(config),sanitize)
$(COMPILER_OBJ): CXXFLAGS+=-fsanitize=address
$(COMPILER_BIN): LDFLAGS+=-fsanitize=address

$(RUNTIME_OBJ): CXXFLAGS+=-fsanitize=address
$(RUNTIME_BIN): LDFLAGS+=-fsanitize=address
endif

$(RUNNER_OBJ): CXXFLAGS=-g -std=c++11

OBJECTS=$(COMPILER_OBJ) $(RUNTIME_OBJ) $(RUNNER_OBJ)

TEST_SRC=$(wildcard tests/*/*.aike)
TEST_OUT=$(TEST_SRC:%=$(BUILD)/%.out)

all: $(COMPILER_BIN) $(RUNTIME_BIN) $(RUNNER_BIN)

test: all
	$(COMPILER_BIN) tests/simple.aike -o $(BUILD)/simple $(flags)
	./$(BUILD)/simple

check: all $(TEST_OUT)

clean:
	rm -rf $(BUILD)

$(COMPILER_BIN): $(COMPILER_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

$(RUNTIME_BIN): $(RUNTIME_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

$(RUNNER_BIN): $(RUNNER_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $< $(CXXFLAGS) -c -MMD -MP -o $@

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $< $(CFLAGS) -c -MMD -MP -o $@

$(BUILD)/%.aike.out: %.aike $(COMPILER_BIN) $(RUNTIME_BIN) $(RUNNER_BIN)
	@mkdir -p $(dir $@)
	$(RUNNER_BIN) $< $(BUILD)/$*.aike.o $(COMPILER_BIN) $(flags) --robot
	@touch $@

-include $(OBJECTS:.o=.d)

.PHONY: all test clean