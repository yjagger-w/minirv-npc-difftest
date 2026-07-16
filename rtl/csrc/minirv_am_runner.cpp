#include "Vminirv_core.h"
#include "verilated.h"
#include "verilated_fst_c.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kMemoryBase = 0x80000000U;
constexpr std::size_t kMemorySize = 128U * 1024U * 1024U;
constexpr std::size_t kDefaultCycleLimit = 100000U;

class GuestMemory {
 public:
  GuestMemory() : bytes_(kMemorySize, 0) {}

  std::size_t load_binary(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open raw binary: " + path);
    const std::streamsize length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > bytes_.size()) {
      throw std::runtime_error("raw binary exceeds 128 MiB guest memory");
    }
    if (length == 0) throw std::runtime_error("raw binary is empty: " + path);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes_.data()), length);
    if (!input) throw std::runtime_error("failed while reading raw binary");
    return static_cast<std::size_t>(length);
  }

  std::uint32_t read_word(std::uint32_t address) const {
    const std::size_t offset = translate(address, 4, "word read");
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
      value |= static_cast<std::uint32_t>(bytes_[offset + index])
               << (8U * index);
    }
    return value;
  }

  void apply_store(std::uint32_t address, std::uint32_t data,
                   std::uint8_t mask) {
    const std::size_t offset = translate(address, 4, "store");
    for (unsigned index = 0; index < 4; ++index) {
      if ((mask & (1U << index)) != 0U) {
        bytes_[offset + index] =
            static_cast<std::uint8_t>(data >> (8U * index));
      }
    }
  }

 private:
  std::size_t translate(std::uint32_t address, std::size_t width,
                        const char* operation) const {
    if (address < kMemoryBase) {
      throw std::out_of_range(std::string(operation) +
                              " below guest memory base");
    }
    const std::uint64_t offset =
        static_cast<std::uint64_t>(address) - kMemoryBase;
    if (offset + width > bytes_.size()) {
      throw std::out_of_range(std::string(operation) +
                              " beyond guest memory limit");
    }
    return static_cast<std::size_t>(offset);
  }

  std::vector<std::uint8_t> bytes_;
};

class Runner {
 public:
  Runner(const std::string& image, std::size_t cycle_limit, bool waveform)
      : context_(std::make_unique<VerilatedContext>()),
        dut_(std::make_unique<Vminirv_core>(context_.get())),
        cycle_limit_(cycle_limit) {
    const std::size_t image_size = memory_.load_binary(image);
    std::cout << "NPC image: " << image << '\n'
              << "Image size: " << image_size << " bytes\n"
              << "Guest load address: 0x" << std::hex << std::setw(8)
              << std::setfill('0') << kMemoryBase << std::dec << '\n'
              << "Cycle limit: " << cycle_limit_ << '\n'
              << std::flush;
    context_->traceEverOn(waveform);
    if (waveform) {
      trace_ = std::make_unique<VerilatedFstC>();
      dut_->trace(trace_.get(), 5);
      trace_->open("build/minirv_am.fst");
    }
    dut_->clk = 0;
    dut_->reset = 1;
    dut_->debug_gpr_addr = 0;
    tick();
    tick();
    dut_->reset = 0;
    settle();
  }

  ~Runner() {
    dut_->final();
    if (trace_) trace_->close();
  }

  int run() {
    for (std::size_t cycle = 0; cycle < cycle_limit_; ++cycle) {
      tick();
      if (dut_->commit_valid && dut_->commit_mem_wen) {
        memory_.apply_store(dut_->commit_mem_addr, dut_->commit_mem_wdata,
                            static_cast<std::uint8_t>(dut_->commit_mem_wmask));
        settle();
      }
      if (dut_->trap_valid) {
        std::cerr << "NPC architectural trap: cause=0x" << std::hex
                  << static_cast<unsigned>(dut_->trap_cause) << " pc=0x"
                  << std::setw(8) << std::setfill('0') << dut_->imem_addr
                  << " instruction=0x" << std::setw(8) << dut_->imem_rdata
                  << std::dec << '\n';
        return 2;
      }
      if (dut_->ebreak_valid) {
        dut_->debug_gpr_addr = 10;
        settle();
        const std::uint32_t code = dut_->debug_gpr_data;
        if (code == 0U) {
          std::cout << "HIT GOOD TRAP\n";
          return 0;
        }
        std::cerr << "HIT BAD TRAP: code=" << code << '\n';
        return 1;
      }
    }
    std::cerr << "NPC timeout after " << cycle_limit_ << " cycles\n";
    return 3;
  }

 private:
  void drive_instruction_memory() {
    if (dut_->reset) {
      dut_->imem_rdata = 0;
      return;
    }
    dut_->imem_rdata = memory_.read_word(dut_->imem_addr);
  }

  void drive_data_memory() {
    if (!dut_->reset && dut_->dmem_valid && !dut_->dmem_write) {
      dut_->dmem_rdata = memory_.read_word(dut_->dmem_addr);
    } else {
      dut_->dmem_rdata = 0;
    }
  }

  void settle(bool suppress_first_data = false) {
    for (int iteration = 0; iteration < 3; ++iteration) {
      drive_instruction_memory();
      dut_->eval();
      if (!suppress_first_data || iteration != 0) {
        drive_data_memory();
      }
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
    settle(true);
    dump();
    dut_->clk = 0;
    settle();
    dump();
  }

  std::unique_ptr<VerilatedContext> context_;
  std::unique_ptr<Vminirv_core> dut_;
  std::unique_ptr<VerilatedFstC> trace_;
  GuestMemory memory_;
  std::size_t cycle_limit_;
};

void print_usage(const char* program) {
  std::cerr << "usage: " << program
            << " IMAGE.bin [--max-cycles N] [--fst]\n";
}

bool parse_cycle_limit(const std::string& text, std::size_t& value) {
  std::uint64_t parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, parsed, 10);
  if (text.empty() || result.ec != std::errc{} || result.ptr != end ||
      parsed == 0U || parsed > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  value = static_cast<std::size_t>(parsed);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  bool waveform = false;
  std::string image;
  std::size_t cycle_limit = kDefaultCycleLimit;
  bool cycle_limit_seen = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--fst") {
      waveform = true;
    } else if (argument == "--max-cycles") {
      if (cycle_limit_seen || index + 1 >= argc ||
          !parse_cycle_limit(argv[++index], cycle_limit)) {
        std::cerr << "invalid --max-cycles: expected one positive integer\n";
        print_usage(argv[0]);
        return 2;
      }
      cycle_limit_seen = true;
    } else if (argument.rfind("--", 0) == 0U) {
      std::cerr << "unknown option: " << argument << '\n';
      print_usage(argv[0]);
      return 2;
    } else if (image.empty()) {
      image = argument;
    } else {
      print_usage(argv[0]);
      return 2;
    }
  }
  if (image.empty()) {
    print_usage(argv[0]);
    return 2;
  }
  try {
    Runner runner(image, cycle_limit, waveform);
    return runner.run();
  } catch (const std::exception& error) {
    std::cerr << "NPC runner error: " << error.what() << '\n';
    return 2;
  }
}
