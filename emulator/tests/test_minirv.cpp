#include "minirv_emu.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using minirv::Emulator;
using minirv::Status;

constexpr std::uint32_t kEbreak = 0x00100073U;

std::uint32_t encode_r(std::uint32_t rd, std::uint32_t rs1,
                       std::uint32_t rs2) {
  return (rs2 << 20U) | (rs1 << 15U) | (rd << 7U) | 0x33U;
}

std::uint32_t encode_i(std::int32_t immediate, std::uint32_t rs1,
                       std::uint32_t funct3, std::uint32_t rd,
                       std::uint32_t opcode) {
  return ((static_cast<std::uint32_t>(immediate) & 0xfffU) << 20U) |
         (rs1 << 15U) | (funct3 << 12U) | (rd << 7U) | opcode;
}

std::uint32_t encode_s(std::int32_t immediate, std::uint32_t rs1,
                       std::uint32_t rs2, std::uint32_t funct3) {
  const std::uint32_t bits = static_cast<std::uint32_t>(immediate) & 0xfffU;
  return ((bits >> 5U) << 25U) | (rs2 << 20U) | (rs1 << 15U) |
         (funct3 << 12U) | ((bits & 0x1fU) << 7U) | 0x23U;
}

std::uint32_t addi(std::uint32_t rd, std::uint32_t rs1,
                   std::int32_t immediate) {
  return encode_i(immediate, rs1, 0, rd, 0x13U);
}

std::uint32_t lui(std::uint32_t rd, std::uint32_t upper) {
  return (upper << 12U) | (rd << 7U) | 0x37U;
}

std::uint32_t lw(std::uint32_t rd, std::uint32_t rs1,
                 std::int32_t immediate) {
  return encode_i(immediate, rs1, 2, rd, 0x03U);
}

std::uint32_t lbu(std::uint32_t rd, std::uint32_t rs1,
                  std::int32_t immediate) {
  return encode_i(immediate, rs1, 4, rd, 0x03U);
}

std::uint32_t jalr(std::uint32_t rd, std::uint32_t rs1,
                   std::int32_t immediate) {
  return encode_i(immediate, rs1, 0, rd, 0x67U);
}

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void load(Emulator& emulator, const std::vector<std::uint32_t>& program) {
  require(emulator.load_program(program), "program did not fit in memory");
}

void run_to_ebreak(Emulator& emulator, std::size_t limit = 100) {
  const Status result = emulator.run(limit);
  require(result == Status::Halted,
          std::string("expected halted, got ") + minirv::status_name(result) +
              ": " + emulator.error_message());
}

void test_official_addi_jalr() {
  Emulator emulator;
  load(emulator, {0x01400513U, 0x010000e7U, 0x00c000e7U,
                  0x00c00067U, 0x00a50513U, 0x00008067U});
  for (int i = 0; i < 6; ++i) {
    require(emulator.step() == Status::Running, "official program failed");
  }
  require(emulator.reg(10) == 30U, "official program x10 != 30");
  require(emulator.reg(1) == 12U, "official program x1 != 12");
  require(emulator.pc() == 12U, "official program pc != 12");
  require(emulator.reg(0) == 0U, "official program changed x0");
  require(emulator.trace().size() == 6U, "official trace length is wrong");
  const auto& last = emulator.trace().back();
  require(last.pc == 12U && last.instruction == 0x00c00067U &&
              last.next_pc == 12U,
          "official trace contents are wrong");
}

void test_jalr_rd_equals_rs1() {
  Emulator emulator;
  load(emulator, {addi(1, 0, 16), jalr(1, 1, 8), 0U, 0U, 0U, 0U,
                  kEbreak});
  require(emulator.step() == Status::Running, "JALR setup failed");
  require(emulator.step() == Status::Running, "JALR rd==rs1 failed");
  require(emulator.pc() == 24U,
          "JALR rd==rs1 did not use the old rs1 value for its target");
  require(emulator.reg(1) == 8U,
          "JALR rd==rs1 did not write old_pc + 4 to rd");
  run_to_ebreak(emulator);
}

void test_negative_addi() {
  Emulator emulator;
  load(emulator, {addi(5, 0, -1), kEbreak});
  run_to_ebreak(emulator);
  require(emulator.reg(5) == 0xffffffffU, "ADDI did not sign-extend -1");
}

void test_add_and_overflow() {
  Emulator emulator;
  load(emulator, {addi(1, 0, 7), addi(2, 0, 9), encode_r(3, 1, 2),
                  addi(4, 0, -1), addi(5, 0, 1), encode_r(6, 4, 5),
                  kEbreak});
  run_to_ebreak(emulator);
  require(emulator.reg(3) == 16U, "ADD returned the wrong sum");
  require(emulator.reg(6) == 0U, "ADD did not wrap modulo 2^32");
}

