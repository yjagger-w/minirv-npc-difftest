`timescale 1ns/1ps
module minirv_fpga_system #(
    parameter integer ROM_WORDS=256,
    parameter integer RAM_WORDS=256,
    parameter ROM_INIT_FILE="fpga/board/smoke.hex"
) (
    input wire clk, reset,
    output reg done=0, fault=0,
    output wire [31:0] gpio_output,
    output wire commit_valid,
    output wire [31:0] commit_pc, commit_instr,
    output wire commit_rd_wen,
    output wire [3:0] commit_rd,
    output wire [31:0] commit_rd_data
);
  wire [31:0] imem_addr,imem_rdata,ram_addr,ram_wdata,ram_rdata;
  wire [3:0] ram_wmask;
  wire ram_valid,ram_write,bus_error,rom_fault,ram_fault;
  wire ebreak_valid,trap_valid;
  minirv_fpga_rom #(.WORDS(ROM_WORDS),.INIT_FILE(ROM_INIT_FILE)) rom (
    .addr(imem_addr),.rdata(imem_rdata),.fault(rom_fault));
  minirv_fpga_ram #(.WORDS(RAM_WORDS)) ram (
    .clk(clk),.reset(reset),.valid(ram_valid),.write(ram_write),
    .addr(ram_addr),.wdata(ram_wdata),.wmask(ram_wmask),
    .rdata(ram_rdata),.fault(ram_fault));
  minirv_soc soc (
    .clk(clk),.reset(reset),.imem_addr(imem_addr),.imem_rdata(imem_rdata),
    .ram_valid(ram_valid),.ram_write(ram_write),.ram_addr(ram_addr),
    .ram_wdata(ram_wdata),.ram_wmask(ram_wmask),.ram_rdata(ram_rdata),
    .gpio_input(32'd0),.gpio_output(gpio_output),
    .uart_tx_valid(),.uart_tx_data(),.bus_error(bus_error),
    .commit_valid(commit_valid),.commit_pc(commit_pc),.commit_instr(commit_instr),
    .commit_next_pc(),.commit_rd_wen(commit_rd_wen),.commit_rd(commit_rd),
    .commit_rd_data(commit_rd_data),.commit_mem_wen(),.commit_mem_addr(),
    .commit_mem_wmask(),.commit_mem_wdata(),
    .debug_gpr_addr(4'd0),.debug_gpr_data(),.ebreak_valid(ebreak_valid),
    .trap_valid(trap_valid),.trap_cause(),.halted());
  always @(posedge clk) begin
    if (reset) begin done<=0; fault<=0; end
    else begin
      if (ebreak_valid) done<=1;
      if (trap_valid || bus_error || rom_fault || ram_fault) fault<=1;
    end
  end
endmodule
