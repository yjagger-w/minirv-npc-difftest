#ifndef MINIRV_ENCODING_H
#define MINIRV_ENCODING_H

#include <cstdint>

namespace minirv_encoding {

constexpr std::uint32_t kEbreak = 0x00100073U;

inline std::uint32_t encode_r(std::uint32_t rd, std::uint32_t rs1,
                              std::uint32_t rs2) {
  return (rs2 << 20U) | (rs1 << 15U) | (rd << 7U) | 0x33U;
}

inline std::uint32_t encode_i(std::int32_t immediate, std::uint32_t rs1,
                              std::uint32_t funct3, std::uint32_t rd,
                              std::uint32_t opcode) {
  return ((static_cast<std::uint32_t>(immediate) & 0xfffU) << 20U) |
         (rs1 << 15U) | (funct3 << 12U) | (rd << 7U) | opcode;
}

inline std::uint32_t encode_s(std::int32_t immediate, std::uint32_t rs1,
                              std::uint32_t rs2, std::uint32_t funct3) {
  const std::uint32_t bits = static_cast<std::uint32_t>(immediate) & 0xfffU;
  return ((bits >> 5U) << 25U) | (rs2 << 20U) | (rs1 << 15U) |
         (funct3 << 12U) | ((bits & 0x1fU) << 7U) | 0x23U;
}

inline std::uint32_t addi(std::uint32_t rd, std::uint32_t rs1,
                          std::int32_t immediate) {
  return encode_i(immediate, rs1, 0, rd, 0x13U);
}

inline std::uint32_t lui(std::uint32_t rd, std::uint32_t upper) {
  return (upper << 12U) | (rd << 7U) | 0x37U;
}

inline std::uint32_t lw(std::uint32_t rd, std::uint32_t rs1,
                        std::int32_t immediate) {
  return encode_i(immediate, rs1, 2, rd, 0x03U);
}

inline std::uint32_t lbu(std::uint32_t rd, std::uint32_t rs1,
                         std::int32_t immediate) {
  return encode_i(immediate, rs1, 4, rd, 0x03U);
}

inline std::uint32_t jalr(std::uint32_t rd, std::uint32_t rs1,
                          std::int32_t immediate) {
  return encode_i(immediate, rs1, 0, rd, 0x67U);
}

}  // namespace minirv_encoding

#endif
