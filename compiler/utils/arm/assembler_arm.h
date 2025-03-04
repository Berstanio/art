/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_COMPILER_UTILS_ARM_ASSEMBLER_ARM_H_
#define ART_COMPILER_UTILS_ARM_ASSEMBLER_ARM_H_

#include <type_traits>
#include <vector>

#include "base/bit_utils.h"
#include "base/logging.h"
#include "base/value_object.h"
#include "constants_arm.h"
#include "utils/arm/managed_register_arm.h"
#include "utils/assembler.h"
#include "offsets.h"

namespace art {
namespace arm {

class Arm32Assembler;
class Thumb2Assembler;

// Assembler literal is a value embedded in code, retrieved using a PC-relative load.
class Literal {
 public:
  static constexpr size_t kMaxSize = 8;

  Literal(uint32_t size, const uint8_t* data)
      : label_(), size_(size) {
    DCHECK_LE(size, Literal::kMaxSize);
    memcpy(data_, data, size);
  }

  template <typename T>
  T GetValue() const {
    DCHECK_EQ(size_, sizeof(T));
    T value;
    memcpy(&value, data_, sizeof(T));
    return value;
  }

  uint32_t GetSize() const {
    return size_;
  }

  const uint8_t* GetData() const {
    return data_;
  }

  Label* GetLabel() {
    return &label_;
  }

  const Label* GetLabel() const {
    return &label_;
  }

 private:
  Label label_;
  const uint32_t size_;
  uint8_t data_[kMaxSize];

  DISALLOW_COPY_AND_ASSIGN(Literal);
};

class ShifterOperand {
 public:
  ShifterOperand() : type_(kUnknown), rm_(kNoRegister), rs_(kNoRegister),
      is_rotate_(false), is_shift_(false), shift_(kNoShift), rotate_(0), immed_(0) {
  }

  explicit ShifterOperand(uint32_t immed);

  // Data-processing operands - Register
  explicit ShifterOperand(Register rm) : type_(kRegister), rm_(rm), rs_(kNoRegister),
      is_rotate_(false), is_shift_(false), shift_(kNoShift), rotate_(0), immed_(0) {
  }

  ShifterOperand(uint32_t rotate, uint32_t immed8) : type_(kImmediate), rm_(kNoRegister),
      rs_(kNoRegister),
      is_rotate_(true), is_shift_(false), shift_(kNoShift), rotate_(rotate), immed_(immed8) {
  }

  ShifterOperand(Register rm, Shift shift, uint32_t shift_imm = 0) : type_(kRegister), rm_(rm),
      rs_(kNoRegister),
      is_rotate_(false), is_shift_(true), shift_(shift), rotate_(0), immed_(shift_imm) {
  }

  // Data-processing operands - Logical shift/rotate by register
  ShifterOperand(Register rm, Shift shift, Register rs)  : type_(kRegister), rm_(rm),
      rs_(rs),
      is_rotate_(false), is_shift_(true), shift_(shift), rotate_(0), immed_(0) {
  }

  bool is_valid() const { return (type_ == kImmediate) || (type_ == kRegister); }

  uint32_t type() const {
    CHECK(is_valid());
    return type_;
  }

  uint32_t encodingArm() const;
  uint32_t encodingThumb() const;

  bool IsEmpty() const {
    return type_ == kUnknown;
  }

  bool IsImmediate() const {
    return type_ == kImmediate;
  }

  bool IsRegister() const {
    return type_ == kRegister;
  }

  bool IsShift() const {
    return is_shift_;
  }

  uint32_t GetImmediate() const {
    return immed_;
  }

  Shift GetShift() const {
    return shift_;
  }

  Register GetRegister() const {
    return rm_;
  }

  Register GetSecondRegister() const {
    return rs_;
  }

  enum Type {
    kUnknown = -1,
    kRegister,
    kImmediate
  };

 private:
  Type type_;
  Register rm_;
  Register rs_;
  bool is_rotate_;
  bool is_shift_;
  Shift shift_;
  uint32_t rotate_;
  uint32_t immed_;

