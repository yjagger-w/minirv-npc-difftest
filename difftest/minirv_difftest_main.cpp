#include "Vminirv_core.h"
#include "minirv_emu.h"
#include "minirv_encoding.h"
#include "minirv_memory.h"
#include "verilated.h"
#include "verilated_fst_c.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using minirv::StepEvent;
using minirv::TrapCause;
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

constexpr std::size_t kMemorySize = 64U * 1024U;
constexpr std::size_t kCycleLimit = 300;
constexpr std::size_t kRetirementLimit = 200;

struct ExpectedRegister {
  std::uint8_t index;
  std::uint32_t value;
};

struct ExpectedMemory {
  std::uint32_t address;
  std::uint32_t value;
};

struct TestSpec {
  std::string name;
  std::vector<std::uint32_t> program;
  std::vector<ExpectedMemory> initial_memory;
  std::optional<std::size_t> exact_retirements;
  std::optional<TrapCause> expected_trap;
  std::vector<ExpectedRegister> expected_registers;
  std::vector<ExpectedMemory> expected_memory;
  std::optional<std::uint32_t> expected_next_pc;
};

std::string hex32(std::uint32_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return stream.str();
}

const char* instruction_name(std::uint32_t instruction) {
  const std::uint32_t opcode = instruction & 0x7fU;
  const std::uint32_t funct3 = (instruction >> 12U) & 7U;
  const std::uint32_t funct7 = instruction >> 25U;
  if (instruction == kEbreak) return "EBREAK";
  if (opcode == 0x33U && funct3 == 0U && funct7 == 0U) return "ADD";
  if (opcode == 0x13U && funct3 == 0U) return "ADDI";
  if (opcode == 0x13U && funct3 == 3U) return "SLTIU";
  if (opcode == 0x37U) return "LUI";
  if (opcode == 0x17U) return "AUIPC";
  if (opcode == 0x63U && funct3 == 0U) return "BEQ";
  if (opcode == 0x63U && funct3 == 1U) return "BNE";
  if (opcode == 0x03U && funct3 == 2U) return "LW";
  if (opcode == 0x03U && funct3 == 4U) return "LBU";
  if (opcode == 0x23U && funct3 == 2U) return "SW";
  if (opcode == 0x23U && funct3 == 0U) return "SB";
  if (opcode == 0x67U && funct3 == 0U) return "JALR";
  return "ILLEGAL";
}

struct HistoryEntry {
  std::size_t retirement;
  std::uint32_t pc;
  std::uint32_t instruction;
  std::uint32_t next_pc;
};

class DiffRig {
 public:
  DiffRig(const TestSpec& spec, bool waveform)
      : spec_(spec),
        context_(std::make_unique<VerilatedContext>()),
        dut_(std::make_unique<Vminirv_core>(context_.get())),
        reference_(kMemorySize),
        dut_memory_(kMemorySize) {
    context_->traceEverOn(waveform);
    if (waveform) {
      trace_ = std::make_unique<VerilatedFstC>();
      dut_->trace(trace_.get(), 5);
      trace_->open("build/minirv_difftest.fst");
    }
    dut_->clk = 0;
    dut_->reset = 1;
    dut_->debug_gpr_addr = 0;
    tick();
    tick();
    dut_->reset = 0;
    reference_.reset();
    initialize_images();
    settle();
  }

  ~DiffRig() {
    dut_->final();
    if (trace_) trace_->close();
  }

  void run() {
    while (true) {
      if (spec_.exact_retirements &&
          retirement_ == *spec_.exact_retirements) {
        break;
      }
      if (cycles_ >= kCycleLimit) mismatch("cycle limit", cycles_, kCycleLimit);
      tick();
      ++cycles_;

      const bool commit = dut_->commit_valid;
      const bool trap = dut_->trap_valid;
      if (commit && trap) mismatch("commit_valid and trap_valid both asserted", 1, 0);
      if (!commit && !trap) mismatch("missing DUT event", 0, 1);

      process_event(commit, trap);
      if (commit && dut_->ebreak_valid) break;
      if (trap) break;
    }
    check_postconditions();
  }

