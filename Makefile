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
RTL_HEADERS := $(RTL_DIR)/csrc/minirv_memory.h $(RTL_DIR)/csrc/minirv_encoding.h
RTL_BUILD_COMMAND = verilator --cc --exe --build --trace-fst -Wall \
	--top-module minirv_core --Mdir $(RTL_OBJ_DIR) \
	-CFLAGS "-std=c++17 -Wall -Wextra -Werror -I$(abspath $(RTL_DIR)/csrc)" \
	$(RTL_VSRCS) $(RTL_CSRC)
DIFFTEST_DIR := difftest
DIFFTEST_BUILD_DIR := $(BUILD_DIR)/difftest
DIFFTEST_OBJ_DIR := $(DIFFTEST_BUILD_DIR)/obj_dir
DIFFTEST_BINARY := $(DIFFTEST_OBJ_DIR)/Vminirv_core
DIFFTEST_CSRC := $(DIFFTEST_DIR)/minirv_difftest_main.cpp \
	emulator/src/minirv_emu.cpp
DIFFTEST_HEADERS := emulator/include/minirv_emu.h $(RTL_HEADERS)
DIFFTEST_BUILD_COMMAND = verilator --cc --exe --build --trace-fst -Wall \
	--top-module minirv_core --Mdir $(DIFFTEST_OBJ_DIR) \
	-CFLAGS "-std=c++17 -Wall -Wextra -Werror \
	-I$(abspath emulator/include) -I$(abspath $(RTL_DIR)/csrc)" \
	$(RTL_VSRCS) $(DIFFTEST_CSRC)
AM_BUILD_DIR := $(BUILD_DIR)/am
AM_BINARY := $(AM_BUILD_DIR)/Vminirv_core
AM_CSRC := $(RTL_DIR)/csrc/minirv_am_runner.cpp
AM_BUILD_COMMAND = verilator --cc --exe --build --trace-fst -Wall \
	--top-module minirv_core -GRESET_PC=2147483648 --Mdir $(AM_BUILD_DIR) \
	-CFLAGS "-std=c++17 -Wall -Wextra -Werror" \
	$(RTL_VSRCS) $(AM_CSRC)
AM_RUN_IMAGE = $(if $(strip $(IMAGE)),$(strip $(IMAGE)),$(strip $(AM_IMAGE)))
SOC_BUILD_DIR := $(BUILD_DIR)/soc
SOC_OBJ_DIR := $(SOC_BUILD_DIR)/obj_dir
SOC_BINARY := $(SOC_OBJ_DIR)/Vminirv_soc
SOC_VSRCS := $(RTL_VSRCS) rtl/soc/minirv_bus.v \
	rtl/peripheral/minirv_gpio.v rtl/peripheral/minirv_uart_tx.v \
	rtl/soc/minirv_soc.v
SOC_CSRC := rtl/csrc/minirv_soc_test.cpp
SOC_BUILD_COMMAND = verilator --cc --exe --build --trace-fst -Wall \
	--top-module minirv_soc --Mdir $(SOC_OBJ_DIR) \
	-CFLAGS "-std=c++17 -Wall -Wextra -Werror -I$(abspath $(RTL_DIR)/csrc)" \
	$(SOC_VSRCS) $(SOC_CSRC)
SOC_AM_BUILD_DIR := $(BUILD_DIR)/soc-am
SOC_AM_BINARY := $(SOC_AM_BUILD_DIR)/Vminirv_soc
SOC_AM_CSRC := rtl/csrc/minirv_soc_am_runner.cpp
SOC_AM_BUILD_COMMAND = verilator --cc --exe --build --trace-fst -Wall \
	--top-module minirv_soc -GRESET_PC=2147483648 --Mdir $(SOC_AM_BUILD_DIR) \
	-CFLAGS "-std=c++17 -Wall -Wextra -Werror" \
	$(SOC_VSRCS) $(SOC_AM_CSRC)
SOC_AM_RUN_IMAGE = $(if $(strip $(IMAGE)),$(strip $(IMAGE)),$(strip $(AM_IMAGE)))