  friend class Arm32Assembler;
  friend class Thumb2Assembler;

#ifdef SOURCE_ASSEMBLER_SUPPORT
  friend class BinaryAssembler;
#endif
};


enum LoadOperandType {
  kLoadSignedByte,
  kLoadUnsignedByte,
  kLoadSignedHalfword,
  kLoadUnsignedHalfword,
  kLoadWord,
  kLoadWordPair,
  kLoadSWord,
  kLoadDWord
};


enum StoreOperandType {
  kStoreByte,
  kStoreHalfword,
  kStoreWord,
  kStoreWordPair,
  kStoreSWord,
  kStoreDWord
};


// Load/store multiple addressing mode.
enum BlockAddressMode {
  // bit encoding P U W
  DA           = (0|0|0) << 21,  // decrement after
  IA           = (0|4|0) << 21,  // increment after
  DB           = (8|0|0) << 21,  // decrement before
  IB           = (8|4|0) << 21,  // increment before
  DA_W         = (0|0|1) << 21,  // decrement after with writeback to base
  IA_W         = (0|4|1) << 21,  // increment after with writeback to base
  DB_W         = (8|0|1) << 21,  // decrement before with writeback to base
  IB_W         = (8|4|1) << 21   // increment before with writeback to base
};
inline std::ostream& operator<<(std::ostream& os, const BlockAddressMode& rhs) {
  os << static_cast<int>(rhs);
  return os;
}

class Address : public ValueObject {
 public:
  // Memory operand addressing mode (in ARM encoding form.  For others we need
  // to adjust)
  enum Mode {
    // bit encoding P U W
    Offset       = (8|4|0) << 21,  // offset (w/o writeback to base)
    PreIndex     = (8|4|1) << 21,  // pre-indexed addressing with writeback
    PostIndex    = (0|4|0) << 21,  // post-indexed addressing with writeback
    NegOffset    = (8|0|0) << 21,  // negative offset (w/o writeback to base)
    NegPreIndex  = (8|0|1) << 21,  // negative pre-indexed with writeback
    NegPostIndex = (0|0|0) << 21   // negative post-indexed with writeback
  };

  Address(Register rn, int32_t offset = 0, Mode am = Offset) : rn_(rn), rm_(R0),
      offset_(offset),
      am_(am), is_immed_offset_(true), shift_(LSL) {
  }

  Address(Register rn, Register rm, Mode am = Offset) : rn_(rn), rm_(rm), offset_(0),
      am_(am), is_immed_offset_(false), shift_(LSL) {
    CHECK_NE(rm, PC);
  }

  Address(Register rn, Register rm, Shift shift, uint32_t count, Mode am = Offset) :
                       rn_(rn), rm_(rm), offset_(count),
                       am_(am), is_immed_offset_(false), shift_(shift) {
    CHECK_NE(rm, PC);
  }

  // LDR(literal) - pc relative load.
  explicit Address(int32_t offset) :
               rn_(PC), rm_(R0), offset_(offset),
               am_(Offset), is_immed_offset_(false), shift_(LSL) {
  }

  static bool CanHoldLoadOffsetArm(LoadOperandType type, int offset);
  static bool CanHoldStoreOffsetArm(StoreOperandType type, int offset);

  static bool CanHoldLoadOffsetThumb(LoadOperandType type, int offset);
  static bool CanHoldStoreOffsetThumb(StoreOperandType type, int offset);

  uint32_t encodingArm() const;
  uint32_t encodingThumb(bool is_32bit) const;

  uint32_t encoding3() const;
  uint32_t vencoding() const;

  uint32_t encodingThumbLdrdStrd() const;

  Register GetRegister() const {
    return rn_;
  }

  Register GetRegisterOffset() const {
    return rm_;
  }

  int32_t GetOffset() const {
    return offset_;
  }

  Mode GetMode() const {
    return am_;
  }

  bool IsImmediate() const {
    return is_immed_offset_;
  }

  Shift GetShift() const {
    return shift_;
  }

  int32_t GetShiftCount() const {
    CHECK(!is_immed_offset_);
    return offset_;
  }

 private:
  const Register rn_;
  const Register rm_;
  const int32_t offset_;      // Used as shift amount for register offset.
  const Mode am_;
  const bool is_immed_offset_;
  const Shift shift_;
};
inline std::ostream& operator<<(std::ostream& os, const Address::Mode& rhs) {
  os << static_cast<int>(rhs);
  return os;
}

// Instruction encoding bits.
enum {
  H   = 1 << 5,   // halfword (or byte)
  L   = 1 << 20,  // load (or store)
  S   = 1 << 20,  // set condition code (or leave unchanged)
  W   = 1 << 21,  // writeback base register (or leave unchanged)
  A   = 1 << 21,  // accumulate in multiply instruction (or not)
  B   = 1 << 22,  // unsigned byte (or word)
  N   = 1 << 22,  // long (or short)
  U   = 1 << 23,  // positive (or negative) offset/index
  P   = 1 << 24,  // offset/pre-indexed addressing (or post-indexed addressing)
  I   = 1 << 25,  // immediate shifter operand (or not)

  B0 = 1,
  B1 = 1 << 1,
  B2 = 1 << 2,
  B3 = 1 << 3,
  B4 = 1 << 4,
  B5 = 1 << 5,
  B6 = 1 << 6,
  B7 = 1 << 7,
  B8 = 1 << 8,
  B9 = 1 << 9,
  B10 = 1 << 10,
  B11 = 1 << 11,
  B12 = 1 << 12,
  B13 = 1 << 13,
  B14 = 1 << 14,
  B15 = 1 << 15,
  B16 = 1 << 16,
  B17 = 1 << 17,
  B18 = 1 << 18,
  B19 = 1 << 19,
  B20 = 1 << 20,
  B21 = 1 << 21,
  B22 = 1 << 22,
  B23 = 1 << 23,
  B24 = 1 << 24,
  B25 = 1 << 25,
  B26 = 1 << 26,
  B27 = 1 << 27,
  B28 = 1 << 28,
  B29 = 1 << 29,
  B30 = 1 << 30,
  B31 = 1 << 31,