void test_lui() {
  Emulator emulator;
  load(emulator, {lui(7, 0xabcdeU), kEbreak});
  run_to_ebreak(emulator);
  require(emulator.reg(7) == 0xabcde000U, "LUI placement is wrong");
}

void test_lw_sw() {
  Emulator emulator;
  load(emulator, {addi(1, 0, 256), lui(2, 0x12345U), addi(2, 2, 0x678),
                  encode_s(0, 1, 2, 2), lw(3, 1, 0), kEbreak});
  run_to_ebreak(emulator);
  std::uint32_t stored = 0;
  require(emulator.read_word(256, stored), "could not inspect stored word");
  require(stored == 0x12345678U, "SW wrote the wrong little-endian word");
  require(emulator.reg(3) == 0x12345678U, "LW read the wrong word");
}

void test_lw_negative_offset() {
  Emulator emulator;
  require(emulator.write_word(256, 0x89abcdefU), "test setup failed");
  load(emulator, {addi(1, 0, 264), lw(2, 1, -8), kEbreak});
  run_to_ebreak(emulator);
  require(emulator.reg(2) == 0x89abcdefU,
          "LW did not sign-extend its negative offset");
}

void test_sw_negative_offset() {
  Emulator emulator;
  load(emulator, {addi(1, 0, 264), lui(2, 0x12345U), addi(2, 2, 0x678),
                  encode_s(-8, 1, 2, 2), kEbreak});
  run_to_ebreak(emulator);
  std::uint32_t stored = 0;
  require(emulator.read_word(256, stored), "could not inspect stored word");
  require(stored == 0x12345678U,
          "SW did not sign-extend its negative offset");
}

void test_lbu() {
  Emulator emulator;
  require(emulator.write_word(256, 0x12345678U), "test setup failed");
  load(emulator, {addi(1, 0, 256), lbu(2, 1, 0), lbu(3, 1, 1),
                  lbu(4, 1, 2), lbu(5, 1, 3), kEbreak});
  run_to_ebreak(emulator);
  require(emulator.reg(2) == 0x78U, "LBU a+0 failed");
  require(emulator.reg(3) == 0x56U, "LBU a+1 failed");
  require(emulator.reg(4) == 0x34U, "LBU a+2 failed");
  require(emulator.reg(5) == 0x12U, "LBU a+3 failed");
}

void test_sb() {
  Emulator emulator;
  require(emulator.write_word(256, 0x12345678U), "test setup failed");
  load(emulator, {addi(1, 0, 256), addi(2, 0, -17), encode_s(0, 1, 2, 0),
                  addi(2, 0, -51), encode_s(1, 1, 2, 0),
                  addi(2, 0, -85), encode_s(2, 1, 2, 0),
                  addi(2, 0, -112), encode_s(3, 1, 2, 0), kEbreak});
  run_to_ebreak(emulator);
  std::uint32_t stored = 0;
  require(emulator.read_word(256, stored), "could not inspect SB result");
  require(stored == 0x90abcdefU, "SB sequence did not make 0x90abcdef");
}

void test_x0() {
  Emulator emulator;
  load(emulator, {addi(0, 0, 123), lui(0, 0xfffffU),
                  encode_r(0, 0, 0), jalr(0, 0, 16), kEbreak});
  require(emulator.step() == Status::Running, "x0 ADDI failed");
  require(emulator.reg(0) == 0U, "ADDI changed x0");
  require(emulator.step() == Status::Running, "x0 LUI failed");
  require(emulator.reg(0) == 0U, "LUI changed x0");
  require(emulator.step() == Status::Running, "x0 ADD failed");
  require(emulator.reg(0) == 0U, "ADD changed x0");
  require(emulator.step() == Status::Running, "x0 JALR failed");
  require(emulator.reg(0) == 0U, "JALR changed x0");
  run_to_ebreak(emulator);
}

void test_invalid_rv32e_register() {
  Emulator emulator;
  load(emulator, {addi(16, 0, 1)});
  require(emulator.step() == Status::IllegalInstruction,
          "x16 encoding was not rejected");
  require(emulator.error_message().find("x16") != std::string::npos,
          "invalid register diagnostic is not predictable");
  require(emulator.trace().empty(), "illegal instruction was retired");
}