.PHONY: build test clean rtl-lint rtl-build rtl-test rtl-clean rtl-wave \
	difftest-build difftest-test difftest-wave difftest-clean regression \
	am-build am-run am-wave am-clean soc-lint soc-build soc-test soc-wave soc-clean \
	soc-am-build soc-am-run soc-am-wave soc-am-clean

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

difftest-build: $(DIFFTEST_BINARY)

$(DIFFTEST_BINARY): $(RTL_VSRCS) $(DIFFTEST_CSRC) $(DIFFTEST_HEADERS)
	@mkdir -p $(DIFFTEST_BUILD_DIR)
	$(DIFFTEST_BUILD_COMMAND)

difftest-test: difftest-build
	$(DIFFTEST_BINARY)

difftest-wave: difftest-build
	$(DIFFTEST_BINARY) --fst
	@test -s $(BUILD_DIR)/minirv_difftest.fst

difftest-clean:
	rm -rf $(DIFFTEST_BUILD_DIR) $(BUILD_DIR)/minirv_difftest.fst

regression:
	$(MAKE) test
	$(MAKE) rtl-lint
	$(MAKE) rtl-test
	$(MAKE) difftest-test

am-build: $(AM_BINARY)

$(AM_BINARY): $(RTL_VSRCS) $(AM_CSRC)
	@mkdir -p $(AM_BUILD_DIR)
	$(AM_BUILD_COMMAND)

am-run: am-build
	@test -n "$(AM_RUN_IMAGE)" || \
		(echo "IMAGE is required: make am-run IMAGE=/absolute/path/program.bin"; exit 2)
	$(AM_BINARY) $(AM_RUN_IMAGE)

am-wave: am-build
	@test -n "$(AM_RUN_IMAGE)" || \
		(echo "IMAGE is required: make am-wave IMAGE=/absolute/path/program.bin"; exit 2)
	$(AM_BINARY) $(AM_RUN_IMAGE) --fst
	@test -s $(BUILD_DIR)/minirv_am.fst

am-clean:
	rm -rf $(AM_BUILD_DIR) $(BUILD_DIR)/minirv_am.fst

soc-lint:
	verilator --lint-only -Wall --top-module minirv_soc $(SOC_VSRCS)

soc-build: $(SOC_BINARY)

$(SOC_BINARY): $(SOC_VSRCS) $(SOC_CSRC) $(RTL_HEADERS)
	@mkdir -p $(SOC_BUILD_DIR)
	$(SOC_BUILD_COMMAND)

soc-test: soc-build
	$(SOC_BINARY)

soc-wave: soc-build
	$(SOC_BINARY) --fst
	@test -s $(BUILD_DIR)/minirv_soc.fst

soc-clean:
	rm -rf $(SOC_BUILD_DIR) $(BUILD_DIR)/minirv_soc.fst

soc-am-build: $(SOC_AM_BINARY)

$(SOC_AM_BINARY): $(SOC_VSRCS) $(SOC_AM_CSRC)
	@mkdir -p $(SOC_AM_BUILD_DIR)
	$(SOC_AM_BUILD_COMMAND)

soc-am-run: soc-am-build
	@test -n "$(SOC_AM_RUN_IMAGE)" || \
		(echo "IMAGE is required: make soc-am-run IMAGE=/absolute/path/program.bin"; exit 2)
	$(SOC_AM_BINARY) $(SOC_AM_RUN_IMAGE)

soc-am-wave: soc-am-build
	@test -n "$(SOC_AM_RUN_IMAGE)" || \
		(echo "IMAGE is required: make soc-am-wave IMAGE=/absolute/path/program.bin"; exit 2)
	$(SOC_AM_BINARY) $(SOC_AM_RUN_IMAGE) --fst
	@test -s $(BUILD_DIR)/minirv_soc_am.fst

soc-am-clean:
	rm -rf $(SOC_AM_BUILD_DIR) $(BUILD_DIR)/minirv_soc_am.fst
