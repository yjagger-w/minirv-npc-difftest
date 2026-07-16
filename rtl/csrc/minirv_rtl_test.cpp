#include "Vminirv_core.h"
#include "minirv_encoding.h"
#include "minirv_memory.h"
#include "verilated.h"
#include "verilated_fst_c.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kCycleLimit = 200;
using minirv_encoding::addi;
using minirv_encoding::auipc;
using minirv_encoding::beq;
using minirv_encoding::bne;
using minirv_encoding::encode_i;
using minirv_encoding::encode_r;
using minirv_encoding::encode_s;
using minirv_encoding::jalr;
using minirv_encoding::kEbreak;
using minirv_encoding::lbu;
using minirv_encoding::lui;
using minirv_encoding::lw;
using minirv_encoding::sltiu;

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class Rig {
 public:
  explicit Rig(bool waveform = false)
      : context_(std::make_unique<VerilatedContext>()),
        dut_(std::make_unique<Vminirv_core>(context_.get())),
        waveform_(waveform) {
    context_->traceEverOn(waveform_);
    if (waveform_) {
      trace_ = std::make_unique<VerilatedFstC>();
      dut_->trace(trace_.get(), 5);
      trace_->open("build/minirv_rtl.fst");
    }
    dut_->clk = 0;
    dut_->reset = 1;
    dut_->debug_gpr_addr = 0;
    tick();
    tick();
    dut_->reset = 0;
    settle();
  }

  ~Rig() {
    dut_->final();
    if (trace_) {
      trace_->close();
    }
  }

  MiniRVMemory memory;

  Vminirv_core& dut() { return *dut_; }

  void load(const std::vector<std::uint32_t>& program) {
    memory.load_program(program);
    settle();
  }

  void cycle() {
    require(cycles_ < kCycleLimit, "cycle limit exceeded");
    tick();
    ++cycles_;
    if (dut_->commit_valid && dut_->commit_mem_wen) {
      memory.apply_store(dut_->commit_mem_addr, dut_->commit_mem_wdata,
                         static_cast<std::uint8_t>(dut_->commit_mem_wmask));
      settle();
    }
  }

  void run_to_halt(std::size_t instruction_limit = 100) {
    std::size_t retired = 0;
    while (!dut_->halted) {
      cycle();
      if (dut_->commit_valid) {
        ++retired;
        require(retired <= instruction_limit, "instruction limit exceeded");
      }
    }
  }

  void retire_exactly(std::size_t count) {
    std::size_t retired = 0;
    while (retired < count) {
      cycle();
      require(!dut_->trap_valid, "unexpected trap before retirement target");
      if (dut_->commit_valid) {
        ++retired;
      }
    }
  }

  std::uint32_t reg(std::uint8_t index) {
    dut_->debug_gpr_addr = index;
    settle();
    return dut_->debug_gpr_data;
  }

 private:
  void drive_memory() {
    dut_->imem_rdata = memory.read_word(dut_->imem_addr);
    if (dut_->dmem_valid && !dut_->dmem_write) {
      if (dut_->dmem_wmask == 0U) {
        dut_->dmem_rdata = memory.read_word(dut_->dmem_addr);
      } else {
        dut_->dmem_rdata = memory.read_word(dut_->dmem_addr);
      }
    } else {
      dut_->dmem_rdata = 0;
    }
  }

  void settle() {
    for (int iteration = 0; iteration < 3; ++iteration) {
      drive_memory();
      dut_->eval();
    }
  }

  void dump() {
    if (trace_) {
      trace_->dump(context_->time());
    }
    context_->timeInc(1);
  }

  void tick() {
    dut_->clk = 0;
    settle();
    dump();
    dut_->clk = 1;
    settle();
    dump();
    dut_->clk = 0;
    settle();
    dump();
  }

  std::unique_ptr<VerilatedContext> context_;
  std::unique_ptr<Vminirv_core> dut_;
  std::unique_ptr<VerilatedFstC> trace_;
  bool waveform_ = false;
  std::size_t cycles_ = 0;
};

