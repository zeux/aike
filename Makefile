BUILD=build

COMPILER_SRC=$(wildcard compiler/*.cpp)
COMPILER_BIN=$(BUILD)/aikec
COMPILER_OBJ=$(COMPILER_SRC:%=$(BUILD)/%.o)

$(COMPILER_OBJ): CXXFLAGS=-g -std=c++11
$(COMPILER_BIN): LDFLAGS=

RUNTIME_SRC=$(wildcard runtime/*.cpp)
RUNTIME_BIN=$(BUILD)/aike-runtime.so
RUNTIME_OBJ=$(RUNTIME_SRC:%=$(BUILD)/%.o)

$(RUNTIME_OBJ): CXXFLAGS=-g -std=c++11 -fno-rtti -fno-exceptions -fPIC
$(RUNTIME_BIN): LDFLAGS=-shared

ifeq ($(LLVMCONFIG),)
ifeq ($(shell uname),Darwin)
LLVMCONFIG=/usr/local/opt/llvm/bin/llvm-config
else
LLVMCONFIG=llvm-config
endif
endif

$(COMPILER_OBJ): CXXFLAGS+=$(shell $(LLVMCONFIG) --cppflags)
$(COMPILER_BIN): LDFLAGS+=$(shell $(LLVMCONFIG) --ldflags)
$(COMPILER_BIN): LDFLAGS+=$(shell $(LLVMCONFIG) --libs all) -lz -lcurses -lpthread

OBJECTS=$(COMPILER_OBJ) $(RUNTIME_OBJ)

all: $(COMPILER_BIN) $(RUNTIME_BIN)

test: all
	$(COMPILER_BIN) tests/simple.aike -o $(BUILD)/simple.obj
	$(CC) $(BUILD)/simple.obj $(RUNTIME_BIN) -o $(BUILD)/simple
	./$(BUILD)/simple

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
