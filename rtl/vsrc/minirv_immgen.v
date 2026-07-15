module minirv_immgen (
    input  [11:0] imm_i_bits,
    input  [6:0]  imm_s_high,
    input  [4:0]  imm_s_low,
    output [31:0] imm_i,
    output [31:0] imm_s
);

  assign imm_i = {{20{imm_i_bits[11]}}, imm_i_bits};
  assign imm_s = {{20{imm_s_high[6]}}, imm_s_high, imm_s_low};

endmodule
