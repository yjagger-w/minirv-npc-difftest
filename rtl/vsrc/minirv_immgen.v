module minirv_immgen (
    input  [11:0] imm_i_bits,
    input  [6:0]  imm_s_high,
    input  [4:0]  imm_s_low,
    input  [31:7] instr,
    output [31:0] imm_i,
    output [31:0] imm_s,
    output [31:0] imm_b,
    output [31:0] imm_u
);

  assign imm_i = {{20{imm_i_bits[11]}}, imm_i_bits};
  assign imm_s = {{20{imm_s_high[6]}}, imm_s_high, imm_s_low};
  assign imm_b = {{19{instr[31]}}, instr[31], instr[7], instr[30:25],
                  instr[11:8], 1'b0};
  assign imm_u = {instr[31:12], 12'd0};

endmodule
