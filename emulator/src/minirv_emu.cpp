#include "minirv_emu.h"

#include <iomanip>
#include <limits>
#include <sstream>

namespace minirv {
namespace {

std::int32_t sign_extend(std::uint32_t value, unsigned bits) {
  const std::uint32_t sign = std::uint32_t{1} << (bits - 1U);
  return static_cast<std::int32_t>((value ^ sign) - sign);
}

std::string hex32(std::uint32_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return stream.str();
}

}  // namespace

Emulator::Emulator(std::size_t memory_size) : memory_(memory_size, 0) {
  reset();
}

void Emulator::reset() {
  pc_ = 0;
  registers_.fill(0);
  status_ = Status::Running;
  error_message_.clear();
  trace_.clear();
  last_event_ = {};
}

bool Emulator::range_valid(std::uint32_t address, std::size_t width) const {
  const std::uint64_t end = static_cast<std::uint64_t>(address) + width;
  return end <= memory_.size();
}

bool Emulator::write_byte(std::uint32_t address, std::uint8_t value) {
  if (!range_valid(address, 1)) {
    return false;
  }
  memory_[address] = value;
  return true;
}

bool Emulator::write_word(std::uint32_t address, std::uint32_t value) {
  if (!range_valid(address, 4)) {
    return false;
  }
  for (unsigned i = 0; i < 4; ++i) {
    memory_[address + i] = static_cast<std::uint8_t>(value >> (8U * i));
  }
  return true;
}

bool Emulator::read_byte(std::uint32_t address, std::uint8_t& value) const {
  if (!range_valid(address, 1)) {
    return false;
  }
  value = memory_[address];
  return true;
}

bool Emulator::read_word(std::uint32_t address, std::uint32_t& value) const {
  if (!range_valid(address, 4)) {
    return false;
  }
  value = 0;
  for (unsigned i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(memory_[address + i]) << (8U * i);
  }
  return true;
}

bool Emulator::load_program(const std::vector<std::uint32_t>& words,
                            std::uint32_t address) {
  if ((address & 3U) != 0U ||
      words.size() > std::numeric_limits<std::size_t>::max() / 4U ||
      !range_valid(address, words.size() * 4U)) {
    return false;
  }
  for (std::size_t i = 0; i < words.size(); ++i) {
    if (!write_word(address + static_cast<std::uint32_t>(i * 4U), words[i])) {
      return false;
    }
  }
  return true;
}

std::uint32_t Emulator::reg(std::size_t index) const {
  return index < registers_.size() ? registers_[index] : 0;
}

Status Emulator::fail(Status status, const std::string& message) {
  status_ = status;
  error_message_ = message;
  registers_[0] = 0;
  return status_;
}

Status Emulator::step() {
  last_event_ = {};
  last_event_.valid = true;
  last_event_.pc = pc_;
  last_event_.next_pc = pc_;
  if (status_ != Status::Running) {
    return status_;
  }
  if ((pc_ & 3U) != 0U) {
    last_event_.trap_cause = TrapCause::InstructionAddressMisaligned;
    return fail(Status::MisalignedAccess,
                "misaligned instruction fetch at " + hex32(pc_));
  }

  std::uint32_t instruction = 0;
  if (!read_word(pc_, instruction)) {
    return fail(Status::MemoryError,
                "instruction fetch outside memory at " + hex32(pc_));
  }
  last_event_.instruction = instruction;

  const std::uint32_t old_pc = pc_;
  std::uint32_t next_pc = old_pc + 4U;
  const std::uint32_t opcode = instruction & 0x7fU;
  const std::uint32_t rd = (instruction >> 7U) & 0x1fU;
  const std::uint32_t funct3 = (instruction >> 12U) & 7U;
  const std::uint32_t rs1 = (instruction >> 15U) & 0x1fU;
  const std::uint32_t rs2 = (instruction >> 20U) & 0x1fU;
  const std::uint32_t funct7 = instruction >> 25U;

  auto invalid_register = [&](std::uint32_t index, const char* field) {
    last_event_.trap_cause = TrapCause::IllegalInstruction;
    return fail(Status::IllegalInstruction,
                std::string("RV32E register ") + field + "=x" +
                    std::to_string(index) + " is invalid at " + hex32(old_pc));
  };
  auto check_rd_rs1 = [&]() -> bool {
    if (rd >= registers_.size()) {
      invalid_register(rd, "rd");
      return false;
    }
    if (rs1 >= registers_.size()) {
      invalid_register(rs1, "rs1");
      return false;
    }
    return true;
  };
  auto write_register = [&](std::uint32_t index, std::uint32_t value) {
    if (index != 0U) {
      registers_[index] = value;
      last_event_.rd_wen = true;
      last_event_.rd = index;
      last_event_.rd_data = value;
    }
  };

  if (instruction == 0x00100073U) {  // EBREAK
    status_ = Status::Halted;
  } else if (opcode == 0x33U && funct3 == 0U && funct7 == 0U) {  // ADD
    if (!check_rd_rs1()) {
      return status_;
    }
    if (rs2 >= registers_.size()) {
      return invalid_register(rs2, "rs2");
    }
    write_register(rd, registers_[rs1] + registers_[rs2]);
  } else if (opcode == 0x13U && funct3 == 0U) {  // ADDI
    if (!check_rd_rs1()) {
      return status_;
    }
    const std::int32_t immediate = sign_extend(instruction >> 20U, 12);
    write_register(rd, registers_[rs1] + static_cast<std::uint32_t>(immediate));
  } else if (opcode == 0x13U && funct3 == 3U) {  // SLTIU
    if (!check_rd_rs1()) {
      return status_;
    }
    const std::uint32_t immediate = static_cast<std::uint32_t>(
        sign_extend(instruction >> 20U, 12));
    write_register(rd, registers_[rs1] < immediate ? 1U : 0U);
  } else if (opcode == 0x37U) {  // LUI
    if (rd >= registers_.size()) {
      return invalid_register(rd, "rd");
    }
    write_register(rd, instruction & 0xfffff000U);
  } else if (opcode == 0x17U) {  // AUIPC
    if (rd >= registers_.size()) {
      return invalid_register(rd, "rd");
    }
    write_register(rd, old_pc + (instruction & 0xfffff000U));
  } else if (opcode == 0x63U && (funct3 == 0U || funct3 == 1U)) {  // BEQ/BNE
    if (rs1 >= registers_.size()) {
      return invalid_register(rs1, "rs1");
    }
    if (rs2 >= registers_.size()) {
      return invalid_register(rs2, "rs2");
    }
    const std::uint32_t immediate_bits =
        ((instruction >> 31U) << 12U) |
        (((instruction >> 7U) & 1U) << 11U) |
        (((instruction >> 25U) & 0x3fU) << 5U) |
        (((instruction >> 8U) & 0x0fU) << 1U);
    const bool equal = registers_[rs1] == registers_[rs2];
    const bool taken = funct3 == 0U ? equal : !equal;
    if (taken) {
      next_pc = old_pc + static_cast<std::uint32_t>(
                             sign_extend(immediate_bits, 13));
      if ((next_pc & 3U) != 0U) {
        last_event_.trap_cause = TrapCause::InstructionAddressMisaligned;
        return fail(Status::MisalignedAccess,
                    "misaligned branch target " + hex32(next_pc));
      }
    }
  } else if (opcode == 0x03U && (funct3 == 2U || funct3 == 4U)) {  // LW/LBU
    if (!check_rd_rs1()) {
      return status_;
    }
    const std::int32_t immediate = sign_extend(instruction >> 20U, 12);
    const std::uint32_t address =
        registers_[rs1] + static_cast<std::uint32_t>(immediate);
    if (funct3 == 2U) {
      if ((address & 3U) != 0U) {
        last_event_.trap_cause = TrapCause::LoadAddressMisaligned;
        return fail(Status::MisalignedAccess,
                    "misaligned LW address " + hex32(address));
      }
      std::uint32_t value = 0;
      if (!read_word(address, value)) {
        return fail(Status::MemoryError,
                    "LW outside memory at " + hex32(address));
      }
      write_register(rd, value);
    } else {
      std::uint8_t value = 0;
      if (!read_byte(address, value)) {
        return fail(Status::MemoryError,
                    "LBU outside memory at " + hex32(address));
      }
      write_register(rd, value);
    }
  } else if (opcode == 0x23U && (funct3 == 2U || funct3 == 0U)) {  // SW/SB
    if (rs1 >= registers_.size()) {
      return invalid_register(rs1, "rs1");
    }
    if (rs2 >= registers_.size()) {
      return invalid_register(rs2, "rs2");
    }
    const std::uint32_t immediate_bits =
        ((instruction >> 25U) << 5U) | ((instruction >> 7U) & 0x1fU);
    const std::int32_t immediate = sign_extend(immediate_bits, 12);
    const std::uint32_t address =
        registers_[rs1] + static_cast<std::uint32_t>(immediate);
    if (funct3 == 2U) {
      if ((address & 3U) != 0U) {
        last_event_.trap_cause = TrapCause::StoreAddressMisaligned;
        return fail(Status::MisalignedAccess,
                    "misaligned SW address " + hex32(address));
      }
      if (!write_word(address, registers_[rs2])) {
        return fail(Status::MemoryError,
                    "SW outside memory at " + hex32(address));
      }
      last_event_.mem_wen = true;
      last_event_.mem_addr = address;
      last_event_.mem_wmask = 0x0fU;
      last_event_.mem_wdata = registers_[rs2];
    } else if (!write_byte(address,
                           static_cast<std::uint8_t>(registers_[rs2]))) {
      return fail(Status::MemoryError,
                  "SB outside memory at " + hex32(address));
    } else {
      last_event_.mem_wen = true;
      last_event_.mem_addr = address;
      last_event_.mem_wmask = 0x01U;
      last_event_.mem_wdata = registers_[rs2];
    }
  } else if (opcode == 0x67U && funct3 == 0U) {  // JALR
    if (!check_rd_rs1()) {
      return status_;
    }
    const std::uint32_t old_rs1 = registers_[rs1];
    const std::int32_t immediate = sign_extend(instruction >> 20U, 12);
    const std::uint32_t return_address = old_pc + 4U;
    next_pc = (old_rs1 + static_cast<std::uint32_t>(immediate)) & ~1U;
    write_register(rd, return_address);
  } else {
    last_event_.trap_cause = TrapCause::IllegalInstruction;
    return fail(Status::IllegalInstruction,
                "illegal instruction " + hex32(instruction) + " at " +
                    hex32(old_pc));
  }

  pc_ = next_pc;
  registers_[0] = 0;
  trace_.push_back({old_pc, instruction, next_pc});
  last_event_.retired = true;
  last_event_.next_pc = next_pc;
  last_event_.ebreak = instruction == 0x00100073U;
  return status_;
}

Status Emulator::run(std::size_t max_instructions) {
  if (status_ != Status::Running) {
    return status_;
  }
  for (std::size_t count = 0; count < max_instructions; ++count) {
    const Status result = step();
    if (result != Status::Running) {
      return result;
    }
  }
  return fail(Status::ExecutionLimit,
              "execution limit of " + std::to_string(max_instructions) +
                  " instructions reached");
}

const char* status_name(Status status) {
  switch (status) {
    case Status::Running: return "running";
    case Status::Halted: return "halted";
    case Status::IllegalInstruction: return "illegal instruction";
    case Status::MemoryError: return "memory error";
    case Status::MisalignedAccess: return "misaligned access";
    case Status::ExecutionLimit: return "execution limit";
  }
  return "unknown";
}

}  // namespace minirv