 private:
  void initialize_images() {
    dut_memory_.load_program(spec_.program);
    if (!reference_.load_program(spec_.program)) {
      throw std::runtime_error("reference program image does not fit");
    }
    for (const ExpectedMemory& item : spec_.initial_memory) {
      dut_memory_.write_word(item.address, item.value);
      if (!reference_.write_word(item.address, item.value)) {
        throw std::runtime_error("reference data image does not fit");
      }
    }
    for (std::size_t address = 0; address < dut_memory_.size(); ++address) {
      std::uint8_t reference_byte = 0;
      if (!reference_.read_byte(static_cast<std::uint32_t>(address),
                                reference_byte)) {
        throw std::runtime_error("reference image range verification failed");
      }
      if (dut_memory_.read_byte(static_cast<std::uint32_t>(address)) !=
          reference_byte) {
        throw std::runtime_error("initial DUT/reference images differ at " +
                                 hex32(static_cast<std::uint32_t>(address)));
      }
    }
  }

  void process_event(bool commit, bool trap) {
    const std::uint32_t reference_pc_before = reference_.pc();
    const std::uint32_t dut_pc = commit ? dut_->commit_pc : dut_->imem_addr;
    if (dut_pc != reference_pc_before) {
      mismatch("event PC", dut_pc, reference_pc_before);
    }

    std::uint32_t reference_instruction = 0;
    const bool fetchable = (reference_pc_before & 3U) == 0U &&
                           reference_.read_word(reference_pc_before,
                                                reference_instruction);
    if (commit) {
      if (!fetchable) mismatch("reference instruction fetchable", 0, 1);
      if (dut_->commit_instr != reference_instruction) {
        mismatch("instruction", dut_->commit_instr, reference_instruction,
                 reference_instruction);
      }
    } else if (fetchable && dut_->imem_rdata != reference_instruction) {
      mismatch("faulting instruction", dut_->imem_rdata,
               reference_instruction, reference_instruction);
    }

    const std::array<std::uint32_t, 16> registers_before = reference_registers();
    reference_.step();
    const StepEvent& event = reference_.last_event();
    if (!event.valid) mismatch("reference event valid", 0, 1);

    if (commit) {
      ++retirement_;
      if (retirement_ > kRetirementLimit) {
        mismatch("retirement limit", retirement_, kRetirementLimit,
                 reference_instruction);
      }
      compare_commit(event, reference_instruction);
      history_.push_back({retirement_, dut_->commit_pc, dut_->commit_instr,
                          dut_->commit_next_pc});
      if (history_.size() > 8U) history_.pop_front();
    } else {
      compare_trap(event, registers_before, reference_instruction);
    }
  }