void test_invalid_rv32e_rs1() {
  Emulator emulator;
  load(emulator, {addi(1, 16, 1)});
  require(emulator.step() == Status::IllegalInstruction,
          "x16 rs1 encoding was not rejected");
  require(emulator.error_message().find("rs1=x16") != std::string::npos,
          "invalid rs1 diagnostic is not predictable");
  require(emulator.trace().empty(), "invalid-rs1 instruction was retired");
}

void test_invalid_rv32e_rs2() {
  Emulator emulator;
  load(emulator, {encode_r(1, 0, 16)});
  require(emulator.step() == Status::IllegalInstruction,
          "x16 rs2 encoding was not rejected");
  require(emulator.error_message().find("rs2=x16") != std::string::npos,
          "invalid rs2 diagnostic is not predictable");
  require(emulator.trace().empty(), "invalid-rs2 instruction was retired");
}

void require_illegal_encoding(std::uint32_t instruction,
                              const std::string& description) {
  Emulator emulator;
  load(emulator, {instruction});
  require(emulator.step() == Status::IllegalInstruction,
          description + " was not rejected");
  require(emulator.trace().empty(), description + " was retired");
}

void test_illegal_add_funct7() {
  require_illegal_encoding(encode_r(1, 2, 3) | (0x20U << 25U),
                           "ADD with unsupported funct7");
}

void test_illegal_load_funct3() {
  require_illegal_encoding(encode_i(0, 1, 1, 2, 0x03U),
                           "load with unsupported funct3");
}

void test_illegal_store_funct3() {
  require_illegal_encoding(encode_s(0, 1, 2, 1),
                           "store with unsupported funct3");
}

void test_illegal_jalr_funct3() {
  require_illegal_encoding(encode_i(0, 1, 1, 2, 0x67U),
                           "JALR with unsupported funct3");
}

void test_ebreak() {
  Emulator emulator;
  load(emulator, {kEbreak});
  require(emulator.run(1) == Status::Halted, "EBREAK did not halt");
  require(emulator.pc() == 4U, "EBREAK did not retire");
  require(emulator.trace().size() == 1U, "EBREAK was not traced");
}

void test_errors_and_bound() {
  Emulator bounded;
  load(bounded, {jalr(0, 0, 0)});
  require(bounded.run(3) == Status::ExecutionLimit,
          "infinite loop did not hit execution limit");

  Emulator misaligned;
  load(misaligned, {addi(1, 0, 257), lw(2, 1, 0)});
  require(misaligned.step() == Status::Running, "setup ADDI failed");
  require(misaligned.step() == Status::MisalignedAccess,
          "misaligned LW was not rejected");

  Emulator outside(16);
  load(outside, {addi(1, 0, 16), lbu(2, 1, 0)});
  require(outside.step() == Status::Running, "setup ADDI failed");
  require(outside.step() == Status::MemoryError,
          "out-of-range LBU was not rejected");
}

void test_misaligned_sw() {
  Emulator emulator;
  load(emulator, {addi(1, 0, 257), encode_s(0, 1, 0, 2)});
  require(emulator.step() == Status::Running, "misaligned SW setup failed");
  require(emulator.step() == Status::MisalignedAccess,
          "misaligned SW was not rejected");
  require(emulator.trace().size() == 1U, "misaligned SW was retired");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
      {"official ADDI/JALR E5 program", test_official_addi_jalr},
      {"JALR rd equals rs1", test_jalr_rd_equals_rs1},
      {"negative ADDI immediate", test_negative_addi},
      {"ADD and overflow", test_add_and_overflow},
      {"LUI", test_lui},
      {"LW/SW", test_lw_sw},
      {"LW negative offset", test_lw_negative_offset},
      {"SW negative offset", test_sw_negative_offset},
      {"LBU", test_lbu},
      {"SB", test_sb},
      {"x0 hardwiring", test_x0},
      {"invalid RV32E register", test_invalid_rv32e_register},
      {"invalid RV32E rs1", test_invalid_rv32e_rs1},
      {"invalid RV32E rs2", test_invalid_rv32e_rs2},
      {"illegal ADD funct7", test_illegal_add_funct7},
      {"illegal load funct3", test_illegal_load_funct3},
      {"illegal store funct3", test_illegal_store_funct3},
      {"illegal JALR funct3", test_illegal_jalr_funct3},
      {"EBREAK", test_ebreak},
      {"errors and execution bound", test_errors_and_bound},
      {"misaligned SW", test_misaligned_sw},
  };

  std::size_t failures = 0;
  for (const auto& test : tests) {
    try {
      test.second();
      std::cout << "[PASS] " << test.first << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
    }
  }
  std::cout << tests.size() - failures << '/' << tests.size()
            << " tests passed\n";
  return failures == 0U ? 0 : 1;
}
