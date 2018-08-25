#ifndef ARCH_ARM64_REGISTERS
#define ARCH_ARM64_REGISTERS

#include "src/arch/arm64/constants-arm64.h"
#include "src/globals.h"

namespace zz {
namespace arm64 {

class CPURegister {
public:
  enum RegisterType {
    Register_32,
    Register_W = Register_32,
    Register_64,
    Register_X = Register_64,

    SIMD_FP_Register_8,
    SIMD_FP_Register_B = SIMD_FP_Register_8,
    SIMD_FP_Register_16,
    SIMD_FP_Register_H = SIMD_FP_Register_16,
    SIMD_FP_Register_32,
    SIMD_FP_Register_S = SIMD_FP_Register_32,
    SIMD_FP_Register_64,
    SIMD_FP_Register_D = SIMD_FP_Register_64,
    SIMD_FP_Register_128,
    SIMD_FP_Register_Q = SIMD_FP_Register_128
  };
  CPURegister(int code, int size, RegisterType type) : reg_index_(code), reg_type_(type) {
  }

  static CPURegister Create(int code, int size, RegisterType type) {
    return CPURegister(code, size, type);
  }

  int32_t code() {
    return reg_index_;
  };

  bool Is64Bits() {
    return reg_type_ == Register_64;
  };

  RegisterType type() {
    return reg_type_;
  };

private:
  RegisterType reg_type_;
  int reg_index_;
};

typedef CPURegister Register;
typedef CPURegister VRegister;

// clang-format off
#define GENERAL_REGISTER_CODE_LIST(R)                     \
  R(0)  R(1)  R(2)  R(3)  R(4)  R(5)  R(6)  R(7)          \
  R(8)  R(9)  R(10) R(11) R(12) R(13) R(14) R(15)         \
  R(16) R(17) R(18) R(19) R(20) R(21) R(22) R(23)         \
  R(24) R(25) R(26) R(27) R(28) R(29) R(30) R(31)

#define DEFINE_REGISTER(register_class, name, ...) register_class name = register_class::Create(__VA_ARGS__)
#define ALIAS_REGISTER(register_class, alias, name) register_class alias = name

#define DEFINE_REGISTERS(N)                                                                                            \
  DEFINE_REGISTER(CPURegister, w##N, N, 32, CPURegister::Register_32);                                                                 \
  DEFINE_REGISTER(CPURegister, x##N, N, 64, CPURegister::Register_64);
GENERAL_REGISTER_CODE_LIST(DEFINE_REGISTERS)
#undef DEFINE_REGISTERS

#define DEFINE_VREGISTERS(N)                                                                                           \
  DEFINE_REGISTER(VRegister, b##N, N, 8, CPURegister::SIMD_FP_Register_8);                                                                \
  DEFINE_REGISTER(VRegister, h##N, N, 16, CPURegister::SIMD_FP_Register_16);                                                                \
  DEFINE_REGISTER(VRegister, s##N, N, 32, CPURegister::SIMD_FP_Register_32);                                                                \
  DEFINE_REGISTER(VRegister, d##N, N, 64, CPURegister::SIMD_FP_Register_64);                                                                \
  DEFINE_REGISTER(VRegister, q##N, N, 128, CPURegister::SIMD_FP_Register_128);                                                                \
GENERAL_REGISTER_CODE_LIST(DEFINE_VREGISTERS)
#undef DEFINE_VREGISTERS

#undef DEFINE_REGISTER
// clang-format on

} // namespace arm64
} // namespace zz

#endif