void expect_trap(Rig& rig, std::uint8_t cause) {
  rig.run_to_halt();
  require(rig.dut().trap_valid, "expected trap_valid");
  require(rig.dut().trap_cause == cause, "unexpected trap cause");
  require(!rig.dut().commit_valid, "trapping instruction committed");
}

void test_official(bool waveform) {
  Rig rig(waveform);
  rig.load({0x01400513U, 0x010000e7U, 0x00c000e7U,
            0x00c00067U, 0x00a50513U, 0x00008067U});
  rig.retire_exactly(6);
  require(rig.reg(10) == 30U, "x10 != 30");
  require(rig.reg(1) == 12U, "x1 != 12");
  require(rig.dut().commit_next_pc == 12U, "sixth next PC != 12");
  require(rig.reg(0) == 0U, "x0 changed");
}

void test_jalr_same_register(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(1, 0, 16), jalr(1, 1, 8), 0U, 0U, 0U, 0U, kEbreak});
  rig.retire_exactly(2);
  require(rig.dut().commit_next_pc == 24U, "target did not use old rs1");
  require(rig.reg(1) == 8U, "rd did not receive old_pc + 4");
}

void test_negative_addi(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(5, 0, -1), kEbreak});
  rig.run_to_halt();
  require(rig.reg(5) == 0xffffffffU, "negative ADDI failed");
}

void test_add_overflow(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(1, 0, 7), addi(2, 0, 9), encode_r(3, 1, 2),
            addi(4, 0, -1), addi(5, 0, 1), encode_r(6, 4, 5), kEbreak});
  rig.run_to_halt();
  require(rig.reg(3) == 16U, "ordinary ADD failed");
  require(rig.reg(6) == 0U, "ADD overflow did not wrap");
}

void test_lui(bool waveform) {
  Rig rig(waveform);
  rig.load({lui(7, 0xabcdeU), kEbreak});
  rig.run_to_halt();
  require(rig.reg(7) == 0xabcde000U, "LUI failed");
}

void test_auipc(bool waveform) {
  Rig rig(waveform);
  rig.load({auipc(1, 1), auipc(2, 1), auipc(3, 0xfffffU),
            auipc(0, 0x12345U), kEbreak});
  rig.run_to_halt();
  require(rig.reg(1) == 0x1000U, "AUIPC at PC 0 failed");
  require(rig.reg(2) == 0x1004U, "AUIPC at nonzero PC failed");
  require(rig.reg(3) == 0xfffff008U, "AUIPC high immediate failed");
  require(rig.reg(0) == 0U, "AUIPC changed x0");
}

void test_sltiu(bool waveform) {
  Rig rig(waveform);
  rig.load({sltiu(1, 0, 1), addi(2, 0, 1), sltiu(3, 2, 1),
            addi(4, 0, -1), sltiu(5, 4, 1), sltiu(6, 0, -1),
            sltiu(7, 4, -1), sltiu(0, 0, 1), kEbreak});
  rig.run_to_halt();
  require(rig.reg(1) == 1U && rig.reg(3) == 0U && rig.reg(5) == 0U &&
              rig.reg(6) == 1U && rig.reg(7) == 0U && rig.reg(0) == 0U,
          "SLTIU edge cases failed");
}

void test_branches(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(1, 0, 1), bne(1, 0, 8), kEbreak, addi(1, 0, 0),
            beq(1, 0, -8), kEbreak});
  rig.run_to_halt();
  require(rig.dut().commit_pc == 8U, "forward/backward branch flow failed");

  Rig not_taken;
  not_taken.load({addi(1, 0, 1), beq(1, 0, 2), bne(1, 1, 2), kEbreak});
  not_taken.run_to_halt();
  require(!not_taken.dut().trap_valid, "not-taken branch trapped");
}

