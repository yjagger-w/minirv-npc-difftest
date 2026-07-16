module minirv_decoder (
    input  [31:0] instr,
    output reg    is_add,
    output reg    is_addi,
    output reg    is_lui,
    output reg    is_auipc,
    output reg    is_sltiu,
    output reg    is_beq,
    output reg    is_bne,
    output reg    is_lw,
    output reg    is_lbu,
    output reg    is_sw,
    output reg    is_sb,
    output reg    is_jalr,
    output reg    is_ebreak,
    output reg    use_rd,
    output reg    use_rs1,
    output reg    use_rs2,
    output reg    illegal
);

  wire [6:0] opcode = instr[6:0];
  wire [2:0] funct3 = instr[14:12];
  wire [6:0] funct7 = instr[31:25];

  always @(*) begin
    is_add = 1'b0;
    is_addi = 1'b0;
    is_lui = 1'b0;
    is_auipc = 1'b0;
    is_sltiu = 1'b0;
    is_beq = 1'b0;
    is_bne = 1'b0;
    is_lw = 1'b0;
    is_lbu = 1'b0;
    is_sw = 1'b0;
    is_sb = 1'b0;
    is_jalr = 1'b0;
    is_ebreak = 1'b0;
    use_rd = 1'b0;
    use_rs1 = 1'b0;
    use_rs2 = 1'b0;
    illegal = 1'b0;

    if (instr == 32'h00100073) begin
      is_ebreak = 1'b1;
    end else begin
      case (opcode)
        7'h33: begin
          if ((funct3 == 3'd0) && (funct7 == 7'd0)) begin
            is_add = 1'b1;
            use_rd = 1'b1;
            use_rs1 = 1'b1;
            use_rs2 = 1'b1;
          end else begin
            illegal = 1'b1;
          end
        end
        7'h13: begin
          if (funct3 == 3'd0) begin
            is_addi = 1'b1;
            use_rd = 1'b1;
            use_rs1 = 1'b1;
          end else if (funct3 == 3'd3) begin
            is_sltiu = 1'b1;
            use_rd = 1'b1;
            use_rs1 = 1'b1;
          end else begin
            illegal = 1'b1;
          end
        end
        7'h37: begin
          is_lui = 1'b1;
          use_rd = 1'b1;
        end
        7'h17: begin
          is_auipc = 1'b1;
          use_rd = 1'b1;
        end
        7'h63: begin
          if (funct3 == 3'd0) begin
            is_beq = 1'b1;
            use_rs1 = 1'b1;
            use_rs2 = 1'b1;
          end else if (funct3 == 3'd1) begin
            is_bne = 1'b1;
            use_rs1 = 1'b1;
            use_rs2 = 1'b1;
          end else begin
            illegal = 1'b1;
          end
        end
        7'h03: begin
          if (funct3 == 3'd2) begin
            is_lw = 1'b1;
            use_rd = 1'b1;
            use_rs1 = 1'b1;
          end else if (funct3 == 3'd4) begin
            is_lbu = 1'b1;
            use_rd = 1'b1;
            use_rs1 = 1'b1;
          end else begin
            illegal = 1'b1;
          end
        end
        7'h23: begin
          if (funct3 == 3'd2) begin
            is_sw = 1'b1;
            use_rs1 = 1'b1;
            use_rs2 = 1'b1;
          end else if (funct3 == 3'd0) begin
            is_sb = 1'b1;
            use_rs1 = 1'b1;
            use_rs2 = 1'b1;
          end else begin
            illegal = 1'b1;
          end
        end
        7'h67: begin
          if (funct3 == 3'd0) begin
            is_jalr = 1'b1;
            use_rd = 1'b1;
            use_rs1 = 1'b1;
          end else begin
            illegal = 1'b1;
          end
        end
        default: illegal = 1'b1;
      endcase
    end
  end

endmodule
