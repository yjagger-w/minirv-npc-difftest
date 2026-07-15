CXX ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS := -Iemulator/include
BUILD_DIR := build
TEST_BINARY := $(BUILD_DIR)/test_minirv
SOURCES := emulator/src/minirv_emu.cpp emulator/tests/test_minirv.cpp
RTL_DIR := rtl
RTL_BUILD_DIR := $(BUILD_DIR)/rtl
RTL_OBJ_DIR := $(RTL_BUILD_DIR)/obj_dir
RTL_BINARY := $(RTL_OBJ_DIR)/Vminirv_core
RTL_VSRCS := $(RTL_DIR)/vsrc/minirv_regfile.v \
	$(RTL_DIR)/vsrc/minirv_decoder.v \
	$(RTL_DIR)/vsrc/minirv_immgen.v \
	$(RTL_DIR)/vsrc/minirv_alu.v \
	$(RTL_DIR)/vsrc/minirv_core.v
RTL_CSRC := $(RTL_DIR)/csrc/minirv_rtl_test.cpp
RTL_HEADERS := $(RTL_DIR)/csrc/minirv_memory.h
RTL_BUILD_COMMAND = verilator --cc --exe --build --trace-fst -Wall \
	--top-module minirv_core --Mdir $(RTL_OBJ_DIR) \
	-CFLAGS "-std=c++17 -Wall -Wextra -Werror -I$(abspath $(RTL_DIR)/csrc)" \
	$(RTL_VSRCS) $(RTL_CSRC)

.PHONY: build test clean rtl-lint rtl-build rtl-test rtl-clean rtl-wave

build: $(TEST_BINARY)

$(TEST_BINARY): $(SOURCES) emulator/include/minirv_emu.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $@

test: build
	./$(TEST_BINARY)

clean:
	rm -rf $(BUILD_DIR)

rtl-lint:
	verilator --lint-only -Wall --top-module minirv_core $(RTL_VSRCS)

rtl-build: $(RTL_BINARY)

$(RTL_BINARY): $(RTL_VSRCS) $(RTL_CSRC) $(RTL_HEADERS)
	@mkdir -p $(RTL_BUILD_DIR)
	$(RTL_BUILD_COMMAND)

rtl-test: rtl-build
	$(RTL_BINARY)

rtl-wave: rtl-build
	$(RTL_BINARY) --fst
	@test -s $(BUILD_DIR)/minirv_rtl.fst

rtl-clean:
	rm -rf $(RTL_BUILD_DIR) $(BUILD_DIR)/minirv_rtl.fst
