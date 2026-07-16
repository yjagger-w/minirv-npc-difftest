module minirv_uart_tx (
    input         clk,
    input         reset,
    input         valid,
    input         write,
    input  [31:0] wdata,
    input  [3:0]  wmask,
    output reg    tx_valid,
    output reg [7:0] tx_data
);

  always @(posedge clk) begin
    if (reset) begin
      tx_valid <= 1'b0;
      tx_data <= 8'd0;
    end else begin
      tx_valid <= 1'b0;
      if (valid && write && (|wmask)) begin
        tx_valid <= 1'b1;
        if (wmask[0]) begin
          tx_data <= wdata[7:0];
        end else if (wmask[1]) begin
          tx_data <= wdata[15:8];
        end else if (wmask[2]) begin
          tx_data <= wdata[23:16];
        end else begin
          tx_data <= wdata[31:24];
        end
      end
    end
  end

endmodule
