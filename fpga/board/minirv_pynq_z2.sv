`timescale 1ns/1ps
module minirv_pynq_clock (
    input wire clk125, btn_reset,
    output wire cpu_clk, locked
);
`ifdef VERILATOR
  // Behavioral frequency/lock model only. XSim separately checks UNISIM MMCM.
  reg sim_clk=0;
  always #10 sim_clk=~sim_clk;
  reg [3:0] lock_count=0;
  always @(posedge clk125 or posedge btn_reset)
    if (btn_reset) lock_count<=0;
    else if (lock_count!=15) lock_count<=lock_count+1'b1;
  assign cpu_clk=sim_clk;
  assign locked=&lock_count;
`else
  wire input_clk,feedback,feedback_buf,mmcm_clk;
  IBUF ref_input(.I(clk125),.O(input_clk));
  BUFG feedback_global(.I(feedback),.O(feedback_buf));
  BUFG cpu_global(.I(mmcm_clk),.O(cpu_clk));
  // 125 MHz * 8 / 1 = 1000 MHz VCO; /20 = 50 MHz CPU.
  MMCME2_BASE #(.BANDWIDTH("OPTIMIZED"),.CLKIN1_PERIOD(8.0),
    .CLKFBOUT_MULT_F(8.0),.DIVCLK_DIVIDE(1),.CLKOUT0_DIVIDE_F(20.0),
    .STARTUP_WAIT("FALSE")) mmcm (
    .CLKIN1(input_clk),.CLKFBIN(feedback_buf),.CLKFBOUT(feedback),
    .CLKOUT0(mmcm_clk),.LOCKED(locked),.PWRDWN(1'b0),.RST(btn_reset),
    .CLKFBOUTB(),.CLKOUT0B(),.CLKOUT1(),.CLKOUT1B(),.CLKOUT2(),
    .CLKOUT2B(),.CLKOUT3(),.CLKOUT3B(),.CLKOUT4(),.CLKOUT5(),.CLKOUT6());
`endif
endmodule

module minirv_reset_release (
    input wire clk, async_reset,
    output wire reset
);
  (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg [2:0] sync_ff=3'b111;
  always @(posedge clk or posedge async_reset)
    if (async_reset) sync_ff<=3'b111;
    else sync_ff<={sync_ff[1:0],1'b0};
  assign reset=sync_ff[2];
endmodule

module minirv_pynq_z2 #(
    parameter integer HEARTBEAT_BITS=24,
    parameter integer ROM_WORDS=256,
    parameter integer RAM_WORDS=256,
    parameter ROM_INIT_FILE="fpga/board/smoke.hex"
) (
    input wire clk125,
    input wire btn_reset,
    output wire [3:0] led
);
  wire cpu_clk,locked,reset,done,fault;
  wire [31:0] gpio;
  minirv_pynq_clock clocking_unit(.clk125(clk125),.btn_reset(btn_reset),
    .cpu_clk(cpu_clk),.locked(locked));
  minirv_reset_release reset_release(.clk(cpu_clk),
    .async_reset(btn_reset || !locked),.reset(reset));
  minirv_fpga_system #(.ROM_WORDS(ROM_WORDS),.RAM_WORDS(RAM_WORDS),
    .ROM_INIT_FILE(ROM_INIT_FILE)) system (
    .clk(cpu_clk),.reset(reset),.done(done),.fault(fault),.gpio_output(gpio),
    .commit_valid(),.commit_pc(),.commit_instr(),.commit_rd_wen(),
    .commit_rd(),.commit_rd_data());
  reg [HEARTBEAT_BITS-1:0] heartbeat=0;
  always @(posedge cpu_clk)
    if (reset) heartbeat<=0;
    else heartbeat<=heartbeat+1'b1;
  assign led={heartbeat[HEARTBEAT_BITS-1],gpio[0],fault,done};
endmodule