  // Instruction bit masks.
  RdMask = 15 << 12,  // in str instruction
  CondMask = 15 << 28,
  CoprocessorMask = 15 << 8,
  OpCodeMask = 15 << 21,  // in data-processing instructions
  Imm24Mask = (1 << 24) - 1,
  Off12Mask = (1 << 12) - 1,

  // ldrex/strex register field encodings.
  kLdExRnShift = 16,
  kLdExRtShift = 12,
  kStrExRnShift = 16,
  kStrExRdShift = 12,
  kStrExRtShift = 0,
};

// IfThen state for IT instructions.
enum ItState {
  kItOmitted,
  kItThen,
  kItT = kItThen,
  kItElse,
  kItE = kItElse
};

// Set condition codes request.
enum SetCc {
  kCcDontCare,  // Allows prioritizing 16-bit instructions on Thumb2 whether they set CCs or not.
  kCcSet,
  kCcKeep,
};

constexpr uint32_t kNoItCondition = 3;
constexpr uint32_t kInvalidModifiedImmediate = -1;

extern const char* kRegisterNames[];
extern const char* kConditionNames[];

// This is an abstract ARM assembler.  Subclasses provide assemblers for the individual
// instruction sets (ARM32, Thumb2, etc.)
//
class ArmAssembler : public Assembler {
 public:
  virtual ~ArmAssembler() {}

  // Is this assembler for the thumb instruction set?
  virtual bool IsThumb() const = 0;

