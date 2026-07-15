module minirv_regfile (
    input         clk,
    input         reset,
    input  [3:0]  rs1_addr,
    input  [3:0]  rs2_addr,
    output [31:0] rs1_data,
    output [31:0] rs2_data,
    input         rd_wen,
    input  [3:0]  rd_addr,
    input  [31:0] rd_data,
    input  [3:0]  debug_addr,
    output [31:0] debug_data
);

  reg [31:0] registers [0:15];
  integer index;

  assign rs1_data = (rs1_addr == 4'd0) ? 32'd0 : registers[rs1_addr];
  assign rs2_data = (rs2_addr == 4'd0) ? 32'd0 : registers[rs2_addr];
  assign debug_data = (debug_addr == 4'd0) ? 32'd0 : registers[debug_addr];

  always @(posedge clk) begin
    if (reset) begin
      for (index = 0; index < 16; index = index + 1) begin
        registers[index] <= 32'd0;
      end
    end else begin
      if (rd_wen && (rd_addr != 4'd0)) begin
        registers[rd_addr] <= rd_data;
      end
      registers[0] <= 32'd0;
    end
  end

endmodule
