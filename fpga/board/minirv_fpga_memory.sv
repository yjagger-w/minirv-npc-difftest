`timescale 1ns/1ps
module minirv_fpga_rom #(
    parameter integer WORDS = 256,
    parameter [31:0] BASE = 32'h00000000,
    parameter INIT_FILE = "fpga/board/smoke.hex"
) (
    input wire [31:0] addr,
    output wire [31:0] rdata,
    output wire fault
);
  localparam integer AW = $clog2(WORDS);
  (* rom_style = "distributed" *) reg [31:0] contents [0:WORDS-1];
  wire [31:0] offset = addr - BASE;
  wire in_range = offset < WORDS * 4 && addr[1:0] == 0;
  integer i;
  initial begin
    if (WORDS < 2 || (WORDS & (WORDS-1)) != 0) $fatal(1,"ROM WORDS must be a power of two >= 2");
    for (i=0; i<WORDS; i=i+1) contents[i] = 32'd0;
    if (INIT_FILE != "") $readmemh(INIT_FILE, contents);
  end
  assign rdata = in_range ? contents[offset[AW+1:2]] : 32'd0;
  assign fault = !in_range;
endmodule

// Byte-addressed CPU view: mask bit zero always refers to the addressed byte.
// Physical lanes are shifted here. Read is asynchronous; write is at posedge.
module minirv_fpga_ram #(
    parameter integer WORDS = 256,
    parameter [31:0] BASE = 32'h80000000
) (
    input wire clk, reset, valid, write,
    input wire [31:0] addr, wdata,
    input wire [3:0] wmask,
    output wire [31:0] rdata,
    output wire fault
);
  localparam integer AW = $clog2(WORDS);
  (* ram_style = "distributed" *) reg [7:0] lane0 [0:WORDS-1];
  (* ram_style = "distributed" *) reg [7:0] lane1 [0:WORDS-1];
  (* ram_style = "distributed" *) reg [7:0] lane2 [0:WORDS-1];
  (* ram_style = "distributed" *) reg [7:0] lane3 [0:WORDS-1];
  wire [31:0] offset = addr - BASE;
  wire [AW-1:0] index = offset[AW+1:2];
  wire in_range = offset < WORDS * 4;
  wire [31:0] aligned_read = {lane3[index],lane2[index],lane1[index],lane0[index]};
  wire [31:0] shifted_data = wdata << {addr[1:0],3'b000};
  wire [7:0] expanded_mask = {4'b0000,wmask} << addr[1:0];
  wire [3:0] shifted_mask = expanded_mask[3:0];
  // LW/SW are aligned by the core. Byte accesses use only mask[0]/rdata[7:0].
  wire crossing = |expanded_mask[7:4];
  assign rdata = valid && in_range ? aligned_read >> {addr[1:0],3'b000} : 32'd0;
  assign fault = valid && (!in_range || (write && crossing));
  integer i;
  initial begin
    if (WORDS < 2 || (WORDS & (WORDS-1)) != 0) $fatal(1,"RAM WORDS must be a power of two >= 2");
    for (i=0; i<WORDS; i=i+1) begin
      lane0[i]=0; lane1[i]=0; lane2[i]=0; lane3[i]=0;
    end
  end
  // Soft reset inhibits stores but intentionally preserves RAM. No array reset
  // is used: FPGA configuration initializes it, allowing LUTRAM inference.
  always @(posedge clk) begin
    if (!reset && valid && write && in_range && !crossing) begin
      if (shifted_mask[0]) lane0[index] <= shifted_data[7:0];
      if (shifted_mask[1]) lane1[index] <= shifted_data[15:8];
      if (shifted_mask[2]) lane2[index] <= shifted_data[23:16];
      if (shifted_mask[3]) lane3[index] <= shifted_data[31:24];
    end
  end
endmodule
