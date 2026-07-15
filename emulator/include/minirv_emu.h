#ifndef MINIRV_EMU_H
#define MINIRV_EMU_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace minirv {

enum class Status {
  Running,
  Halted,
  IllegalInstruction,
  MemoryError,
  MisalignedAccess,
  ExecutionLimit,
};

struct TraceEntry {
  std::uint32_t pc;
  std::uint32_t instruction;
  std::uint32_t next_pc;
};

class Emulator {
 public:
  explicit Emulator(std::size_t memory_size = 64U * 1024U);

  void reset();
  bool load_program(const std::vector<std::uint32_t>& words,
                    std::uint32_t address = 0);
  bool write_byte(std::uint32_t address, std::uint8_t value);
  bool write_word(std::uint32_t address, std::uint32_t value);
  bool read_byte(std::uint32_t address, std::uint8_t& value) const;
  bool read_word(std::uint32_t address, std::uint32_t& value) const;

  Status step();
  Status run(std::size_t max_instructions);

  std::uint32_t pc() const { return pc_; }
  std::uint32_t reg(std::size_t index) const;
  Status status() const { return status_; }
  const std::string& error_message() const { return error_message_; }
  const std::vector<TraceEntry>& trace() const { return trace_; }

 private:
  bool range_valid(std::uint32_t address, std::size_t width) const;
  Status fail(Status status, const std::string& message);

  std::uint32_t pc_ = 0;
  std::array<std::uint32_t, 16> registers_{};
  std::vector<std::uint8_t> memory_;
  Status status_ = Status::Running;
  std::string error_message_;
  std::vector<TraceEntry> trace_;
};

const char* status_name(Status status);

}  // namespace minirv

#endif
