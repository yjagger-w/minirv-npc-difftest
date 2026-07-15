CXX ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS := -Iemulator/include
BUILD_DIR := build
TEST_BINARY := $(BUILD_DIR)/test_minirv
SOURCES := emulator/src/minirv_emu.cpp emulator/tests/test_minirv.cpp

.PHONY: build test clean

build: $(TEST_BINARY)

$(TEST_BINARY): $(SOURCES) emulator/include/minirv_emu.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $@

test: build
	./$(TEST_BINARY)

clean:
	rm -rf $(BUILD_DIR)
