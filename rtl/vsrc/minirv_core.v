module minirv_core (
    input         clk,
    input         reset,

    output [31:0] imem_addr,
    input  [31:0] imem_rdata,

    output        dmem_valid,
    output        dmem_write,
    output [31:0] dmem_addr,
    output [31:0] dmem_wdata,
    output [3:0]  dmem_wmask,
    input  [31:0] dmem_rdata,

    output reg        commit_valid,
    output reg [31:0] commit_pc,
    output reg [31:0] commit_instr,
    output reg [31:0] commit_next_pc,
    output reg        commit_rd_wen,
    output reg [3:0]  commit_rd,
    output reg [31:0] commit_rd_data,
    output reg        commit_mem_wen,
    output reg [31:0] commit_mem_addr,
    output reg [3:0]  commit_mem_wmask,
    output reg [31:0] commit_mem_wdata,

    input  [3:0]  debug_gpr_addr,
    output [31:0] debug_gpr_data,

    output reg       ebreak_valid,
    output reg       trap_valid,
    output reg [3:0] trap_cause,
    output reg       halted
);

  reg [31:0] pc;

  wire is_add;
  wire is_addi;
  wire is_lui;
  wire is_lw;
  wire is_lbu;
  wire is_sw;
  wire is_sb;
  wire is_jalr;
  wire is_ebreak;
  wire use_rd;
  wire use_rs1;
  wire use_rs2;
  wire decode_illegal;
  wire [31:0] imm_i;
  wire [31:0] imm_s;
  wire [4:0] rd_field = imem_rdata[11:7];
  wire [4:0] rs1_field = imem_rdata[19:15];
  wire [4:0] rs2_field = imem_rdata[24:20];
  wire [31:0] rs1_data;
  wire [31:0] rs2_data;
  wire [31:0] alu_rhs = (is_add || is_jalr) ? rs2_data : imm_i;
  wire [31:0] alu_sum;
  wire [31:0] effective_addr = rs1_data + (is_sw || is_sb ? imm_s : imm_i);
  wire invalid_register = (use_rd && rd_field[4]) ||
                          (use_rs1 && rs1_field[4]) ||
                          (use_rs2 && rs2_field[4]);
  wire fetch_misaligned = |pc[1:0];
  wire load_misaligned = is_lw && (|effective_addr[1:0]);
  wire store_misaligned = is_sw && (|effective_addr[1:0]);
  wire instruction_trap = fetch_misaligned || decode_illegal ||
                          invalid_register || load_misaligned ||
                          store_misaligned;
  wire execute_enable = !reset && !halted && !instruction_trap;
  wire result_instruction = is_add || is_addi || is_lui || is_lw ||
                            is_lbu || is_jalr;
  wire register_write = execute_enable && result_instruction &&
                        (rd_field[3:0] != 4'd0);
  wire [31:0] sequential_pc = pc + 32'd4;
  wire [31:0] jalr_target = (rs1_data + imm_i) & 32'hfffffffe;
  wire [31:0] next_pc = is_jalr ? jalr_target : sequential_pc;
  wire [31:0] writeback_data = is_lui ? {imem_rdata[31:12], 12'd0} :
                                   is_lw ? dmem_rdata :
                                   is_lbu ? {24'd0, dmem_rdata[7:0]} :
                                   is_jalr ? sequential_pc : alu_sum;

  assign imem_addr = pc;
  assign dmem_valid = execute_enable && (is_lw || is_lbu || is_sw || is_sb);
  assign dmem_write = execute_enable && (is_sw || is_sb);
  assign dmem_addr = effective_addr;
  assign dmem_wdata = rs2_data;
  assign dmem_wmask = is_sw ? 4'b1111 : (is_sb ? 4'b0001 : 4'b0000);

  minirv_decoder decoder (
      .instr(imem_rdata),
      .is_add(is_add),
      .is_addi(is_addi),
      .is_lui(is_lui),
      .is_lw(is_lw),
      .is_lbu(is_lbu),
      .is_sw(is_sw),
      .is_sb(is_sb),
      .is_jalr(is_jalr),
      .is_ebreak(is_ebreak),
      .use_rd(use_rd),
      .use_rs1(use_rs1),
      .use_rs2(use_rs2),
      .illegal(decode_illegal)
  );

  minirv_immgen immgen (
      .imm_i_bits(imem_rdata[31:20]),
      .imm_s_high(imem_rdata[31:25]),
      .imm_s_low(imem_rdata[11:7]),
      .imm_i(imm_i),
      .imm_s(imm_s)
  );

  minirv_alu alu (
      .lhs(rs1_data),
      .rhs(alu_rhs),
      .sum(alu_sum)
  );

  minirv_regfile regfile (
      .clk(clk),
      .reset(reset),
      .rs1_addr(rs1_field[3:0]),
      .rs2_addr(rs2_field[3:0]),
      .rs1_data(rs1_data),
      .rs2_data(rs2_data),
      .rd_wen(register_write),
      .rd_addr(rd_field[3:0]),
      .rd_data(writeback_data),
      .debug_addr(debug_gpr_addr),
      .debug_data(debug_gpr_data)
  );

  always @(posedge clk) begin
    if (reset) begin
      pc <= 32'd0;
      commit_valid <= 1'b0;
      commit_pc <= 32'd0;
      commit_instr <= 32'd0;
      commit_next_pc <= 32'd0;
      commit_rd_wen <= 1'b0;
      commit_rd <= 4'd0;
      commit_rd_data <= 32'd0;
      commit_mem_wen <= 1'b0;
      commit_mem_addr <= 32'd0;
      commit_mem_wmask <= 4'd0;
      commit_mem_wdata <= 32'd0;
      ebreak_valid <= 1'b0;
      trap_valid <= 1'b0;
      trap_cause <= 4'd0;
      halted <= 1'b0;
    end else begin
      commit_valid <= 1'b0;
      commit_rd_wen <= 1'b0;
      commit_mem_wen <= 1'b0;
      ebreak_valid <= 1'b0;
      if (!halted) begin
        if (instruction_trap) begin
          halted <= 1'b1;
          trap_valid <= 1'b1;
          if (fetch_misaligned) begin
            trap_cause <= 4'd2;
          end else if (load_misaligned) begin
            trap_cause <= 4'd3;
          end else if (store_misaligned) begin
            trap_cause <= 4'd4;
          end else begin
            trap_cause <= 4'd1;
          end
        end else begin
          pc <= next_pc;
          commit_valid <= 1'b1;
          commit_pc <= pc;
          commit_instr <= imem_rdata;
          commit_next_pc <= next_pc;
          commit_rd_wen <= register_write;
          commit_rd <= rd_field[3:0];
          commit_rd_data <= writeback_data;
          commit_mem_wen <= dmem_write;
          commit_mem_addr <= effective_addr;
          commit_mem_wmask <= dmem_wmask;
          commit_mem_wdata <= dmem_wdata;
          if (is_ebreak) begin
            ebreak_valid <= 1'b1;
            halted <= 1'b1;
          end
        end
      end
    end
  end

endmodule