  void compare_commit(const StepEvent& event, std::uint32_t instruction) {
    if (!event.retired) mismatch("reference retired", 0, 1, instruction);
    compare("commit_pc", dut_->commit_pc, event.pc, instruction);
    compare("commit_instr", dut_->commit_instr, event.instruction, instruction);
    compare("commit_next_pc", dut_->commit_next_pc, event.next_pc, instruction);
    compare("commit_rd_wen", dut_->commit_rd_wen, event.rd_wen, instruction);
    if (event.rd_wen) {
      compare("commit_rd", dut_->commit_rd, event.rd, instruction);
      compare("commit_rd_data", dut_->commit_rd_data, event.rd_data, instruction);
    }
    compare("commit_mem_wen", dut_->commit_mem_wen, event.mem_wen, instruction);
    if (event.mem_wen) {
      compare("commit_mem_addr", dut_->commit_mem_addr, event.mem_addr,
              instruction);
      compare("commit_mem_wmask", dut_->commit_mem_wmask, event.mem_wmask,
              instruction);
      compare("commit_mem_wdata", dut_->commit_mem_wdata, event.mem_wdata,
              instruction);
    }
    compare("ebreak_valid", dut_->ebreak_valid, event.ebreak, instruction);
    compare("reference next PC", reference_.pc(), dut_->commit_next_pc,
            instruction);
    compare_registers(instruction);
    if (event.mem_wen) {
      dut_memory_.apply_store(dut_->commit_mem_addr, dut_->commit_mem_wdata,
                              static_cast<std::uint8_t>(dut_->commit_mem_wmask));
      for (unsigned byte = 0; byte < 4; ++byte) {
        if ((event.mem_wmask & (1U << byte)) != 0U) {
          std::uint8_t reference_byte = 0;
          const std::uint32_t address = event.mem_addr + byte;
          if (!reference_.read_byte(address, reference_byte)) {
            mismatch("reference store byte range", address, 0, instruction);
          }
          compare("stored byte", dut_memory_.read_byte(address), reference_byte,
                  instruction);
        }
      }
      settle();
    }
  }

  void compare_trap(const StepEvent& event,
                    const std::array<std::uint32_t, 16>& registers_before,
                    std::uint32_t instruction) {
    if (event.retired) mismatch("reference trap retired", 1, 0, instruction);
    compare("trap_cause", dut_->trap_cause,
            static_cast<std::uint8_t>(event.trap_cause), instruction);
    compare("trap commit_valid", dut_->commit_valid, 0, instruction);
    compare("trap commit_rd_wen", dut_->commit_rd_wen, 0, instruction);
    compare("trap commit_mem_wen", dut_->commit_mem_wen, 0, instruction);
    compare("trap halted", dut_->halted, 1, instruction);
    compare("reference trap PC", reference_.pc(), event.pc, instruction);
    for (std::size_t index = 0; index < registers_before.size(); ++index) {
      compare("reference register unchanged", reference_.reg(index),
              registers_before[index], instruction);
    }
    compare_registers(instruction);
    if (spec_.expected_trap) {
      compare("expected trap cause", dut_->trap_cause,
              static_cast<std::uint8_t>(*spec_.expected_trap), instruction);
    }
  }

  void compare_registers(std::uint32_t instruction) {
    for (std::uint8_t index = 0; index < 16; ++index) {
      dut_->debug_gpr_addr = index;
      settle();
      compare(std::string("x") + std::to_string(index), dut_->debug_gpr_data,
              reference_.reg(index), instruction);
    }
    compare("x0 hardwired", reference_.reg(0), 0, instruction);
  }

  std::array<std::uint32_t, 16> reference_registers() const {
    std::array<std::uint32_t, 16> values{};
    for (std::size_t index = 0; index < values.size(); ++index) {
      values[index] = reference_.reg(index);
    }
    return values;
  }

  void check_postconditions() {
    if (spec_.exact_retirements) {
      compare("exact retirement count", retirement_, *spec_.exact_retirements);
    }
    if (spec_.expected_trap && !dut_->trap_valid) {
      mismatch("expected trap_valid", 0, 1);
    }
    for (const ExpectedRegister& expected : spec_.expected_registers) {
      dut_->debug_gpr_addr = expected.index;
      settle();
      compare(std::string("final x") + std::to_string(expected.index),
              dut_->debug_gpr_data, expected.value);
    }
    for (const ExpectedMemory& expected : spec_.expected_memory) {
      compare("final DUT memory", dut_memory_.read_word(expected.address),
              expected.value);
      std::uint32_t reference_word = 0;
      if (!reference_.read_word(expected.address, reference_word)) {
        mismatch("final reference memory range", expected.address, 0);
      }
      compare("final reference memory", reference_word, expected.value);
    }
    if (spec_.expected_next_pc) {
      compare("final next PC", reference_.pc(), *spec_.expected_next_pc);
    }
  }

