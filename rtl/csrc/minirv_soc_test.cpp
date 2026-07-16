#include "Vminirv_soc.h"
#include "minirv_encoding.h"
#include "verilated.h"
#include "verilated_fst_c.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using minirv_encoding::addi;
using minirv_encoding::beq;
using minirv_encoding::encode_s;
using minirv_encoding::kEbreak;
using minirv_encoding::lui;
using minirv_encoding::lw;

constexpr std::uint32_t kRamBase = 0x80000000U;
constexpr std::size_t kRamBytes = 64U * 1024U;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

class Rig {
 public:
  explicit Rig(bool waveform = false)
      : context_(std::make_unique<VerilatedContext>()),
        dut_(std::make_unique<Vminirv_soc>(context_.get())),
        ram_(kRamBytes, 0) {
    context_->traceEverOn(waveform);
    if (waveform) {
      trace_ = std::make_unique<VerilatedFstC>();
      dut_->trace(trace_.get(), 8);
      trace_->open("build/minirv_soc.fst");
    }
    dut_->clk = 0;
    dut_->reset = 1;
    dut_->gpio_input = 0;
    dut_->debug_gpr_addr = 0;
    tick();
    tick();
    dut_->reset = 0;
    settle();
  }

  ~Rig() {
    dut_->final();
    if (trace_) trace_->close();
  }

  void load(const std::vector<std::uint32_t>& words) {
    program_ = words;
    settle();
  }

  void set_gpio_input(std::uint32_t value) {
    dut_->gpio_input = value;
    settle();
  }

  void cycle() {
    require(cycles_++ < 500U, "SoC cycle limit exceeded");
    tick();
    if (dut_->commit_valid && dut_->commit_mem_wen && dut_->ram_valid) {
      apply_ram_store(dut_->commit_mem_addr, dut_->commit_mem_wdata,
                      static_cast<std::uint8_t>(dut_->commit_mem_wmask));
      settle();
    }
  }

  void run_to_halt() {
    while (!dut_->halted) {
      require(!dut_->bus_error, "unexpected unmapped bus access");
      cycle();
    }
    require(!dut_->trap_valid, "unexpected architectural trap");
  }

  std::uint32_t reg(std::uint8_t index) {
    dut_->debug_gpr_addr = index;
    settle();
    return dut_->debug_gpr_data;
  }

  Vminirv_soc& dut() { return *dut_; }

 private:
  std::uint32_t instruction(std::uint32_t address) const {
    const std::size_t index = address / 4U;
    return index < program_.size() ? program_[index] : 0U;
  }

  std::size_t ram_offset(std::uint32_t address, std::size_t width) const {
    require(address >= kRamBase, "RAM access below base");
    const std::uint64_t offset =
        static_cast<std::uint64_t>(address) - kRamBase;
    require(offset + width <= ram_.size(), "RAM access outside test storage");
    return static_cast<std::size_t>(offset);
  }

  std::uint32_t read_ram(std::uint32_t address) const {
    const std::size_t offset = ram_offset(address, 4);
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
      value |= static_cast<std::uint32_t>(ram_[offset + byte]) << (8U * byte);
    }
    return value;
  }

  void apply_ram_store(std::uint32_t address, std::uint32_t value,
                       std::uint8_t mask) {
    const std::size_t offset = ram_offset(address, 4);
    for (unsigned byte = 0; byte < 4; ++byte) {
      if ((mask & (1U << byte)) != 0U) {
        ram_[offset + byte] = static_cast<std::uint8_t>(value >> (8U * byte));
      }
    }
  }

  void drive_instruction() {
    dut_->imem_rdata = dut_->reset ? 0U : instruction(dut_->imem_addr);
  }

  void drive_ram() {
    if (!dut_->reset && dut_->ram_valid && !dut_->ram_write) {
      dut_->ram_rdata = read_ram(dut_->ram_addr);
    } else {
      dut_->ram_rdata = 0;
    }
  }

  void settle() {
    for (int iteration = 0; iteration < 3; ++iteration) {
      drive_instruction();
      dut_->eval();
      drive_ram();
      dut_->eval();
    }
  }

  void dump() {
    if (trace_) trace_->dump(context_->time());
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
  std::unique_ptr<Vminirv_soc> dut_;
  std::unique_ptr<VerilatedFstC> trace_;
  std::vector<std::uint32_t> program_;
  std::vector<std::uint8_t> ram_;
  std::size_t cycles_ = 0;
};

std::uint32_t sw(std::uint32_t rs2, std::uint32_t rs1, std::int32_t offset) {
  return encode_s(offset, rs1, rs2, 2);
}