void test_new_instruction_validation(bool waveform) {
  Rig auipc_rd(waveform);
  auipc_rd.load({auipc(16, 0)});
  expect_trap(auipc_rd, 1);
  Rig sltiu_rd;
  sltiu_rd.load({sltiu(16, 0, 0)});
  expect_trap(sltiu_rd, 1);
  Rig sltiu_rs1;
  sltiu_rs1.load({sltiu(1, 16, 0)});
  expect_trap(sltiu_rs1, 1);
  Rig branch_rs1;
  branch_rs1.load({beq(16, 0, 4)});
  expect_trap(branch_rs1, 1);
  Rig branch_rs2;
  branch_rs2.load({bne(0, 16, 4)});
  expect_trap(branch_rs2, 1);
  Rig illegal_branch;
  illegal_branch.load({minirv_encoding::encode_b(4, 0, 0, 2)});
  expect_trap(illegal_branch, 1);
  Rig misaligned;
  misaligned.load({beq(0, 0, 2)});
  expect_trap(misaligned, 2);
}

void test_lw_sw(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(1, 0, 256), lui(2, 0x12345U), addi(2, 2, 0x678),
            encode_s(0, 1, 2, 2), lw(3, 1, 0), kEbreak});
  rig.run_to_halt();
  require(rig.memory.read_word(256) == 0x12345678U, "SW failed");
  require(rig.reg(3) == 0x12345678U, "LW failed");
}

void test_negative_lw(bool waveform) {
  Rig rig(waveform);
  rig.memory.write_word(256, 0x89abcdefU);
  rig.load({addi(1, 0, 264), lw(2, 1, -8), kEbreak});
  rig.run_to_halt();
  require(rig.reg(2) == 0x89abcdefU, "negative LW offset failed");
}

void test_negative_sw(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(1, 0, 264), lui(2, 0x12345U), addi(2, 2, 0x678),
            encode_s(-8, 1, 2, 2), kEbreak});
  rig.run_to_halt();
  require(rig.memory.read_word(256) == 0x12345678U,
          "negative SW offset failed");
}

void test_lbu(bool waveform) {
  Rig rig(waveform);
  rig.memory.write_word(256, 0x12345678U);
  rig.load({addi(1, 0, 256), lbu(2, 1, 0), lbu(3, 1, 1),
            lbu(4, 1, 2), lbu(5, 1, 3), kEbreak});
  rig.run_to_halt();
  require(rig.reg(2) == 0x78U && rig.reg(3) == 0x56U &&
              rig.reg(4) == 0x34U && rig.reg(5) == 0x12U,
          "LBU byte order failed");
}

void test_sb(bool waveform) {
  Rig rig(waveform);
  rig.memory.write_word(256, 0x12345678U);
  rig.load({addi(1, 0, 256), addi(2, 0, -17), encode_s(0, 1, 2, 0),
            addi(2, 0, -51), encode_s(1, 1, 2, 0),
            addi(2, 0, -85), encode_s(2, 1, 2, 0),
            addi(2, 0, -112), encode_s(3, 1, 2, 0), kEbreak});
  rig.run_to_halt();
  require(rig.memory.read_word(256) == 0x90abcdefU, "SB failed");
}

void test_x0(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(0, 0, 123), lui(0, 0xfffffU), encode_r(0, 0, 0),
            jalr(0, 0, 16), kEbreak});
  rig.run_to_halt();
  require(rig.reg(0) == 0U, "x0 changed");
}

void test_ebreak(bool waveform) {
  Rig rig(waveform);
  rig.load({kEbreak});
  rig.cycle();
  require(rig.dut().commit_valid && rig.dut().ebreak_valid,
          "EBREAK did not retire");
  require(rig.dut().halted && !rig.dut().trap_valid,
          "EBREAK did not halt cleanly");
  rig.cycle();
  require(!rig.dut().commit_valid && !rig.dut().ebreak_valid,
          "EBREAK retired more than once");
}

