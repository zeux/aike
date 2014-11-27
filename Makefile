BUILD=build

SOURCES=$(wildcard bootstrap/*.cpp) $(wildcard llvmaike/*.cpp)
EXECUTABLE=$(BUILD)/aike

CXXFLAGS=-c -g -std=c++11
LDFLAGS=

CXXFLAGS+=-Illvmaike

ifeq ($(shell uname),Darwin)
LLVMCONFIG=/usr/local/opt/llvm/bin/llvm-config
else
LLVMCONFIG=llvm-config
endif

CXXFLAGS+=$(shell $(LLVMCONFIG) --cppflags)
LDFLAGS+=$(shell $(LLVMCONFIG) --ldflags)
LDFLAGS+=$(shell $(LLVMCONFIG) --libs all) -lz -lcurses

OBJECTS=$(SOURCES:%=$(BUILD)/%.o)

all: $(EXECUTABLE)

test: $(EXECUTABLE)
	./$(EXECUTABLE)

clean:
	rm -rf $(BUILD)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD)/%.o: %
	@mkdir -p $(dir $@)
	$(CXX) $< $(CXXFLAGS) -MMD -MP -o $@

-include $(OBJECTS:.o=.d)

.PHONY: all test clean	