std::uint32_t sb(std::uint32_t rs2, std::uint32_t rs1, std::int32_t offset) {
  return encode_s(offset, rs1, rs2, 0);
}

void test_loopback(bool waveform) {
  Rig rig(waveform);
  rig.set_gpio_input(0x5aU);
  rig.load({lui(1, 0x10000U), lw(2, 1, 0), sw(2, 1, 4), lw(3, 1, 4),
            beq(2, 3, 12), addi(10, 0, 1), kEbreak,
            addi(10, 0, 0), kEbreak});
  rig.run_to_halt();
  require(rig.reg(2) == 0x5aU, "GPIO input LW failed");
  require(rig.reg(3) == 0x5aU, "GPIO output readback failed");
  require(rig.dut().gpio_output == 0x5aU, "GPIO loopback output failed");
  require(rig.reg(10) == 0U, "loopback returned a bad trap code");
  std::cout << "GPIO input:  0x" << std::hex << std::setw(8)
            << std::setfill('0') << rig.dut().gpio_input << '\n'
            << "GPIO output: 0x" << std::setw(8) << rig.dut().gpio_output
            << std::dec << "\nHIT GOOD TRAP\n";
}

void test_gpio_word_and_readback(bool) {
  Rig rig;
  rig.load({lui(1, 0x10000U), lui(2, 0x12345U), addi(2, 2, 0x678),
            sw(2, 1, 4), lw(3, 1, 4), kEbreak});
  rig.run_to_halt();
  require(rig.dut().gpio_output == 0x12345678U, "GPIO SW failed");
  require(rig.reg(3) == 0x12345678U, "GPIO readback after SW failed");
}

void test_gpio_byte_masks(bool) {
  Rig rig;
  rig.load({lui(1, 0x10000U), lui(2, 0x11223U), addi(2, 2, 0x344),
            sw(2, 1, 4), addi(2, 0, 0xaa), sb(2, 1, 5),
            addi(2, 0, 0xbb), sb(2, 1, 7), lw(3, 1, 4), kEbreak});
  rig.run_to_halt();
  require(rig.dut().gpio_output == 0xbb22aa44U,
          "GPIO byte write masks changed untouched bytes");
  require(rig.reg(3) == 0xbb22aa44U, "GPIO byte-write readback failed");
}

void test_gpio_input_is_read_only(bool) {
  Rig rig;
  rig.set_gpio_input(0x5aU);
  rig.load({lui(1, 0x10000U), addi(2, 0, 0x33), sw(2, 1, 4),
            addi(2, 0, 0x77), sw(2, 1, 0), kEbreak});
  rig.run_to_halt();
  require(rig.dut().gpio_input == 0x5aU, "write changed GPIO input");
  require(rig.dut().gpio_output == 0x33U,
          "write to GPIO input changed GPIO output");
}

void test_ram_through_bus(bool) {
  Rig rig;
  rig.load({lui(1, 0x80000U), lui(2, 0x12345U), addi(2, 2, 0x678),
            sw(2, 1, 256), lw(3, 1, 256), kEbreak});
  rig.run_to_halt();
  require(rig.reg(3) == 0x12345678U, "RAM access through bus failed");
}

void test_unmapped_access(bool) {
  Rig rig;
  rig.load({lui(1, 0x20000U), lw(2, 1, 0)});
  rig.cycle();
  require(rig.dut().bus_error, "unmapped access did not assert bus_error");
  require(!rig.dut().ram_valid, "unmapped access selected RAM");
}

using Test = std::pair<std::string, std::function<void(bool)>>;

}  // namespace

int main(int argc, char** argv) {
  bool waveform = false;
  for (int index = 1; index < argc; ++index) {
    if (std::string(argv[index]) == "--fst") waveform = true;
    else return 2;
  }
  const std::array<Test, 6> tests = {{
      {"integrated GPIO loopback", test_loopback},
      {"GPIO word write and readback", test_gpio_word_and_readback},
      {"GPIO byte write masks", test_gpio_byte_masks},
      {"GPIO input read-only", test_gpio_input_is_read_only},
      {"RAM through decoder", test_ram_through_bus},
      {"unmapped bus error", test_unmapped_access},
  }};
  const std::size_t count = waveform ? 1U : tests.size();
  std::size_t failures = 0;
  for (std::size_t index = 0; index < count; ++index) {
    try {
      tests[index].second(waveform);
      std::cout << "[PASS] " << tests[index].first << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << tests[index].first << ": " << error.what()
                << '\n';
    }
  }
  std::cout << count - failures << '/' << count << " SoC tests passed\n";
  return failures == 0U ? 0 : 1;
}