  // Data-processing instructions.
  virtual void and_(Register rd, Register rn, const ShifterOperand& so,
                    Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void ands(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    and_(rd, rn, so, cond, kCcSet);
  }

  virtual void eor(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void eors(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    eor(rd, rn, so, cond, kCcSet);
  }

  virtual void sub(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void subs(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    sub(rd, rn, so, cond, kCcSet);
  }

  virtual void rsb(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void rsbs(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    rsb(rd, rn, so, cond, kCcSet);
  }

  virtual void add(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void adds(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    add(rd, rn, so, cond, kCcSet);
  }

  virtual void adc(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void adcs(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    adc(rd, rn, so, cond, kCcSet);
  }

  virtual void sbc(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void sbcs(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    sbc(rd, rn, so, cond, kCcSet);
  }

  virtual void rsc(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void rscs(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    rsc(rd, rn, so, cond, kCcSet);
  }

  virtual void tst(Register rn, const ShifterOperand& so, Condition cond = AL) = 0;

  virtual void teq(Register rn, const ShifterOperand& so, Condition cond = AL) = 0;

  virtual void cmp(Register rn, const ShifterOperand& so, Condition cond = AL) = 0;

  virtual void cmn(Register rn, const ShifterOperand& so, Condition cond = AL) = 0;

  virtual void orr(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void orrs(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    orr(rd, rn, so, cond, kCcSet);
  }

  virtual void orn(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void orns(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    orn(rd, rn, so, cond, kCcSet);
  }

  virtual void mov(Register rd, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void movs(Register rd, const ShifterOperand& so, Condition cond = AL) {
    mov(rd, so, cond, kCcSet);
  }

  virtual void bic(Register rd, Register rn, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void bics(Register rd, Register rn, const ShifterOperand& so, Condition cond = AL) {
    bic(rd, rn, so, cond, kCcSet);
  }

  virtual void mvn(Register rd, const ShifterOperand& so,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  virtual void mvns(Register rd, const ShifterOperand& so, Condition cond = AL) {
    mvn(rd, so, cond, kCcSet);
  }

  // Miscellaneous data-processing instructions.
  virtual void clz(Register rd, Register rm, Condition cond = AL) = 0;
  virtual void movw(Register rd, uint16_t imm16, Condition cond = AL) = 0;
  virtual void movt(Register rd, uint16_t imm16, Condition cond = AL) = 0;
  virtual void rbit(Register rd, Register rm, Condition cond = AL) = 0;

  // Multiply instructions.
  virtual void mul(Register rd, Register rn, Register rm, Condition cond = AL) = 0;
  virtual void mla(Register rd, Register rn, Register rm, Register ra,
                   Condition cond = AL) = 0;
  virtual void mls(Register rd, Register rn, Register rm, Register ra,
                   Condition cond = AL) = 0;
  virtual void smull(Register rd_lo, Register rd_hi, Register rn, Register rm,
                     Condition cond = AL) = 0;
  virtual void umull(Register rd_lo, Register rd_hi, Register rn, Register rm,
                     Condition cond = AL) = 0;

  virtual void sdiv(Register rd, Register rn, Register rm, Condition cond = AL) = 0;
  virtual void udiv(Register rd, Register rn, Register rm, Condition cond = AL) = 0;

  // Bit field extract instructions.
  virtual void sbfx(Register rd, Register rn, uint32_t lsb, uint32_t width,
                    Condition cond = AL) = 0;
  virtual void ubfx(Register rd, Register rn, uint32_t lsb, uint32_t width,
                    Condition cond = AL) = 0;

  // Load/store instructions.
  virtual void ldr(Register rd, const Address& ad, Condition cond = AL) = 0;
  virtual void str(Register rd, const Address& ad, Condition cond = AL) = 0;

  virtual void ldrb(Register rd, const Address& ad, Condition cond = AL) = 0;
  virtual void strb(Register rd, const Address& ad, Condition cond = AL) = 0;

  virtual void ldrh(Register rd, const Address& ad, Condition cond = AL) = 0;
  virtual void strh(Register rd, const Address& ad, Condition cond = AL) = 0;

  virtual void ldrsb(Register rd, const Address& ad, Condition cond = AL) = 0;
  virtual void ldrsh(Register rd, const Address& ad, Condition cond = AL) = 0;

  virtual void ldrd(Register rd, const Address& ad, Condition cond = AL) = 0;
  virtual void strd(Register rd, const Address& ad, Condition cond = AL) = 0;

  virtual void ldm(BlockAddressMode am, Register base,
                   RegList regs, Condition cond = AL) = 0;
  virtual void stm(BlockAddressMode am, Register base,
                   RegList regs, Condition cond = AL) = 0;

  virtual void ldrex(Register rd, Register rn, Condition cond = AL) = 0;
  virtual void strex(Register rd, Register rt, Register rn, Condition cond = AL) = 0;
  virtual void ldrexd(Register rt, Register rt2, Register rn, Condition cond = AL) = 0;
  virtual void strexd(Register rd, Register rt, Register rt2, Register rn, Condition cond = AL) = 0;

  // Miscellaneous instructions.
  virtual void clrex(Condition cond = AL) = 0;
  virtual void nop(Condition cond = AL) = 0;

  // Note that gdb sets breakpoints using the undefined instruction 0xe7f001f0.
  virtual void bkpt(uint16_t imm16) = 0;
  virtual void svc(uint32_t imm24) = 0;

  virtual void it(Condition firstcond ATTRIBUTE_UNUSED,
                  ItState i1 ATTRIBUTE_UNUSED = kItOmitted,
                  ItState i2 ATTRIBUTE_UNUSED = kItOmitted,
                  ItState i3 ATTRIBUTE_UNUSED = kItOmitted) {
    // Ignored if not supported.
  }

  virtual void cbz(Register rn, Label* target) = 0;
  virtual void cbnz(Register rn, Label* target) = 0;

  // Floating point instructions (VFPv3-D16 and VFPv3-D32 profiles).
  virtual void vmovsr(SRegister sn, Register rt, Condition cond = AL) = 0;
  virtual void vmovrs(Register rt, SRegister sn, Condition cond = AL) = 0;
  virtual void vmovsrr(SRegister sm, Register rt, Register rt2, Condition cond = AL) = 0;
  virtual void vmovrrs(Register rt, Register rt2, SRegister sm, Condition cond = AL) = 0;
  virtual void vmovdrr(DRegister dm, Register rt, Register rt2, Condition cond = AL) = 0;
  virtual void vmovrrd(Register rt, Register rt2, DRegister dm, Condition cond = AL) = 0;
  virtual void vmovs(SRegister sd, SRegister sm, Condition cond = AL) = 0;
  virtual void vmovd(DRegister dd, DRegister dm, Condition cond = AL) = 0;

  // Returns false if the immediate cannot be encoded.
  virtual bool vmovs(SRegister sd, float s_imm, Condition cond = AL) = 0;
  virtual bool vmovd(DRegister dd, double d_imm, Condition cond = AL) = 0;

  virtual void vldrs(SRegister sd, const Address& ad, Condition cond = AL) = 0;
  virtual void vstrs(SRegister sd, const Address& ad, Condition cond = AL) = 0;
  virtual void vldrd(DRegister dd, const Address& ad, Condition cond = AL) = 0;
  virtual void vstrd(DRegister dd, const Address& ad, Condition cond = AL) = 0;

  virtual void vadds(SRegister sd, SRegister sn, SRegister sm, Condition cond = AL) = 0;
  virtual void vaddd(DRegister dd, DRegister dn, DRegister dm, Condition cond = AL) = 0;
  virtual void vsubs(SRegister sd, SRegister sn, SRegister sm, Condition cond = AL) = 0;
  virtual void vsubd(DRegister dd, DRegister dn, DRegister dm, Condition cond = AL) = 0;
  virtual void vmuls(SRegister sd, SRegister sn, SRegister sm, Condition cond = AL) = 0;
  virtual void vmuld(DRegister dd, DRegister dn, DRegister dm, Condition cond = AL) = 0;
  virtual void vmlas(SRegister sd, SRegister sn, SRegister sm, Condition cond = AL) = 0;
  virtual void vmlad(DRegister dd, DRegister dn, DRegister dm, Condition cond = AL) = 0;
  virtual void vmlss(SRegister sd, SRegister sn, SRegister sm, Condition cond = AL) = 0;
  virtual void vmlsd(DRegister dd, DRegister dn, DRegister dm, Condition cond = AL) = 0;
  virtual void vdivs(SRegister sd, SRegister sn, SRegister sm, Condition cond = AL) = 0;
  virtual void vdivd(DRegister dd, DRegister dn, DRegister dm, Condition cond = AL) = 0;

  virtual void vabss(SRegister sd, SRegister sm, Condition cond = AL) = 0;
  virtual void vabsd(DRegister dd, DRegister dm, Condition cond = AL) = 0;
  virtual void vnegs(SRegister sd, SRegister sm, Condition cond = AL) = 0;
  virtual void vnegd(DRegister dd, DRegister dm, Condition cond = AL) = 0;
  virtual void vsqrts(SRegister sd, SRegister sm, Condition cond = AL) = 0;
  virtual void vsqrtd(DRegister dd, DRegister dm, Condition cond = AL) = 0;

  virtual void vcvtsd(SRegister sd, DRegister dm, Condition cond = AL) = 0;
  virtual void vcvtds(DRegister dd, SRegister sm, Condition cond = AL) = 0;
  virtual void vcvtis(SRegister sd, SRegister sm, Condition cond = AL) = 0;
  virtual void vcvtid(SRegister sd, DRegister dm, Condition cond = AL) = 0;
  virtual void vcvtsi(SRegister sd, SRegister sm, Condition cond = AL) = 0;
  virtual void vcvtdi(DRegister dd, SRegister sm, Condition cond = AL) = 0;
  virtual void vcvtus(SRegister sd, SRegister sm, Condition cond = AL) = 0;
  virtual void vcvtud(SRegister sd, DRegister dm, Condition cond = AL) = 0;
  virtual void vcvtsu(SRegister sd, SRegister sm, Condition cond = AL) = 0;
  virtual void vcvtdu(DRegister dd, SRegister sm, Condition cond = AL) = 0;

  virtual void vcmps(SRegister sd, SRegister sm, Condition cond = AL) = 0;
  virtual void vcmpd(DRegister dd, DRegister dm, Condition cond = AL) = 0;
  virtual void vcmpsz(SRegister sd, Condition cond = AL) = 0;
  virtual void vcmpdz(DRegister dd, Condition cond = AL) = 0;
  virtual void vmstat(Condition cond = AL) = 0;  // VMRS APSR_nzcv, FPSCR

  virtual void vpushs(SRegister reg, int nregs, Condition cond = AL) = 0;
  virtual void vpushd(DRegister reg, int nregs, Condition cond = AL) = 0;
  virtual void vpops(SRegister reg, int nregs, Condition cond = AL) = 0;
  virtual void vpopd(DRegister reg, int nregs, Condition cond = AL) = 0;

  // Branch instructions.
  virtual void b(Label* label, Condition cond = AL) = 0;
  virtual void bl(Label* label, Condition cond = AL) = 0;
  virtual void blx(Register rm, Condition cond = AL) = 0;
  virtual void bx(Register rm, Condition cond = AL) = 0;

  // Memory barriers.
  virtual void dmb(DmbOptions flavor) = 0;

  void Pad(uint32_t bytes);

  // Adjust label position.
  void AdjustLabelPosition(Label* label) {
    DCHECK(label->IsBound());
    uint32_t old_position = static_cast<uint32_t>(label->Position());
    uint32_t new_position = GetAdjustedPosition(old_position);
    label->Reinitialize();
    DCHECK_GE(static_cast<int>(new_position), 0);
    label->BindTo(static_cast<int>(new_position));
  }

  // Get the final position of a label after local fixup based on the old position
  // recorded before FinalizeCode().
  virtual uint32_t GetAdjustedPosition(uint32_t old_position) = 0;

  // Macros.
  // Most of these are pure virtual as they need to be implemented per instruction set.

  // Create a new literal with a given value.
  // NOTE: Force the template parameter to be explicitly specified. In the absence of
  // std::omit_from_type_deduction<T> or std::identity<T>, use std::decay<T>.
  template <typename T>
  Literal* NewLiteral(typename std::decay<T>::type value) {
    static_assert(std::is_integral<T>::value, "T must be an integral type.");
    return NewLiteral(sizeof(value), reinterpret_cast<const uint8_t*>(&value));
  }

  // Create a new literal with the given data.
  virtual Literal* NewLiteral(size_t size, const uint8_t* data) = 0;

  // Load literal.
  virtual void LoadLiteral(Register rt, Literal* literal) = 0;
  virtual void LoadLiteral(Register rt, Register rt2, Literal* literal) = 0;
  virtual void LoadLiteral(SRegister sd, Literal* literal) = 0;
  virtual void LoadLiteral(DRegister dd, Literal* literal) = 0;

  // Add signed constant value to rd. May clobber IP.
  virtual void AddConstant(Register rd, Register rn, int32_t value,
                           Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;
  void AddConstantSetFlags(Register rd, Register rn, int32_t value, Condition cond = AL) {
    AddConstant(rd, rn, value, cond, kCcSet);
  }
  void AddConstant(Register rd, int32_t value, Condition cond = AL, SetCc set_cc = kCcDontCare) {
    AddConstant(rd, rd, value, cond, set_cc);
  }

  // Load and Store. May clobber IP.
  virtual void LoadImmediate(Register rd, int32_t value, Condition cond = AL) = 0;
  void LoadSImmediate(SRegister sd, float value, Condition cond = AL) {
    if (!vmovs(sd, value, cond)) {
      int32_t int_value = bit_cast<int32_t, float>(value);
      if (int_value == bit_cast<int32_t, float>(0.0f)) {
        // 0.0 is quite common, so we special case it by loading
        // 2.0 in `sd` and then substracting it.
        bool success = vmovs(sd, 2.0, cond);
        CHECK(success);
        vsubs(sd, sd, sd, cond);
      } else {
        LoadImmediate(IP, int_value, cond);
        vmovsr(sd, IP, cond);
      }
    }
  }

  void LoadDImmediate(DRegister sd, double value, Condition cond = AL) {
    if (!vmovd(sd, value, cond)) {
      uint64_t int_value = bit_cast<uint64_t, double>(value);
      if (int_value == bit_cast<uint64_t, double>(0.0)) {
        // 0.0 is quite common, so we special case it by loading
        // 2.0 in `sd` and then substracting it.
        bool success = vmovd(sd, 2.0, cond);
        CHECK(success);
        vsubd(sd, sd, sd, cond);
      } else {
        if (sd < 16) {
          SRegister low = static_cast<SRegister>(sd << 1);
          SRegister high = static_cast<SRegister>(low + 1);
          LoadSImmediate(low, bit_cast<float, uint32_t>(Low32Bits(int_value)), cond);
          if (High32Bits(int_value) == Low32Bits(int_value)) {
            vmovs(high, low);
          } else {
            LoadSImmediate(high, bit_cast<float, uint32_t>(High32Bits(int_value)), cond);
          }
        } else {
          LOG(FATAL) << "Unimplemented loading of double into a D register "
                     << "that cannot be split into two S registers";
        }
      }
    }
  }

  virtual void MarkExceptionHandler(Label* label) = 0;
  virtual void LoadFromOffset(LoadOperandType type,
                              Register reg,
                              Register base,
                              int32_t offset,
                              Condition cond = AL) = 0;
  virtual void StoreToOffset(StoreOperandType type,
                             Register reg,
                             Register base,
                             int32_t offset,
                             Condition cond = AL) = 0;
  virtual void LoadSFromOffset(SRegister reg,
                               Register base,
                               int32_t offset,
                               Condition cond = AL) = 0;
  virtual void StoreSToOffset(SRegister reg,
                              Register base,
                              int32_t offset,
                              Condition cond = AL) = 0;
  virtual void LoadDFromOffset(DRegister reg,
                               Register base,
                               int32_t offset,
                               Condition cond = AL) = 0;
  virtual void StoreDToOffset(DRegister reg,
                              Register base,
                              int32_t offset,
                              Condition cond = AL) = 0;

  virtual void Push(Register rd, Condition cond = AL) = 0;
  virtual void Pop(Register rd, Condition cond = AL) = 0;

  virtual void PushList(RegList regs, Condition cond = AL) = 0;
  virtual void PopList(RegList regs, Condition cond = AL) = 0;

  virtual void Mov(Register rd, Register rm, Condition cond = AL) = 0;

  // Convenience shift instructions. Use mov instruction with shifter operand
  // for variants setting the status flags or using a register shift count.
  virtual void Lsl(Register rd, Register rm, uint32_t shift_imm,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  void Lsls(Register rd, Register rm, uint32_t shift_imm, Condition cond = AL) {
    Lsl(rd, rm, shift_imm, cond, kCcSet);
  }

  virtual void Lsr(Register rd, Register rm, uint32_t shift_imm,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  void Lsrs(Register rd, Register rm, uint32_t shift_imm, Condition cond = AL) {
    Lsr(rd, rm, shift_imm, cond, kCcSet);
  }

  virtual void Asr(Register rd, Register rm, uint32_t shift_imm,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  void Asrs(Register rd, Register rm, uint32_t shift_imm, Condition cond = AL) {
    Asr(rd, rm, shift_imm, cond, kCcSet);
  }

  virtual void Ror(Register rd, Register rm, uint32_t shift_imm,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  void Rors(Register rd, Register rm, uint32_t shift_imm, Condition cond = AL) {
    Ror(rd, rm, shift_imm, cond, kCcSet);
  }

  virtual void Rrx(Register rd, Register rm,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  void Rrxs(Register rd, Register rm, Condition cond = AL) {
    Rrx(rd, rm, cond, kCcSet);
  }

  virtual void Lsl(Register rd, Register rm, Register rn,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  void Lsls(Register rd, Register rm, Register rn, Condition cond = AL) {
    Lsl(rd, rm, rn, cond, kCcSet);
  }

  virtual void Lsr(Register rd, Register rm, Register rn,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  void Lsrs(Register rd, Register rm, Register rn, Condition cond = AL) {
    Lsr(rd, rm, rn, cond, kCcSet);
  }

  virtual void Asr(Register rd, Register rm, Register rn,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  void Asrs(Register rd, Register rm, Register rn, Condition cond = AL) {
    Asr(rd, rm, rn, cond, kCcSet);
  }

  virtual void Ror(Register rd, Register rm, Register rn,
                   Condition cond = AL, SetCc set_cc = kCcDontCare) = 0;

  void Rors(Register rd, Register rm, Register rn, Condition cond = AL) {
    Ror(rd, rm, rn, cond, kCcSet);
  }

  // Returns whether the `immediate` can fit in a `ShifterOperand`. If yes,
  // `shifter_op` contains the operand.
  virtual bool ShifterOperandCanHold(Register rd,
                                     Register rn,
                                     Opcode opcode,
                                     uint32_t immediate,
                                     ShifterOperand* shifter_op) = 0;

  virtual bool ShifterOperandCanAlwaysHold(uint32_t immediate) = 0;

  static bool IsInstructionForExceptionHandling(uintptr_t pc);

  virtual void CompareAndBranchIfZero(Register r, Label* label) = 0;
  virtual void CompareAndBranchIfNonZero(Register r, Label* label) = 0;

  //
  // Overridden common assembler high-level functionality
  //

  // Emit code that will create an activation on the stack
  void BuildFrame(size_t frame_size, ManagedRegister method_reg,
                  const std::vector<ManagedRegister>& callee_save_regs,
                  const ManagedRegisterEntrySpills& entry_spills) OVERRIDE;

  // Emit code that will remove an activation from the stack
  void RemoveFrame(size_t frame_size, const std::vector<ManagedRegister>& callee_save_regs)
    OVERRIDE;

  void IncreaseFrameSize(size_t adjust) OVERRIDE;
  void DecreaseFrameSize(size_t adjust) OVERRIDE;

  // Store routines
  void Store(FrameOffset offs, ManagedRegister src, size_t size) OVERRIDE;
  void StoreRef(FrameOffset dest, ManagedRegister src) OVERRIDE;
  void StoreRawPtr(FrameOffset dest, ManagedRegister src) OVERRIDE;

  void StoreImmediateToFrame(FrameOffset dest, uint32_t imm, ManagedRegister scratch) OVERRIDE;

  void StoreImmediateToThread32(ThreadOffset<4> dest, uint32_t imm, ManagedRegister scratch)
      OVERRIDE;

  void StoreStackOffsetToThread32(ThreadOffset<4> thr_offs, FrameOffset fr_offs,
                                  ManagedRegister scratch) OVERRIDE;

  void StoreStackPointerToThread32(ThreadOffset<4> thr_offs) OVERRIDE;

  void StoreSpanning(FrameOffset dest, ManagedRegister src, FrameOffset in_off,
                     ManagedRegister scratch) OVERRIDE;

  // Load routines
  void Load(ManagedRegister dest, FrameOffset src, size_t size) OVERRIDE;

  void LoadFromThread32(ManagedRegister dest, ThreadOffset<4> src, size_t size) OVERRIDE;

  void LoadRef(ManagedRegister dest, FrameOffset src) OVERRIDE;

  void LoadRef(ManagedRegister dest, ManagedRegister base, MemberOffset offs,
               bool unpoison_reference) OVERRIDE;

  void LoadRawPtr(ManagedRegister dest, ManagedRegister base, Offset offs) OVERRIDE;

  void LoadRawPtrFromThread32(ManagedRegister dest, ThreadOffset<4> offs) OVERRIDE;

  // Copying routines
  void Move(ManagedRegister dest, ManagedRegister src, size_t size) OVERRIDE;

  void CopyRawPtrFromThread32(FrameOffset fr_offs, ThreadOffset<4> thr_offs,
                              ManagedRegister scratch) OVERRIDE;

  void CopyRawPtrToThread32(ThreadOffset<4> thr_offs, FrameOffset fr_offs, ManagedRegister scratch)
      OVERRIDE;

  void CopyRef(FrameOffset dest, FrameOffset src, ManagedRegister scratch) OVERRIDE;

  void Copy(FrameOffset dest, FrameOffset src, ManagedRegister scratch, size_t size) OVERRIDE;

  void Copy(FrameOffset dest, ManagedRegister src_base, Offset src_offset, ManagedRegister scratch,
            size_t size) OVERRIDE;

  void Copy(ManagedRegister dest_base, Offset dest_offset, FrameOffset src, ManagedRegister scratch,
            size_t size) OVERRIDE;

  void Copy(FrameOffset dest, FrameOffset src_base, Offset src_offset, ManagedRegister scratch,
            size_t size) OVERRIDE;

  void Copy(ManagedRegister dest, Offset dest_offset, ManagedRegister src, Offset src_offset,
            ManagedRegister scratch, size_t size) OVERRIDE;

  void Copy(FrameOffset dest, Offset dest_offset, FrameOffset src, Offset src_offset,
            ManagedRegister scratch, size_t size) OVERRIDE;

  // Sign extension
  void SignExtend(ManagedRegister mreg, size_t size) OVERRIDE;

  // Zero extension
  void ZeroExtend(ManagedRegister mreg, size_t size) OVERRIDE;

  // Exploit fast access in managed code to Thread::Current()
  void GetCurrentThread(ManagedRegister tr) OVERRIDE;
  void GetCurrentThread(FrameOffset dest_offset, ManagedRegister scratch) OVERRIDE;

  // Set up out_reg to hold a Object** into the handle scope, or to be null if the
  // value is null and null_allowed. in_reg holds a possibly stale reference
  // that can be used to avoid loading the handle scope entry to see if the value is
  // null.
  void CreateHandleScopeEntry(ManagedRegister out_reg, FrameOffset handlescope_offset,
                              ManagedRegister in_reg, bool null_allowed) OVERRIDE;

  // Set up out_off to hold a Object** into the handle scope, or to be null if the
  // value is null and null_allowed.
  void CreateHandleScopeEntry(FrameOffset out_off, FrameOffset handlescope_offset,
                              ManagedRegister scratch, bool null_allowed) OVERRIDE;

  // src holds a handle scope entry (Object**) load this into dst
  void LoadReferenceFromHandleScope(ManagedRegister dst, ManagedRegister src) OVERRIDE;

  // Heap::VerifyObject on src. In some cases (such as a reference to this) we
  // know that src may not be null.
  void VerifyObject(ManagedRegister src, bool could_be_null) OVERRIDE;
  void VerifyObject(FrameOffset src, bool could_be_null) OVERRIDE;

  // Call to address held at [base+offset]
  void Call(ManagedRegister base, Offset offset, ManagedRegister scratch) OVERRIDE;
  void Call(FrameOffset base, Offset offset, ManagedRegister scratch) OVERRIDE;
  void CallFromThread32(ThreadOffset<4> offset, ManagedRegister scratch) OVERRIDE;

  // Generate code to check if Thread::Current()->exception_ is non-null
  // and branch to a ExceptionSlowPath if it is.
  void ExceptionPoll(ManagedRegister scratch, size_t stack_adjust) OVERRIDE;

  static uint32_t ModifiedImmediate(uint32_t value);

  static bool IsLowRegister(Register r) {
    return r < R8;
  }

  static bool IsHighRegister(Register r) {
     return r >= R8;
  }

  //
  // Heap poisoning.
  //

  // Poison a heap reference contained in `reg`.
  void PoisonHeapReference(Register reg) {
    // reg = -reg.
    rsb(reg, reg, ShifterOperand(0));
  }
  // Unpoison a heap reference contained in `reg`.
  void UnpoisonHeapReference(Register reg) {
    // reg = -reg.
    rsb(reg, reg, ShifterOperand(0));
  }
  // Unpoison a heap reference contained in `reg` if heap poisoning is enabled.
  void MaybeUnpoisonHeapReference(Register reg) {
    if (kPoisonHeapReferences) {
      UnpoisonHeapReference(reg);
    }
  }

  void Jump(Label* label) OVERRIDE {
    b(label);
  }

 protected:
  // Returns whether or not the given register is used for passing parameters.
  static int RegisterCompare(const Register* reg1, const Register* reg2) {
    return *reg1 - *reg2;
  }
};

// Slowpath entered when Thread::Current()->_exception is non-null
class ArmExceptionSlowPath FINAL : public SlowPath {
 public:
  ArmExceptionSlowPath(ArmManagedRegister scratch, size_t stack_adjust)
      : scratch_(scratch), stack_adjust_(stack_adjust) {
  }
  void Emit(Assembler *sp_asm) OVERRIDE;
 private:
  const ArmManagedRegister scratch_;
  const size_t stack_adjust_;
};

}  // namespace arm
}  // namespace art

#endif  // ART_COMPILER_UTILS_ARM_ASSEMBLER_ARM_H_
