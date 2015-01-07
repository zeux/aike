BUILD=build

COMPILER_SRC=$(wildcard compiler/*.cpp)
COMPILER_BIN=$(BUILD)/aikec
COMPILER_OBJ=$(COMPILER_SRC:%=$(BUILD)/%.o)

$(COMPILER_OBJ): CXXFLAGS=-g -std=c++11
$(COMPILER_BIN): LDFLAGS=

RUNTIME_SRC=$(wildcard runtime/*.c)
RUNTIME_BIN=$(BUILD)/aike-runtime.so
RUNTIME_OBJ=$(RUNTIME_SRC:%=$(BUILD)/%.o)

$(RUNTIME_OBJ): CFLAGS=-g
$(RUNTIME_BIN): LDFLAGS=-shared

ifeq ($(shell uname),Darwin)
LLVMCONFIG=/usr/local/opt/llvm/bin/llvm-config
else
LLVMCONFIG=llvm-config
endif

$(COMPILER_OBJ): CXXFLAGS+=$(shell $(LLVMCONFIG) --cppflags)
$(COMPILER_BIN): LDFLAGS+=$(shell $(LLVMCONFIG) --ldflags)
$(COMPILER_BIN): LDFLAGS+=$(shell $(LLVMCONFIG) --libs all) -lz -lcurses

OBJECTS=$(COMPILER_OBJ) $(RUNTIME_OBJ)

all: $(COMPILER_BIN) $(RUNTIME_BIN)

test: all
	$(COMPILER_BIN) tests/simple.aike

clean:
	rm -rf $(BUILD)

$(COMPILER_BIN): $(COMPILER_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

$(RUNTIME_BIN): $(RUNTIME_OBJ)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $< $(CXXFLAGS) -c -MMD -MP -o $@

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $< $(CFLAGS) -c -MMD -MP -o $@

-include $(OBJECTS:.o=.d)

.PHONY: all test clean	
