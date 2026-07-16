module minirv_gpio (
    input         clk,
    input         reset,
    input         valid,
    input         write,
    input  [31:0] addr,
    input  [31:0] wdata,
    input  [3:0]  wmask,
    input  [31:0] gpio_input,
    output [31:0] rdata,
    output [31:0] gpio_output
);

  reg [31:0] output_reg;
  integer byte_index;

  assign gpio_output = output_reg;
  assign rdata = (addr == 32'h10000000) ? gpio_input :
                 (addr == 32'h10000004) ? output_reg : 32'd0;

  always @(posedge clk) begin
    if (reset) begin
      output_reg <= 32'd0;
    end else if (valid && write && (addr == 32'h10000004)) begin
      for (byte_index = 0; byte_index < 4; byte_index = byte_index + 1) begin
        if (wmask[byte_index]) begin
          output_reg[byte_index * 8 +: 8] <= wdata[byte_index * 8 +: 8];
        end
      end
    end
  end

endmodule