  template <typename DutValue, typename ReferenceValue>
  void compare(const std::string& field, DutValue dut_value,
               ReferenceValue reference_value, std::uint32_t instruction = 0) {
    const std::uint64_t dut_wide = static_cast<std::uint64_t>(dut_value);
    const std::uint64_t reference_wide =
        static_cast<std::uint64_t>(reference_value);
    if (dut_wide != reference_wide) {
      mismatch(field, dut_wide, reference_wide, instruction);
    }
  }

  [[noreturn]] void mismatch(const std::string& field, std::uint64_t dut_value,
                             std::uint64_t reference_value,
                             std::uint32_t instruction = 0) {
    const std::uint32_t shown_instruction =
        instruction != 0U ? instruction
                          : (dut_->commit_valid ? dut_->commit_instr
                                                : dut_->imem_rdata);
    std::ostringstream report;
    report << "DiffTest mismatch\n"
           << "  test: " << spec_.name << '\n'
           << "  retirement: " << retirement_ << '\n'
           << "  cycle: " << cycles_ << '\n'
           << "  DUT PC: "
           << hex32(dut_->commit_valid ? dut_->commit_pc : dut_->imem_addr)
           << '\n'
           << "  reference PC: " << hex32(reference_.pc()) << '\n'
           << "  instruction: " << hex32(shown_instruction) << " ("
           << instruction_name(shown_instruction) << ")\n"
           << "  field: " << field << '\n'
           << "  DUT value: " << hex32(static_cast<std::uint32_t>(dut_value))
           << '\n'
           << "  reference value: "
           << hex32(static_cast<std::uint32_t>(reference_value)) << '\n'
           << "  registers:\n";
    for (std::uint8_t index = 0; index < 16; ++index) {
      dut_->debug_gpr_addr = index;
      settle();
      report << "    x" << std::dec << static_cast<unsigned>(index)
             << " DUT=" << hex32(dut_->debug_gpr_data)
             << " REF=" << hex32(reference_.reg(index)) << '\n';
    }
    report << "  recent commits:\n";
    for (const HistoryEntry& entry : history_) {
      report << "    #" << entry.retirement << " pc=" << hex32(entry.pc)
             << " instr=" << hex32(entry.instruction) << " ("
             << instruction_name(entry.instruction)
             << ") next=" << hex32(entry.next_pc) << '\n';
    }
    if (dut_->commit_mem_wen || reference_.last_event().mem_wen) {
      report << "  store DUT addr=" << hex32(dut_->commit_mem_addr)
             << " mask=" << hex32(dut_->commit_mem_wmask)
             << " data=" << hex32(dut_->commit_mem_wdata)
             << " REF addr=" << hex32(reference_.last_event().mem_addr)
             << " mask=" << hex32(reference_.last_event().mem_wmask)
             << " data=" << hex32(reference_.last_event().mem_wdata) << '\n';
    }
    throw std::runtime_error(report.str());
  }

