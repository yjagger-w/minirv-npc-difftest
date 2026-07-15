#ifndef MINIRV_MEMORY_H
#define MINIRV_MEMORY_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

class MiniRVMemory {
 public:
  explicit MiniRVMemory(std::size_t size = 64U * 1024U) : bytes_(size, 0) {}

  void write_byte(std::uint32_t address, std::uint8_t value) {
    check(address, 1, "byte write");
    bytes_[address] = value;
  }

  void write_word(std::uint32_t address, std::uint32_t value) {
    check(address, 4, "word write");
    for (unsigned index = 0; index < 4; ++index) {
      bytes_[address + index] =
          static_cast<std::uint8_t>(value >> (8U * index));
    }
  }

  std::uint8_t read_byte(std::uint32_t address) const {
    check(address, 1, "byte read");
    return bytes_[address];
  }

  std::uint32_t read_word(std::uint32_t address) const {
    check(address, 4, "word read");
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
      value |= static_cast<std::uint32_t>(bytes_[address + index])
               << (8U * index);
    }
    return value;
  }

  void load_program(const std::vector<std::uint32_t>& words,
                    std::uint32_t address = 0) {
    for (const std::uint32_t word : words) {
      write_word(address, word);
      address += 4U;
    }
  }

  void apply_store(std::uint32_t address, std::uint32_t data,
                   std::uint8_t mask) {
    for (unsigned index = 0; index < 4; ++index) {
      if ((mask & (1U << index)) != 0U) {
        write_byte(address + index,
                   static_cast<std::uint8_t>(data >> (8U * index)));
      }
    }
  }

 private:
  void check(std::uint32_t address, std::size_t width,
             const char* operation) const {
    const std::uint64_t end = static_cast<std::uint64_t>(address) + width;
    if (end > bytes_.size()) {
      throw std::out_of_range(std::string("host memory ") + operation +
                              " out of range at address " +
                              std::to_string(address));
    }
  }

  std::vector<std::uint8_t> bytes_;
};

#endif
