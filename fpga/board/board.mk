include Makefile
.DEFAULT_GOAL := board-test
BOARD_SRCS := $(SOC_VSRCS) fpga/board/minirv_fpga_memory.sv fpga/board/minirv_fpga_system.sv fpga/board/minirv_pynq_z2.sv
# FPGA INIT values and behavioral clock/test counters intentionally have startup values.
BOARD_FLAGS := --timing --assert -Wall -Wno-DECLFILENAME -Wno-PINCONNECTEMPTY -Wno-UNUSEDSIGNAL -Wno-TIMESCALEMOD -Wno-PROCASSINIT
.PHONY: board-test board-lint
board-lint:
	verilator --lint-only $(BOARD_FLAGS) --top-module minirv_board_tb $(BOARD_SRCS) fpga/board/minirv_board_tb.sv
board-test:
	@mkdir -p build/fpga-board
	verilator --binary $(BOARD_FLAGS) --top-module minirv_board_tb --Mdir build/fpga-board $(BOARD_SRCS) fpga/board/minirv_board_tb.sv
	./build/fpga-board/Vminirv_board_tb
