module minirv_soc #(
    parameter [31:0] RESET_PC = 32'h00000000
) (
    input         clk,
    input         reset,

    output [31:0] imem_addr,
    input  [31:0] imem_rdata,

    output        ram_valid,
    output        ram_write,
    output [31:0] ram_addr,
    output [31:0] ram_wdata,
    output [3:0]  ram_wmask,
    input  [31:0] ram_rdata,

    input  [31:0] gpio_input,
    output [31:0] gpio_output,
    output        uart_tx_valid,
    output [7:0]  uart_tx_data,
    output        bus_error,

    output        commit_valid,
    output [31:0] commit_pc,
    output [31:0] commit_instr,
    output [31:0] commit_next_pc,
    output        commit_rd_wen,
    output [3:0]  commit_rd,
    output [31:0] commit_rd_data,
    output        commit_mem_wen,
    output [31:0] commit_mem_addr,
    output [3:0]  commit_mem_wmask,
    output [31:0] commit_mem_wdata,
    input  [3:0]  debug_gpr_addr,
    output [31:0] debug_gpr_data,
    output        ebreak_valid,
    output        trap_valid,
    output [3:0]  trap_cause,
    output        halted
);

  wire cpu_dmem_valid;
  wire cpu_dmem_write;
  wire [31:0] cpu_dmem_addr;
  wire [31:0] cpu_dmem_wdata;
  wire [3:0] cpu_dmem_wmask;
  wire [31:0] cpu_dmem_rdata;
  wire gpio_valid;
  wire gpio_write;
  wire [31:0] gpio_addr;
  wire [31:0] gpio_wdata;
  wire [3:0] gpio_wmask;
  wire [31:0] gpio_rdata;
  wire uart_valid;
  wire uart_write;
  wire [31:0] uart_wdata;
  wire [3:0] uart_wmask;

  minirv_core #(.RESET_PC(RESET_PC)) core (
      .clk(clk), .reset(reset),
      .imem_addr(imem_addr), .imem_rdata(imem_rdata),
      .dmem_valid(cpu_dmem_valid), .dmem_write(cpu_dmem_write),
      .dmem_addr(cpu_dmem_addr), .dmem_wdata(cpu_dmem_wdata),
      .dmem_wmask(cpu_dmem_wmask), .dmem_rdata(cpu_dmem_rdata),
      .commit_valid(commit_valid), .commit_pc(commit_pc),
      .commit_instr(commit_instr), .commit_next_pc(commit_next_pc),
      .commit_rd_wen(commit_rd_wen), .commit_rd(commit_rd),
      .commit_rd_data(commit_rd_data), .commit_mem_wen(commit_mem_wen),
      .commit_mem_addr(commit_mem_addr), .commit_mem_wmask(commit_mem_wmask),
      .commit_mem_wdata(commit_mem_wdata),
      .debug_gpr_addr(debug_gpr_addr), .debug_gpr_data(debug_gpr_data),
      .ebreak_valid(ebreak_valid), .trap_valid(trap_valid),
      .trap_cause(trap_cause), .halted(halted)
  );

  minirv_bus bus (
      .cpu_valid(cpu_dmem_valid), .cpu_write(cpu_dmem_write),
      .cpu_addr(cpu_dmem_addr), .cpu_wdata(cpu_dmem_wdata),
      .cpu_wmask(cpu_dmem_wmask), .cpu_rdata(cpu_dmem_rdata),
      .ram_valid(ram_valid), .ram_write(ram_write), .ram_addr(ram_addr),
      .ram_wdata(ram_wdata), .ram_wmask(ram_wmask), .ram_rdata(ram_rdata),
      .gpio_valid(gpio_valid), .gpio_write(gpio_write), .gpio_addr(gpio_addr),
      .gpio_wdata(gpio_wdata), .gpio_wmask(gpio_wmask),
      .gpio_rdata(gpio_rdata),
      .uart_valid(uart_valid), .uart_write(uart_write),
      .uart_wdata(uart_wdata), .uart_wmask(uart_wmask),
      .bus_error(bus_error)
  );

  minirv_uart_tx uart_tx (
      .clk(clk), .reset(reset), .valid(uart_valid), .write(uart_write),
      .wdata(uart_wdata), .wmask(uart_wmask),
      .tx_valid(uart_tx_valid), .tx_data(uart_tx_data)
  );

  minirv_gpio gpio (
      .clk(clk), .reset(reset), .valid(gpio_valid), .write(gpio_write),
      .addr(gpio_addr), .wdata(gpio_wdata), .wmask(gpio_wmask),
      .gpio_input(gpio_input), .rdata(gpio_rdata), .gpio_output(gpio_output)
  );

endmodule