void test_invalid_rd(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(16, 0, 1)});
  expect_trap(rig, 1);
}

void test_invalid_rs1(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(1, 16, 1)});
  expect_trap(rig, 1);
}

void test_invalid_rs2(bool waveform) {
  Rig rig(waveform);
  rig.load({encode_r(1, 0, 16)});
  expect_trap(rig, 1);
}

void test_illegal_add(bool waveform) {
  Rig rig(waveform);
  rig.load({encode_r(1, 2, 3) | (0x20U << 25U)});
  expect_trap(rig, 1);
}

void test_illegal_load(bool waveform) {
  Rig rig(waveform);
  rig.load({encode_i(0, 1, 1, 2, 0x03U)});
  expect_trap(rig, 1);
}

void test_illegal_store(bool waveform) {
  Rig rig(waveform);
  rig.load({encode_s(0, 1, 2, 1)});
  expect_trap(rig, 1);
}

void test_illegal_jalr(bool waveform) {
  Rig rig(waveform);
  rig.load({encode_i(0, 1, 1, 2, 0x67U)});
  expect_trap(rig, 1);
}

void test_misaligned_lw(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(1, 0, 257), lw(2, 1, 0)});
  expect_trap(rig, 3);
}

void test_misaligned_sw(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(1, 0, 257), encode_s(0, 1, 0, 2)});
  expect_trap(rig, 4);
}

void test_misaligned_fetch(bool waveform) {
  Rig rig(waveform);
  rig.load({addi(1, 0, 2), jalr(0, 1, 0)});
  expect_trap(rig, 2);
  require(rig.dut().commit_next_pc == 2U,
          "JALR did not create the expected misaligned target");
}

using Test = std::pair<std::string, std::function<void(bool)>>;

}  // namespace

int main(int argc, char** argv) {
  bool waveform = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--fst") {
      waveform = true;
    } else {
      std::cerr << "Unknown argument: " << argument << '\n';
      return 2;
    }
  }

  const std::vector<Test> all_tests = {
      {"official E5 ADDI/JALR", test_official},
      {"JALR rd equals rs1", test_jalr_same_register},
      {"negative ADDI", test_negative_addi},
      {"ADD and overflow", test_add_overflow},
      {"LUI", test_lui},
      {"AUIPC", test_auipc},
      {"SLTIU", test_sltiu},
      {"BEQ/BNE", test_branches},
      {"new instruction validation", test_new_instruction_validation},
      {"LW/SW", test_lw_sw},
      {"negative LW offset", test_negative_lw},
      {"negative SW offset", test_negative_sw},
      {"LBU byte order", test_lbu},
      {"SB", test_sb},
      {"x0 hardwiring", test_x0},
      {"EBREAK", test_ebreak},
      {"invalid RV32E rd", test_invalid_rd},
      {"invalid RV32E rs1", test_invalid_rs1},
      {"invalid RV32E rs2", test_invalid_rs2},
      {"illegal ADD funct7", test_illegal_add},
      {"illegal load funct3", test_illegal_load},
      {"illegal store funct3", test_illegal_store},
      {"illegal JALR funct3", test_illegal_jalr},
      {"misaligned LW", test_misaligned_lw},
      {"misaligned SW", test_misaligned_sw},
      {"misaligned instruction fetch", test_misaligned_fetch},
  };

  const std::size_t count = waveform ? 1U : all_tests.size();
  std::size_t failures = 0;
  for (std::size_t index = 0; index < count; ++index) {
    try {
      all_tests[index].second(waveform);
      std::cout << "[PASS] " << all_tests[index].first << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << all_tests[index].first << ": "
                << error.what() << '\n';
    }
  }
  std::cout << count - failures << '/' << count << " RTL tests passed\n";
  return failures == 0U ? 0 : 1;
}