  void drive_memory() {
    dut_->imem_rdata = dut_memory_.read_word(dut_->imem_addr);
    if (dut_->dmem_valid && !dut_->dmem_write) {
      dut_->dmem_rdata = dut_memory_.read_word(dut_->dmem_addr);
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

  void dump() {
    if (trace_) trace_->dump(context_->time());
    context_->timeInc(1);
  }

  const TestSpec& spec_;
  std::unique_ptr<VerilatedContext> context_;
  std::unique_ptr<Vminirv_core> dut_;
  std::unique_ptr<VerilatedFstC> trace_;
  minirv::Emulator reference_;
  MiniRVMemory dut_memory_;
  std::size_t cycles_ = 0;
  std::size_t retirement_ = 0;
  std::deque<HistoryEntry> history_;
};

std::vector<TestSpec> make_tests() {
  const std::vector<std::uint32_t> integrated = {
      addi(1, 0, 256), lui(2, 0x12345U), addi(2, 2, 0x678),
      encode_s(0, 1, 2, 2), lw(3, 1, 0), lbu(4, 1, 1),
      addi(5, 0, -17), encode_s(4, 1, 5, 0), encode_r(6, 3, 4),
      jalr(7, 0, 44), 0U, kEbreak};

  return {
      {"official E5 ADDI/JALR",
       {0x01400513U, 0x010000e7U, 0x00c000e7U, 0x00c00067U,
        0x00a50513U, 0x00008067U},
       {}, 6U, {}, {{10, 30U}, {1, 12U}, {0, 0U}}, {}, 12U},
      {"JALR rd equals rs1",
       {addi(1, 0, 16), jalr(1, 1, 8), 0U, 0U, 0U, 0U, kEbreak},
       {}, 2U, {}, {{1, 8U}}, {}, 24U},
      {"negative ADDI", {addi(5, 0, -1), kEbreak}, {}, {}, {},
       {{5, 0xffffffffU}}, {}, {}},
      {"ADD and overflow",
       {addi(1, 0, 7), addi(2, 0, 9), encode_r(3, 1, 2),
        addi(4, 0, -1), addi(5, 0, 1), encode_r(6, 4, 5), kEbreak},
       {}, {}, {}, {{3, 16U}, {6, 0U}}, {}, {}},
      {"LUI", {lui(7, 0xabcdeU), kEbreak}, {}, {}, {},
       {{7, 0xabcde000U}}, {}, {}},
      {"AUIPC semantics",
       {auipc(1, 1), auipc(2, 1), auipc(3, 0xfffffU),
        auipc(0, 0x12345U), kEbreak},
       {}, {}, {}, {{1, 0x1000U}, {2, 0x1004U}, {3, 0xfffff008U}, {0, 0U}},
       {}, {}},
      {"SLTIU semantics",
       {sltiu(1, 0, 1), addi(2, 0, 1), sltiu(3, 2, 1),
        addi(4, 0, -1), sltiu(5, 4, 1), sltiu(6, 0, -1),
        sltiu(7, 4, -1), sltiu(0, 0, 1), kEbreak},
       {}, {}, {}, {{1, 1U}, {3, 0U}, {5, 0U}, {6, 1U}, {7, 0U}, {0, 0U}},
       {}, {}},
      {"BEQ/BNE forward and backward",
       {addi(1, 0, 1), bne(1, 0, 8), kEbreak, addi(1, 0, 0),
        beq(1, 0, -8), kEbreak},
       {}, {}, {}, {{1, 0U}}, {}, 12U},
      {"not-taken misaligned branch targets",
       {addi(1, 0, 1), beq(1, 0, 2), bne(1, 1, 2), kEbreak},
       {}, {}, {}, {{1, 1U}}, {}, 16U},
      {"taken misaligned branch target", {beq(0, 0, 2)}, {}, {},
       TrapCause::InstructionAddressMisaligned, {}, {}, 0U},
      {"AUIPC invalid RV32E rd", {auipc(16, 0)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"SLTIU invalid RV32E rs1", {sltiu(1, 16, 0)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"branch invalid RV32E rs2", {bne(0, 16, 4)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"illegal branch funct3", {minirv_encoding::encode_b(4, 0, 0, 2)},
       {}, {}, TrapCause::IllegalInstruction, {}, {}, {}},
      {"LW and SW",
       {addi(1, 0, 256), lui(2, 0x12345U), addi(2, 2, 0x678),
        encode_s(0, 1, 2, 2), lw(3, 1, 0), kEbreak},
       {}, {}, {}, {{3, 0x12345678U}}, {{256, 0x12345678U}}, {}},
      {"negative LW offset", {addi(1, 0, 264), lw(2, 1, -8), kEbreak},
       {{256, 0x89abcdefU}}, {}, {}, {{2, 0x89abcdefU}}, {}, {}},
      {"negative SW offset",
       {addi(1, 0, 264), lui(2, 0x12345U), addi(2, 2, 0x678),
        encode_s(-8, 1, 2, 2), kEbreak},
       {}, {}, {}, {}, {{256, 0x12345678U}}, {}},
      {"LBU byte order",
       {addi(1, 0, 256), lbu(2, 1, 0), lbu(3, 1, 1),
        lbu(4, 1, 2), lbu(5, 1, 3), kEbreak},
       {{256, 0x12345678U}}, {}, {},
       {{2, 0x78U}, {3, 0x56U}, {4, 0x34U}, {5, 0x12U}}, {}, {}},
      {"SB sequence",
       {addi(1, 0, 256), addi(2, 0, -17), encode_s(0, 1, 2, 0),
        addi(2, 0, -51), encode_s(1, 1, 2, 0), addi(2, 0, -85),
        encode_s(2, 1, 2, 0), addi(2, 0, -112), encode_s(3, 1, 2, 0),
        kEbreak},
       {{256, 0x12345678U}}, {}, {}, {}, {{256, 0x90abcdefU}}, {}},
      {"x0 write suppression",
       {addi(0, 0, 123), lui(0, 0xfffffU), encode_r(0, 0, 0),
        jalr(0, 0, 16), kEbreak},
       {}, {}, {}, {{0, 0U}}, {}, {}},
      {"integrated all instructions", integrated, {}, {}, {},
       {{3, 0x12345678U}, {4, 0x56U}, {5, 0xffffffefU},
        {6, 0x123456ceU}, {7, 40U}},
       {{256, 0x12345678U}, {260, 0x000000efU}}, {}},
      {"EBREAK retirement", {kEbreak}, {}, {}, {}, {{0, 0U}}, {}, 4U},
      {"invalid RV32E rd", {addi(16, 0, 1)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"invalid RV32E rs1", {addi(1, 16, 1)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"invalid RV32E rs2", {encode_r(1, 0, 16)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"illegal ADD funct7", {encode_r(1, 2, 3) | (0x20U << 25U)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"illegal load funct3", {encode_i(0, 1, 1, 2, 0x03U)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"illegal store funct3", {encode_s(0, 1, 2, 1)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"illegal JALR funct3", {encode_i(0, 1, 1, 2, 0x67U)}, {}, {},
       TrapCause::IllegalInstruction, {}, {}, {}},
      {"misaligned LW", {addi(1, 0, 257), lw(2, 1, 0)}, {}, {},
       TrapCause::LoadAddressMisaligned, {}, {}, {}},
      {"misaligned SW", {addi(1, 0, 257), encode_s(0, 1, 0, 2)}, {}, {},
       TrapCause::StoreAddressMisaligned, {}, {}, {}},
      {"misaligned instruction fetch", {addi(1, 0, 2), jalr(0, 1, 0)},
       {}, {}, TrapCause::InstructionAddressMisaligned, {}, {}, 2U},
  };
}

}  // namespace

int main(int argc, char** argv) {
  bool waveform = false;
  for (int index = 1; index < argc; ++index) {
    if (std::string(argv[index]) == "--fst") {
      waveform = true;
    } else {
      std::cerr << "Unknown argument: " << argv[index] << '\n';
      return 2;
    }
  }

  const std::vector<TestSpec> tests = make_tests();
  std::size_t first = 0;
  std::size_t count = tests.size();
  if (waveform) {
    first = 11;
    count = 1;
  }

  std::size_t failures = 0;
  for (std::size_t offset = 0; offset < count; ++offset) {
    const TestSpec& test = tests[first + offset];
    try {
      DiffRig rig(test, waveform);
      rig.run();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ":\n" << error.what() << '\n';
      break;
    }
  }
  std::cout << count - failures << '/' << count << " DiffTest tests passed\n";
  return failures == 0U ? 0 : 1;
}
