/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "assembler_arm32.h"

#include "base/bit_utils.h"
#include "base/logging.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "offsets.h"
#include "thread.h"

namespace art {
namespace arm {

bool Arm32Assembler::ShifterOperandCanHoldArm32(uint32_t immediate, ShifterOperand* shifter_op) {
  // Avoid the more expensive test for frequent small immediate values.
  if (immediate < (1 << kImmed8Bits)) {
    shifter_op->type_ = ShifterOperand::kImmediate;
    shifter_op->is_rotate_ = true;
    shifter_op->rotate_ = 0;
    shifter_op->immed_ = immediate;
    return true;
  }
  // Note that immediate must be unsigned for the test to work correctly.
  for (int rot = 0; rot < 16; rot++) {
    uint32_t imm8 = (immediate << 2*rot) | (immediate >> (32 - 2*rot));
    if (imm8 < (1 << kImmed8Bits)) {
      shifter_op->type_ = ShifterOperand::kImmediate;
      shifter_op->is_rotate_ = true;
      shifter_op->rotate_ = rot;
      shifter_op->immed_ = imm8;
      return true;
    }
  }
  return false;
}

bool Arm32Assembler::ShifterOperandCanAlwaysHold(uint32_t immediate) {
  ShifterOperand shifter_op;
  return ShifterOperandCanHoldArm32(immediate, &shifter_op);
}

bool Arm32Assembler::ShifterOperandCanHold(Register rd ATTRIBUTE_UNUSED,
                                           Register rn ATTRIBUTE_UNUSED,
                                           Opcode opcode ATTRIBUTE_UNUSED,
                                           uint32_t immediate,
                                           ShifterOperand* shifter_op) {
  return ShifterOperandCanHoldArm32(immediate, shifter_op);
}

void Arm32Assembler::and_(Register rd, Register rn, const ShifterOperand& so,
                          Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), AND, set_cc, rn, rd, so);
}


void Arm32Assembler::eor(Register rd, Register rn, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), EOR, set_cc, rn, rd, so);
}


void Arm32Assembler::sub(Register rd, Register rn, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), SUB, set_cc, rn, rd, so);
}

void Arm32Assembler::rsb(Register rd, Register rn, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), RSB, set_cc, rn, rd, so);
}

void Arm32Assembler::add(Register rd, Register rn, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), ADD, set_cc, rn, rd, so);
}


void Arm32Assembler::adc(Register rd, Register rn, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), ADC, set_cc, rn, rd, so);
}


void Arm32Assembler::sbc(Register rd, Register rn, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), SBC, set_cc, rn, rd, so);
}


void Arm32Assembler::rsc(Register rd, Register rn, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), RSC, set_cc, rn, rd, so);
}


void Arm32Assembler::tst(Register rn, const ShifterOperand& so, Condition cond) {
  CHECK_NE(rn, PC);  // Reserve tst pc instruction for exception handler marker.
  EmitType01(cond, so.type(), TST, kCcSet, rn, R0, so);
}


void Arm32Assembler::teq(Register rn, const ShifterOperand& so, Condition cond) {
  CHECK_NE(rn, PC);  // Reserve teq pc instruction for exception handler marker.
  EmitType01(cond, so.type(), TEQ, kCcSet, rn, R0, so);
}


void Arm32Assembler::cmp(Register rn, const ShifterOperand& so, Condition cond) {
  EmitType01(cond, so.type(), CMP, kCcSet, rn, R0, so);
}


void Arm32Assembler::cmn(Register rn, const ShifterOperand& so, Condition cond) {
  EmitType01(cond, so.type(), CMN, kCcSet, rn, R0, so);
}


void Arm32Assembler::orr(Register rd, Register rn, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), ORR, set_cc, rn, rd, so);
}


void Arm32Assembler::orn(Register rd ATTRIBUTE_UNUSED,
                         Register rn ATTRIBUTE_UNUSED,
                         const ShifterOperand& so ATTRIBUTE_UNUSED,
                         Condition cond ATTRIBUTE_UNUSED,
                         SetCc set_cc ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "orn is not supported on ARM32";
}


void Arm32Assembler::mov(Register rd, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), MOV, set_cc, R0, rd, so);
}


void Arm32Assembler::bic(Register rd, Register rn, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), BIC, set_cc, rn, rd, so);
}


void Arm32Assembler::mvn(Register rd, const ShifterOperand& so,
                         Condition cond, SetCc set_cc) {
  EmitType01(cond, so.type(), MVN, set_cc, R0, rd, so);
}


void Arm32Assembler::mul(Register rd, Register rn, Register rm, Condition cond) {
  // Assembler registers rd, rn, rm are encoded as rn, rm, rs.
  EmitMulOp(cond, 0, R0, rd, rn, rm);
}


void Arm32Assembler::mla(Register rd, Register rn, Register rm, Register ra,
                         Condition cond) {
  // Assembler registers rd, rn, rm, ra are encoded as rn, rm, rs, rd.
  EmitMulOp(cond, B21, ra, rd, rn, rm);
}


void Arm32Assembler::mls(Register rd, Register rn, Register rm, Register ra,
                         Condition cond) {
  // Assembler registers rd, rn, rm, ra are encoded as rn, rm, rs, rd.
  EmitMulOp(cond, B22 | B21, ra, rd, rn, rm);
}


void Arm32Assembler::smull(Register rd_lo, Register rd_hi, Register rn,
                           Register rm, Condition cond) {
  // Assembler registers rd_lo, rd_hi, rn, rm are encoded as rd, rn, rm, rs.
  EmitMulOp(cond, B23 | B22, rd_lo, rd_hi, rn, rm);
}


void Arm32Assembler::umull(Register rd_lo, Register rd_hi, Register rn,
                           Register rm, Condition cond) {
  // Assembler registers rd_lo, rd_hi, rn, rm are encoded as rd, rn, rm, rs.
  EmitMulOp(cond, B23, rd_lo, rd_hi, rn, rm);
}


void Arm32Assembler::sdiv(Register rd, Register rn, Register rm, Condition cond) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(rn, kNoRegister);
  CHECK_NE(rm, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = B26 | B25 | B24 | B20 |
      B15 | B14 | B13 | B12 |
      (static_cast<int32_t>(cond) << kConditionShift) |
      (static_cast<int32_t>(rn) << 0) |
      (static_cast<int32_t>(rd) << 16) |
      (static_cast<int32_t>(rm) << 8) |
      B4;
  Emit(encoding);
}


void Arm32Assembler::udiv(Register rd, Register rn, Register rm, Condition cond) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(rn, kNoRegister);
  CHECK_NE(rm, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = B26 | B25 | B24 | B21 | B20 |
      B15 | B14 | B13 | B12 |
      (static_cast<int32_t>(cond) << kConditionShift) |
      (static_cast<int32_t>(rn) << 0) |
      (static_cast<int32_t>(rd) << 16) |
      (static_cast<int32_t>(rm) << 8) |
      B4;
  Emit(encoding);
}


void Arm32Assembler::sbfx(Register rd, Register rn, uint32_t lsb, uint32_t width, Condition cond) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(rn, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  CHECK_LE(lsb, 31U);
  CHECK(1U <= width && width <= 32U) << width;
  uint32_t widthminus1 = width - 1;

  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
      B26 | B25 | B24 | B23 | B21 |
      (widthminus1 << 16) |
      (static_cast<uint32_t>(rd) << 12) |
      (lsb << 7) |
      B6 | B4 |
      static_cast<uint32_t>(rn);
  Emit(encoding);
}


void Arm32Assembler::ubfx(Register rd, Register rn, uint32_t lsb, uint32_t width, Condition cond) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(rn, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  CHECK_LE(lsb, 31U);
  CHECK(1U <= width && width <= 32U) << width;
  uint32_t widthminus1 = width - 1;

  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
      B26 | B25 | B24 | B23 | B22 | B21 |
      (widthminus1 << 16) |
      (static_cast<uint32_t>(rd) << 12) |
      (lsb << 7) |
      B6 | B4 |
      static_cast<uint32_t>(rn);
  Emit(encoding);
}


void Arm32Assembler::ldr(Register rd, const Address& ad, Condition cond) {
  EmitMemOp(cond, true, false, rd, ad);
}


void Arm32Assembler::str(Register rd, const Address& ad, Condition cond) {
  EmitMemOp(cond, false, false, rd, ad);
}


void Arm32Assembler::ldrb(Register rd, const Address& ad, Condition cond) {
  EmitMemOp(cond, true, true, rd, ad);
}


void Arm32Assembler::strb(Register rd, const Address& ad, Condition cond) {
  EmitMemOp(cond, false, true, rd, ad);
}


void Arm32Assembler::ldrh(Register rd, const Address& ad, Condition cond) {
  EmitMemOpAddressMode3(cond, L | B7 | H | B4, rd, ad);
}


void Arm32Assembler::strh(Register rd, const Address& ad, Condition cond) {
  EmitMemOpAddressMode3(cond, B7 | H | B4, rd, ad);
}


void Arm32Assembler::ldrsb(Register rd, const Address& ad, Condition cond) {
  EmitMemOpAddressMode3(cond, L | B7 | B6 | B4, rd, ad);
}


void Arm32Assembler::ldrsh(Register rd, const Address& ad, Condition cond) {
  EmitMemOpAddressMode3(cond, L | B7 | B6 | H | B4, rd, ad);
}


void Arm32Assembler::ldrd(Register rd, const Address& ad, Condition cond) {
  CHECK_EQ(rd % 2, 0);
  EmitMemOpAddressMode3(cond, B7 | B6 | B4, rd, ad);
}


void Arm32Assembler::strd(Register rd, const Address& ad, Condition cond) {
  CHECK_EQ(rd % 2, 0);
  EmitMemOpAddressMode3(cond, B7 | B6 | B5 | B4, rd, ad);
}


void Arm32Assembler::ldm(BlockAddressMode am,
                       Register base,
                       RegList regs,
                       Condition cond) {
  EmitMultiMemOp(cond, am, true, base, regs);
}


void Arm32Assembler::stm(BlockAddressMode am,
                       Register base,
                       RegList regs,
                       Condition cond) {
  EmitMultiMemOp(cond, am, false, base, regs);
}


void Arm32Assembler::vmovs(SRegister sd, SRegister sm, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B6, sd, S0, sm);
}


void Arm32Assembler::vmovd(DRegister dd, DRegister dm, Condition cond) {
  EmitVFPddd(cond, B23 | B21 | B20 | B6, dd, D0, dm);
}


bool Arm32Assembler::vmovs(SRegister sd, float s_imm, Condition cond) {
  uint32_t imm32 = bit_cast<uint32_t, float>(s_imm);
  if (((imm32 & ((1 << 19) - 1)) == 0) &&
      ((((imm32 >> 25) & ((1 << 6) - 1)) == (1 << 5)) ||
       (((imm32 >> 25) & ((1 << 6) - 1)) == ((1 << 5) -1)))) {
    uint8_t imm8 = ((imm32 >> 31) << 7) | (((imm32 >> 29) & 1) << 6) |
        ((imm32 >> 19) & ((1 << 6) -1));
    EmitVFPsss(cond, B23 | B21 | B20 | ((imm8 >> 4)*B16) | (imm8 & 0xf),
               sd, S0, S0);
    return true;
  }
  return false;
}


bool Arm32Assembler::vmovd(DRegister dd, double d_imm, Condition cond) {
  uint64_t imm64 = bit_cast<uint64_t, double>(d_imm);
  if (((imm64 & ((1LL << 48) - 1)) == 0) &&
      ((((imm64 >> 54) & ((1 << 9) - 1)) == (1 << 8)) ||
       (((imm64 >> 54) & ((1 << 9) - 1)) == ((1 << 8) -1)))) {
    uint8_t imm8 = ((imm64 >> 63) << 7) | (((imm64 >> 61) & 1) << 6) |
        ((imm64 >> 48) & ((1 << 6) -1));
    EmitVFPddd(cond, B23 | B21 | B20 | ((imm8 >> 4)*B16) | B8 | (imm8 & 0xf),
               dd, D0, D0);
    return true;
  }
  return false;
}


void Arm32Assembler::vadds(SRegister sd, SRegister sn, SRegister sm,
                           Condition cond) {
  EmitVFPsss(cond, B21 | B20, sd, sn, sm);
}


void Arm32Assembler::vaddd(DRegister dd, DRegister dn, DRegister dm,
                           Condition cond) {
  EmitVFPddd(cond, B21 | B20, dd, dn, dm);
}


void Arm32Assembler::vsubs(SRegister sd, SRegister sn, SRegister sm,
                           Condition cond) {
  EmitVFPsss(cond, B21 | B20 | B6, sd, sn, sm);
}


void Arm32Assembler::vsubd(DRegister dd, DRegister dn, DRegister dm,
                           Condition cond) {
  EmitVFPddd(cond, B21 | B20 | B6, dd, dn, dm);
}


void Arm32Assembler::vmuls(SRegister sd, SRegister sn, SRegister sm,
                           Condition cond) {
  EmitVFPsss(cond, B21, sd, sn, sm);
}


void Arm32Assembler::vmuld(DRegister dd, DRegister dn, DRegister dm,
                           Condition cond) {
  EmitVFPddd(cond, B21, dd, dn, dm);
}


void Arm32Assembler::vmlas(SRegister sd, SRegister sn, SRegister sm,
                           Condition cond) {
  EmitVFPsss(cond, 0, sd, sn, sm);
}


void Arm32Assembler::vmlad(DRegister dd, DRegister dn, DRegister dm,
                           Condition cond) {
  EmitVFPddd(cond, 0, dd, dn, dm);
}


void Arm32Assembler::vmlss(SRegister sd, SRegister sn, SRegister sm,
                           Condition cond) {
  EmitVFPsss(cond, B6, sd, sn, sm);
}


void Arm32Assembler::vmlsd(DRegister dd, DRegister dn, DRegister dm,
                           Condition cond) {
  EmitVFPddd(cond, B6, dd, dn, dm);
}


void Arm32Assembler::vdivs(SRegister sd, SRegister sn, SRegister sm,
                           Condition cond) {
  EmitVFPsss(cond, B23, sd, sn, sm);
}


void Arm32Assembler::vdivd(DRegister dd, DRegister dn, DRegister dm,
                           Condition cond) {
  EmitVFPddd(cond, B23, dd, dn, dm);
}


void Arm32Assembler::vabss(SRegister sd, SRegister sm, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B7 | B6, sd, S0, sm);
}


void Arm32Assembler::vabsd(DRegister dd, DRegister dm, Condition cond) {
  EmitVFPddd(cond, B23 | B21 | B20 | B7 | B6, dd, D0, dm);
}


void Arm32Assembler::vnegs(SRegister sd, SRegister sm, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B16 | B6, sd, S0, sm);
}


void Arm32Assembler::vnegd(DRegister dd, DRegister dm, Condition cond) {
  EmitVFPddd(cond, B23 | B21 | B20 | B16 | B6, dd, D0, dm);
}


void Arm32Assembler::vsqrts(SRegister sd, SRegister sm, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B16 | B7 | B6, sd, S0, sm);
}

void Arm32Assembler::vsqrtd(DRegister dd, DRegister dm, Condition cond) {
  EmitVFPddd(cond, B23 | B21 | B20 | B16 | B7 | B6, dd, D0, dm);
}


void Arm32Assembler::vcvtsd(SRegister sd, DRegister dm, Condition cond) {
  EmitVFPsd(cond, B23 | B21 | B20 | B18 | B17 | B16 | B8 | B7 | B6, sd, dm);
}


void Arm32Assembler::vcvtds(DRegister dd, SRegister sm, Condition cond) {
  EmitVFPds(cond, B23 | B21 | B20 | B18 | B17 | B16 | B7 | B6, dd, sm);
}


void Arm32Assembler::vcvtis(SRegister sd, SRegister sm, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B19 | B18 | B16 | B7 | B6, sd, S0, sm);
}


void Arm32Assembler::vcvtid(SRegister sd, DRegister dm, Condition cond) {
  EmitVFPsd(cond, B23 | B21 | B20 | B19 | B18 | B16 | B8 | B7 | B6, sd, dm);
}


void Arm32Assembler::vcvtsi(SRegister sd, SRegister sm, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B19 | B7 | B6, sd, S0, sm);
}


void Arm32Assembler::vcvtdi(DRegister dd, SRegister sm, Condition cond) {
  EmitVFPds(cond, B23 | B21 | B20 | B19 | B8 | B7 | B6, dd, sm);
}


void Arm32Assembler::vcvtus(SRegister sd, SRegister sm, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B19 | B18 | B7 | B6, sd, S0, sm);
}


void Arm32Assembler::vcvtud(SRegister sd, DRegister dm, Condition cond) {
  EmitVFPsd(cond, B23 | B21 | B20 | B19 | B18 | B8 | B7 | B6, sd, dm);
}


void Arm32Assembler::vcvtsu(SRegister sd, SRegister sm, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B19 | B6, sd, S0, sm);
}


void Arm32Assembler::vcvtdu(DRegister dd, SRegister sm, Condition cond) {
  EmitVFPds(cond, B23 | B21 | B20 | B19 | B8 | B6, dd, sm);
}


void Arm32Assembler::vcmps(SRegister sd, SRegister sm, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B18 | B6, sd, S0, sm);
}


void Arm32Assembler::vcmpd(DRegister dd, DRegister dm, Condition cond) {
  EmitVFPddd(cond, B23 | B21 | B20 | B18 | B6, dd, D0, dm);
}


void Arm32Assembler::vcmpsz(SRegister sd, Condition cond) {
  EmitVFPsss(cond, B23 | B21 | B20 | B18 | B16 | B6, sd, S0, S0);
}


void Arm32Assembler::vcmpdz(DRegister dd, Condition cond) {
  EmitVFPddd(cond, B23 | B21 | B20 | B18 | B16 | B6, dd, D0, D0);
}

void Arm32Assembler::b(Label* label, Condition cond) {
  EmitBranch(cond, label, false);
}


void Arm32Assembler::bl(Label* label, Condition cond) {
  EmitBranch(cond, label, true);
}


void Arm32Assembler::MarkExceptionHandler(Label* label) {
  EmitType01(AL, 1, TST, kCcSet, PC, R0, ShifterOperand(0));
  Label l;
  b(&l);
  EmitBranch(AL, label, false);
  Bind(&l);
}


void Arm32Assembler::Emit(int32_t value) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  buffer_.Emit<int32_t>(value);
}


void Arm32Assembler::EmitType01(Condition cond,
                                int type,
                                Opcode opcode,
                                SetCc set_cc,
                                Register rn,
                                Register rd,
                                const ShifterOperand& so) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = static_cast<int32_t>(cond) << kConditionShift |
                     type << kTypeShift |
                     static_cast<int32_t>(opcode) << kOpcodeShift |
                     (set_cc == kCcSet ? 1 : 0) << kSShift |
                     static_cast<int32_t>(rn) << kRnShift |
                     static_cast<int32_t>(rd) << kRdShift |
                     so.encodingArm();
  Emit(encoding);
}


void Arm32Assembler::EmitType5(Condition cond, int offset, bool link) {
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = static_cast<int32_t>(cond) << kConditionShift |
                     5 << kTypeShift |
                     (link ? 1 : 0) << kLinkShift;
  Emit(Arm32Assembler::EncodeBranchOffset(offset, encoding));
}


void Arm32Assembler::EmitMemOp(Condition cond,
                               bool load,
                               bool byte,
                               Register rd,
                               const Address& ad) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  const Address& addr = static_cast<const Address&>(ad);

  int32_t encoding = 0;
  if (!ad.IsImmediate() && ad.GetRegisterOffset() == PC) {
    // PC relative LDR(literal)
    int32_t offset = ad.GetOffset();
    int32_t u = B23;
    if (offset < 0) {
      offset = -offset;
      u = 0;
    }
    CHECK_LT(offset, (1 << 12));
    encoding = (static_cast<int32_t>(cond) << kConditionShift) |
         B26 | B24 | u | B20 |
         (load ? L : 0) |
         (byte ? B : 0) |
         (static_cast<int32_t>(rd) << kRdShift) |
         0xf << 16 |
         (offset & 0xfff);

  } else {
    encoding = (static_cast<int32_t>(cond) << kConditionShift) |
        B26 |
        (load ? L : 0) |
        (byte ? B : 0) |
        (static_cast<int32_t>(rd) << kRdShift) |
        addr.encodingArm();
  }
  Emit(encoding);
}


void Arm32Assembler::EmitMemOpAddressMode3(Condition cond,
                                           int32_t mode,
                                           Register rd,
                                           const Address& ad) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  const Address& addr = static_cast<const Address&>(ad);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B22  |
                     mode |
                     (static_cast<int32_t>(rd) << kRdShift) |
                     addr.encoding3();
  Emit(encoding);
}


void Arm32Assembler::EmitMultiMemOp(Condition cond,
                                    BlockAddressMode am,
                                    bool load,
                                    Register base,
                                    RegList regs) {
  CHECK_NE(base, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 |
                     am |
                     (load ? L : 0) |
                     (static_cast<int32_t>(base) << kRnShift) |
                     regs;
  Emit(encoding);
}


void Arm32Assembler::EmitShiftImmediate(Condition cond,
                                        Shift opcode,
                                        Register rd,
                                        Register rm,
                                        const ShifterOperand& so) {
  CHECK_NE(cond, kNoCondition);
  CHECK(so.IsImmediate());
  int32_t encoding = static_cast<int32_t>(cond) << kConditionShift |
                     static_cast<int32_t>(MOV) << kOpcodeShift |
                     static_cast<int32_t>(rd) << kRdShift |
                     so.encodingArm() << kShiftImmShift |
                     static_cast<int32_t>(opcode) << kShiftShift |
                     static_cast<int32_t>(rm);
  Emit(encoding);
}


void Arm32Assembler::EmitShiftRegister(Condition cond,
                                       Shift opcode,
                                       Register rd,
                                       Register rm,
                                       const ShifterOperand& so) {
  CHECK_NE(cond, kNoCondition);
  CHECK(so.IsRegister());
  int32_t encoding = static_cast<int32_t>(cond) << kConditionShift |
                     static_cast<int32_t>(MOV) << kOpcodeShift |
                     static_cast<int32_t>(rd) << kRdShift |
                     so.encodingArm() << kShiftRegisterShift |
                     static_cast<int32_t>(opcode) << kShiftShift |
                     B4 |
                     static_cast<int32_t>(rm);
  Emit(encoding);
}


void Arm32Assembler::EmitBranch(Condition cond, Label* label, bool link) {
  if (label->IsBound()) {
    EmitType5(cond, label->Position() - buffer_.Size(), link);
  } else {
    int position = buffer_.Size();
    // Use the offset field of the branch instruction for linking the sites.
    EmitType5(cond, label->position_, link);
    label->LinkTo(position);
  }
}


void Arm32Assembler::clz(Register rd, Register rm, Condition cond) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(rm, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  CHECK_NE(rd, PC);
  CHECK_NE(rm, PC);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B24 | B22 | B21 | (0xf << 16) |
                     (static_cast<int32_t>(rd) << kRdShift) |
                     (0xf << 8) | B4 | static_cast<int32_t>(rm);
  Emit(encoding);
}


void Arm32Assembler::movw(Register rd, uint16_t imm16, Condition cond) {
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = static_cast<int32_t>(cond) << kConditionShift |
                     B25 | B24 | ((imm16 >> 12) << 16) |
                     static_cast<int32_t>(rd) << kRdShift | (imm16 & 0xfff);
  Emit(encoding);
}


void Arm32Assembler::movt(Register rd, uint16_t imm16, Condition cond) {
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = static_cast<int32_t>(cond) << kConditionShift |
                     B25 | B24 | B22 | ((imm16 >> 12) << 16) |
                     static_cast<int32_t>(rd) << kRdShift | (imm16 & 0xfff);
  Emit(encoding);
}


void Arm32Assembler::rbit(Register rd, Register rm, Condition cond) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(rm, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  CHECK_NE(rd, PC);
  CHECK_NE(rm, PC);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B26 | B25 | B23 | B22 | B21 | B20 | (0xf << 16) |
                     (static_cast<int32_t>(rd) << kRdShift) |
                     (0xf << 8) | B5 | B4 | static_cast<int32_t>(rm);
  Emit(encoding);
}


void Arm32Assembler::EmitMulOp(Condition cond, int32_t opcode,
                               Register rd, Register rn,
                               Register rm, Register rs) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(rn, kNoRegister);
  CHECK_NE(rm, kNoRegister);
  CHECK_NE(rs, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = opcode |
      (static_cast<int32_t>(cond) << kConditionShift) |
      (static_cast<int32_t>(rn) << kRnShift) |
      (static_cast<int32_t>(rd) << kRdShift) |
      (static_cast<int32_t>(rs) << kRsShift) |
      B7 | B4 |
      (static_cast<int32_t>(rm) << kRmShift);
  Emit(encoding);
}


void Arm32Assembler::ldrex(Register rt, Register rn, Condition cond) {
  CHECK_NE(rn, kNoRegister);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B24 |
                     B23 |
                     L   |
                     (static_cast<int32_t>(rn) << kLdExRnShift) |
                     (static_cast<int32_t>(rt) << kLdExRtShift) |
                     B11 | B10 | B9 | B8 | B7 | B4 | B3 | B2 | B1 | B0;
  Emit(encoding);
}


void Arm32Assembler::ldrexd(Register rt, Register rt2, Register rn, Condition cond) {
  CHECK_NE(rn, kNoRegister);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(rt2, kNoRegister);
  CHECK_NE(rt, R14);
  CHECK_EQ(0u, static_cast<uint32_t>(rt) % 2);
  CHECK_EQ(static_cast<uint32_t>(rt) + 1, static_cast<uint32_t>(rt2));
  CHECK_NE(cond, kNoCondition);

  int32_t encoding =
      (static_cast<uint32_t>(cond) << kConditionShift) |
      B24 | B23 | B21 | B20 |
      static_cast<uint32_t>(rn) << 16 |
      static_cast<uint32_t>(rt) << 12 |
      B11 | B10 | B9 | B8 | B7 | B4 | B3 | B2 | B1 | B0;
  Emit(encoding);
}


void Arm32Assembler::strex(Register rd,
                           Register rt,
                           Register rn,
                           Condition cond) {
  CHECK_NE(rn, kNoRegister);
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B24 |
                     B23 |
                     (static_cast<int32_t>(rn) << kStrExRnShift) |
                     (static_cast<int32_t>(rd) << kStrExRdShift) |
                     B11 | B10 | B9 | B8 | B7 | B4 |
                     (static_cast<int32_t>(rt) << kStrExRtShift);
  Emit(encoding);
}

void Arm32Assembler::strexd(Register rd, Register rt, Register rt2, Register rn, Condition cond) {
  CHECK_NE(rd, kNoRegister);
  CHECK_NE(rn, kNoRegister);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(rt2, kNoRegister);
  CHECK_NE(rt, R14);
  CHECK_NE(rd, rt);
  CHECK_NE(rd, rt2);
  CHECK_EQ(0u, static_cast<uint32_t>(rt) % 2);
  CHECK_EQ(static_cast<uint32_t>(rt) + 1, static_cast<uint32_t>(rt2));
  CHECK_NE(cond, kNoCondition);

  int32_t encoding =
      (static_cast<uint32_t>(cond) << kConditionShift) |
      B24 | B23 | B21 |
      static_cast<uint32_t>(rn) << 16 |
      static_cast<uint32_t>(rd) << 12 |
      B11 | B10 | B9 | B8 | B7 | B4 |
      static_cast<uint32_t>(rt);
  Emit(encoding);
}


void Arm32Assembler::clrex(Condition cond) {
  CHECK_EQ(cond, AL);   // This cannot be conditional on ARM.
  int32_t encoding = (kSpecialCondition << kConditionShift) |
                     B26 | B24 | B22 | B21 | B20 | (0xff << 12) | B4 | 0xf;
  Emit(encoding);
}


void Arm32Assembler::nop(Condition cond) {
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B25 | B24 | B21 | (0xf << 12);
  Emit(encoding);
}


void Arm32Assembler::vmovsr(SRegister sn, Register rt, Condition cond) {
  CHECK_NE(sn, kNoSRegister);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(rt, SP);
  CHECK_NE(rt, PC);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B25 |
                     ((static_cast<int32_t>(sn) >> 1)*B16) |
                     (static_cast<int32_t>(rt)*B12) | B11 | B9 |
                     ((static_cast<int32_t>(sn) & 1)*B7) | B4;
  Emit(encoding);
}


void Arm32Assembler::vmovrs(Register rt, SRegister sn, Condition cond) {
  CHECK_NE(sn, kNoSRegister);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(rt, SP);
  CHECK_NE(rt, PC);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B25 | B20 |
                     ((static_cast<int32_t>(sn) >> 1)*B16) |
                     (static_cast<int32_t>(rt)*B12) | B11 | B9 |
                     ((static_cast<int32_t>(sn) & 1)*B7) | B4;
  Emit(encoding);
}


void Arm32Assembler::vmovsrr(SRegister sm, Register rt, Register rt2,
                             Condition cond) {
  CHECK_NE(sm, kNoSRegister);
  CHECK_NE(sm, S31);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(rt, SP);
  CHECK_NE(rt, PC);
  CHECK_NE(rt2, kNoRegister);
  CHECK_NE(rt2, SP);
  CHECK_NE(rt2, PC);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B22 |
                     (static_cast<int32_t>(rt2)*B16) |
                     (static_cast<int32_t>(rt)*B12) | B11 | B9 |
                     ((static_cast<int32_t>(sm) & 1)*B5) | B4 |
                     (static_cast<int32_t>(sm) >> 1);
  Emit(encoding);
}


void Arm32Assembler::vmovrrs(Register rt, Register rt2, SRegister sm,
                             Condition cond) {
  CHECK_NE(sm, kNoSRegister);
  CHECK_NE(sm, S31);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(rt, SP);
  CHECK_NE(rt, PC);
  CHECK_NE(rt2, kNoRegister);
  CHECK_NE(rt2, SP);
  CHECK_NE(rt2, PC);
  CHECK_NE(rt, rt2);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B22 | B20 |
                     (static_cast<int32_t>(rt2)*B16) |
                     (static_cast<int32_t>(rt)*B12) | B11 | B9 |
                     ((static_cast<int32_t>(sm) & 1)*B5) | B4 |
                     (static_cast<int32_t>(sm) >> 1);
  Emit(encoding);
}


void Arm32Assembler::vmovdrr(DRegister dm, Register rt, Register rt2,
                             Condition cond) {
  CHECK_NE(dm, kNoDRegister);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(rt, SP);
  CHECK_NE(rt, PC);
  CHECK_NE(rt2, kNoRegister);
  CHECK_NE(rt2, SP);
  CHECK_NE(rt2, PC);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B22 |
                     (static_cast<int32_t>(rt2)*B16) |
                     (static_cast<int32_t>(rt)*B12) | B11 | B9 | B8 |
                     ((static_cast<int32_t>(dm) >> 4)*B5) | B4 |
                     (static_cast<int32_t>(dm) & 0xf);
  Emit(encoding);
}


void Arm32Assembler::vmovrrd(Register rt, Register rt2, DRegister dm,
                             Condition cond) {
  CHECK_NE(dm, kNoDRegister);
  CHECK_NE(rt, kNoRegister);
  CHECK_NE(rt, SP);
  CHECK_NE(rt, PC);
  CHECK_NE(rt2, kNoRegister);
  CHECK_NE(rt2, SP);
  CHECK_NE(rt2, PC);
  CHECK_NE(rt, rt2);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B22 | B20 |
                     (static_cast<int32_t>(rt2)*B16) |
                     (static_cast<int32_t>(rt)*B12) | B11 | B9 | B8 |
                     ((static_cast<int32_t>(dm) >> 4)*B5) | B4 |
                     (static_cast<int32_t>(dm) & 0xf);
  Emit(encoding);
}


void Arm32Assembler::vldrs(SRegister sd, const Address& ad, Condition cond) {
  const Address& addr = static_cast<const Address&>(ad);
  CHECK_NE(sd, kNoSRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B24 | B20 |
                     ((static_cast<int32_t>(sd) & 1)*B22) |
                     ((static_cast<int32_t>(sd) >> 1)*B12) |
                     B11 | B9 | addr.vencoding();
  Emit(encoding);
}


void Arm32Assembler::vstrs(SRegister sd, const Address& ad, Condition cond) {
  const Address& addr = static_cast<const Address&>(ad);
  CHECK_NE(static_cast<Register>(addr.encodingArm() & (0xf << kRnShift)), PC);
  CHECK_NE(sd, kNoSRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B24 |
                     ((static_cast<int32_t>(sd) & 1)*B22) |
                     ((static_cast<int32_t>(sd) >> 1)*B12) |
                     B11 | B9 | addr.vencoding();
  Emit(encoding);
}


void Arm32Assembler::vldrd(DRegister dd, const Address& ad, Condition cond) {
  const Address& addr = static_cast<const Address&>(ad);
  CHECK_NE(dd, kNoDRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B24 | B20 |
                     ((static_cast<int32_t>(dd) >> 4)*B22) |
                     ((static_cast<int32_t>(dd) & 0xf)*B12) |
                     B11 | B9 | B8 | addr.vencoding();
  Emit(encoding);
}


void Arm32Assembler::vstrd(DRegister dd, const Address& ad, Condition cond) {
  const Address& addr = static_cast<const Address&>(ad);
  CHECK_NE(static_cast<Register>(addr.encodingArm() & (0xf << kRnShift)), PC);
  CHECK_NE(dd, kNoDRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B24 |
                     ((static_cast<int32_t>(dd) >> 4)*B22) |
                     ((static_cast<int32_t>(dd) & 0xf)*B12) |
                     B11 | B9 | B8 | addr.vencoding();
  Emit(encoding);
}


void Arm32Assembler::vpushs(SRegister reg, int nregs, Condition cond) {
  EmitVPushPop(static_cast<uint32_t>(reg), nregs, true, false, cond);
}


void Arm32Assembler::vpushd(DRegister reg, int nregs, Condition cond) {
  EmitVPushPop(static_cast<uint32_t>(reg), nregs, true, true, cond);
}


void Arm32Assembler::vpops(SRegister reg, int nregs, Condition cond) {
  EmitVPushPop(static_cast<uint32_t>(reg), nregs, false, false, cond);
}


void Arm32Assembler::vpopd(DRegister reg, int nregs, Condition cond) {
  EmitVPushPop(static_cast<uint32_t>(reg), nregs, false, true, cond);
}


void Arm32Assembler::EmitVPushPop(uint32_t reg, int nregs, bool push, bool dbl, Condition cond) {
  CHECK_NE(cond, kNoCondition);
  CHECK_GT(nregs, 0);
  uint32_t D;
  uint32_t Vd;
  if (dbl) {
    // Encoded as D:Vd.
    D = (reg >> 4) & 1;
    Vd = reg & 15U /* 0b1111 */;
  } else {
    // Encoded as Vd:D.
    D = reg & 1;
    Vd = (reg >> 1) & 15U /* 0b1111 */;
  }
  int32_t encoding = B27 | B26 | B21 | B19 | B18 | B16 |
                    B11 | B9 |
        (dbl ? B8 : 0) |
        (push ? B24 : (B23 | B20)) |
        static_cast<int32_t>(cond) << kConditionShift |
        nregs << (dbl ? 1 : 0) |
        D << 22 |
        Vd << 12;
  Emit(encoding);
}


void Arm32Assembler::EmitVFPsss(Condition cond, int32_t opcode,
                                SRegister sd, SRegister sn, SRegister sm) {
  CHECK_NE(sd, kNoSRegister);
  CHECK_NE(sn, kNoSRegister);
  CHECK_NE(sm, kNoSRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B25 | B11 | B9 | opcode |
                     ((static_cast<int32_t>(sd) & 1)*B22) |
                     ((static_cast<int32_t>(sn) >> 1)*B16) |
                     ((static_cast<int32_t>(sd) >> 1)*B12) |
                     ((static_cast<int32_t>(sn) & 1)*B7) |
                     ((static_cast<int32_t>(sm) & 1)*B5) |
                     (static_cast<int32_t>(sm) >> 1);
  Emit(encoding);
}


void Arm32Assembler::EmitVFPddd(Condition cond, int32_t opcode,
                                DRegister dd, DRegister dn, DRegister dm) {
  CHECK_NE(dd, kNoDRegister);
  CHECK_NE(dn, kNoDRegister);
  CHECK_NE(dm, kNoDRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B25 | B11 | B9 | B8 | opcode |
                     ((static_cast<int32_t>(dd) >> 4)*B22) |
                     ((static_cast<int32_t>(dn) & 0xf)*B16) |
                     ((static_cast<int32_t>(dd) & 0xf)*B12) |
                     ((static_cast<int32_t>(dn) >> 4)*B7) |
                     ((static_cast<int32_t>(dm) >> 4)*B5) |
                     (static_cast<int32_t>(dm) & 0xf);
  Emit(encoding);
}


void Arm32Assembler::EmitVFPsd(Condition cond, int32_t opcode,
                               SRegister sd, DRegister dm) {
  CHECK_NE(sd, kNoSRegister);
  CHECK_NE(dm, kNoDRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B25 | B11 | B9 | opcode |
                     ((static_cast<int32_t>(sd) & 1)*B22) |
                     ((static_cast<int32_t>(sd) >> 1)*B12) |
                     ((static_cast<int32_t>(dm) >> 4)*B5) |
                     (static_cast<int32_t>(dm) & 0xf);
  Emit(encoding);
}


void Arm32Assembler::EmitVFPds(Condition cond, int32_t opcode,
                             DRegister dd, SRegister sm) {
  CHECK_NE(dd, kNoDRegister);
  CHECK_NE(sm, kNoSRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B27 | B26 | B25 | B11 | B9 | opcode |
                     ((static_cast<int32_t>(dd) >> 4)*B22) |
                     ((static_cast<int32_t>(dd) & 0xf)*B12) |
                     ((static_cast<int32_t>(sm) & 1)*B5) |
                     (static_cast<int32_t>(sm) >> 1);
  Emit(encoding);
}


void Arm32Assembler::Lsl(Register rd, Register rm, uint32_t shift_imm,
                         Condition cond, SetCc set_cc) {
  CHECK_LE(shift_imm, 31u);
  mov(rd, ShifterOperand(rm, LSL, shift_imm), cond, set_cc);
}


void Arm32Assembler::Lsr(Register rd, Register rm, uint32_t shift_imm,
                         Condition cond, SetCc set_cc) {
  CHECK(1u <= shift_imm && shift_imm <= 32u);
  if (shift_imm == 32) shift_imm = 0;  // Comply to UAL syntax.
  mov(rd, ShifterOperand(rm, LSR, shift_imm), cond, set_cc);
}


void Arm32Assembler::Asr(Register rd, Register rm, uint32_t shift_imm,
                         Condition cond, SetCc set_cc) {
  CHECK(1u <= shift_imm && shift_imm <= 32u);
  if (shift_imm == 32) shift_imm = 0;  // Comply to UAL syntax.
  mov(rd, ShifterOperand(rm, ASR, shift_imm), cond, set_cc);
}


void Arm32Assembler::Ror(Register rd, Register rm, uint32_t shift_imm,
                         Condition cond, SetCc set_cc) {
  CHECK(1u <= shift_imm && shift_imm <= 31u);
  mov(rd, ShifterOperand(rm, ROR, shift_imm), cond, set_cc);
}

void Arm32Assembler::Rrx(Register rd, Register rm, Condition cond, SetCc set_cc) {
  mov(rd, ShifterOperand(rm, ROR, 0), cond, set_cc);
}


void Arm32Assembler::Lsl(Register rd, Register rm, Register rn,
                         Condition cond, SetCc set_cc) {
  mov(rd, ShifterOperand(rm, LSL, rn), cond, set_cc);
}


void Arm32Assembler::Lsr(Register rd, Register rm, Register rn,
                         Condition cond, SetCc set_cc) {
  mov(rd, ShifterOperand(rm, LSR, rn), cond, set_cc);
}


void Arm32Assembler::Asr(Register rd, Register rm, Register rn,
                         Condition cond, SetCc set_cc) {
  mov(rd, ShifterOperand(rm, ASR, rn), cond, set_cc);
}


void Arm32Assembler::Ror(Register rd, Register rm, Register rn,
                         Condition cond, SetCc set_cc) {
  mov(rd, ShifterOperand(rm, ROR, rn), cond, set_cc);
}

void Arm32Assembler::vmstat(Condition cond) {  // VMRS APSR_nzcv, FPSCR
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
      B27 | B26 | B25 | B23 | B22 | B21 | B20 | B16 |
      (static_cast<int32_t>(PC)*B12) |
      B11 | B9 | B4;
  Emit(encoding);
}


void Arm32Assembler::svc(uint32_t imm24) {
  CHECK(IsUint<24>(imm24)) << imm24;
  int32_t encoding = (AL << kConditionShift) | B27 | B26 | B25 | B24 | imm24;
  Emit(encoding);
}


void Arm32Assembler::bkpt(uint16_t imm16) {
  int32_t encoding = (AL << kConditionShift) | B24 | B21 |
                     ((imm16 >> 4) << 8) | B6 | B5 | B4 | (imm16 & 0xf);
  Emit(encoding);
}


void Arm32Assembler::blx(Register rm, Condition cond) {
  CHECK_NE(rm, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B24 | B21 | (0xfff << 8) | B5 | B4 |
                     (static_cast<int32_t>(rm) << kRmShift);
  Emit(encoding);
}


void Arm32Assembler::bx(Register rm, Condition cond) {
  CHECK_NE(rm, kNoRegister);
  CHECK_NE(cond, kNoCondition);
  int32_t encoding = (static_cast<int32_t>(cond) << kConditionShift) |
                     B24 | B21 | (0xfff << 8) | B4 |
                     (static_cast<int32_t>(rm) << kRmShift);
  Emit(encoding);
}


void Arm32Assembler::Push(Register rd, Condition cond) {
  str(rd, Address(SP, -kRegisterSize, Address::PreIndex), cond);
}


void Arm32Assembler::Pop(Register rd, Condition cond) {
  ldr(rd, Address(SP, kRegisterSize, Address::PostIndex), cond);
}


void Arm32Assembler::PushList(RegList regs, Condition cond) {
  stm(DB_W, SP, regs, cond);
}


void Arm32Assembler::PopList(RegList regs, Condition cond) {
  ldm(IA_W, SP, regs, cond);
}


void Arm32Assembler::Mov(Register rd, Register rm, Condition cond) {
  if (rd != rm) {
    mov(rd, ShifterOperand(rm), cond);
  }
}


void Arm32Assembler::Bind(Label* label) {
  CHECK(!label->IsBound());
  int bound_pc = buffer_.Size();
  while (label->IsLinked()) {
    int32_t position = label->Position();
    int32_t next = buffer_.Load<int32_t>(position);
    int32_t encoded = Arm32Assembler::EncodeBranchOffset(bound_pc - position, next);
    buffer_.Store<int32_t>(position, encoded);
    label->position_ = Arm32Assembler::DecodeBranchOffset(next);
  }
  label->BindTo(bound_pc);
}


int32_t Arm32Assembler::EncodeBranchOffset(int offset, int32_t inst) {
  // The offset is off by 8 due to the way the ARM CPUs read PC.
  offset -= 8;
  CHECK_ALIGNED(offset, 4);
  CHECK(IsInt(POPCOUNT(kBranchOffsetMask), offset)) << offset;

  // Properly preserve only the bits supported in the instruction.
  offset >>= 2;
  offset &= kBranchOffsetMask;
  return (inst & ~kBranchOffsetMask) | offset;
}


int Arm32Assembler::DecodeBranchOffset(int32_t inst) {
  // Sign-extend, left-shift by 2, then add 8.
  return ((((inst & kBranchOffsetMask) << 8) >> 6) + 8);
}


uint32_t Arm32Assembler::GetAdjustedPosition(uint32_t old_position ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "Unimplemented.";
  UNREACHABLE();
}

Literal* Arm32Assembler::NewLiteral(size_t size ATTRIBUTE_UNUSED,
                                    const uint8_t* data ATTRIBUTE_UNUSED)  {
  LOG(FATAL) << "Unimplemented.";
  UNREACHABLE();
}

void Arm32Assembler::LoadLiteral(Register rt ATTRIBUTE_UNUSED,
                                 Literal* literal ATTRIBUTE_UNUSED)  {
  LOG(FATAL) << "Unimplemented.";
  UNREACHABLE();
}

void Arm32Assembler::LoadLiteral(Register rt ATTRIBUTE_UNUSED, Register rt2 ATTRIBUTE_UNUSED,
                                 Literal* literal ATTRIBUTE_UNUSED)  {
  LOG(FATAL) << "Unimplemented.";
  UNREACHABLE();
}

void Arm32Assembler::LoadLiteral(SRegister sd ATTRIBUTE_UNUSED,
                                 Literal* literal ATTRIBUTE_UNUSED)  {
  LOG(FATAL) << "Unimplemented.";
  UNREACHABLE();
}

void Arm32Assembler::LoadLiteral(DRegister dd ATTRIBUTE_UNUSED,
                                 Literal* literal ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "Unimplemented.";
  UNREACHABLE();
}


void Arm32Assembler::AddConstant(Register rd, Register rn, int32_t value,
                                 Condition cond, SetCc set_cc) {
  if (value == 0 && set_cc != kCcSet) {
    if (rd != rn) {
      mov(rd, ShifterOperand(rn), cond, set_cc);
    }
    return;
  }
  // We prefer to select the shorter code sequence rather than selecting add for
  // positive values and sub for negatives ones, which would slightly improve
  // the readability of generated code for some constants.
  ShifterOperand shifter_op;
  if (ShifterOperandCanHoldArm32(value, &shifter_op)) {
    add(rd, rn, shifter_op, cond, set_cc);
  } else if (ShifterOperandCanHoldArm32(-value, &shifter_op)) {
    sub(rd, rn, shifter_op, cond, set_cc);
  } else {
    CHECK(rn != IP);
    if (ShifterOperandCanHoldArm32(~value, &shifter_op)) {
      mvn(IP, shifter_op, cond, kCcKeep);
      add(rd, rn, ShifterOperand(IP), cond, set_cc);
    } else if (ShifterOperandCanHoldArm32(~(-value), &shifter_op)) {
      mvn(IP, shifter_op, cond, kCcKeep);
      sub(rd, rn, ShifterOperand(IP), cond, set_cc);
    } else {
      movw(IP, Low16Bits(value), cond);
      uint16_t value_high = High16Bits(value);
      if (value_high != 0) {
        movt(IP, value_high, cond);
      }
      add(rd, rn, ShifterOperand(IP), cond, set_cc);
    }
  }
}


void Arm32Assembler::LoadImmediate(Register rd, int32_t value, Condition cond) {
  ShifterOperand shifter_op;
  if (ShifterOperandCanHoldArm32(value, &shifter_op)) {
    mov(rd, shifter_op, cond);
  } else if (ShifterOperandCanHoldArm32(~value, &shifter_op)) {
    mvn(rd, shifter_op, cond);
  } else {
    movw(rd, Low16Bits(value), cond);
    uint16_t value_high = High16Bits(value);
    if (value_high != 0) {
      movt(rd, value_high, cond);
    }
  }
}


// Implementation note: this method must emit at most one instruction when
// Address::CanHoldLoadOffsetArm.
void Arm32Assembler::LoadFromOffset(LoadOperandType type,
                                    Register reg,
                                    Register base,
                                    int32_t offset,
                                    Condition cond) {
  if (!Address::CanHoldLoadOffsetArm(type, offset)) {
    CHECK(base != IP);
    LoadImmediate(IP, offset, cond);
    add(IP, IP, ShifterOperand(base), cond);
    base = IP;
    offset = 0;
  }
  CHECK(Address::CanHoldLoadOffsetArm(type, offset));
  switch (type) {
    case kLoadSignedByte:
      ldrsb(reg, Address(base, offset), cond);
      break;
    case kLoadUnsignedByte:
      ldrb(reg, Address(base, offset), cond);
      break;
    case kLoadSignedHalfword:
      ldrsh(reg, Address(base, offset), cond);
      break;
    case kLoadUnsignedHalfword:
      ldrh(reg, Address(base, offset), cond);
      break;
    case kLoadWord:
      ldr(reg, Address(base, offset), cond);
      break;
    case kLoadWordPair:
      ldrd(reg, Address(base, offset), cond);
      break;
    default:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
  }
}


// Implementation note: this method must emit at most one instruction when
// Address::CanHoldLoadOffsetArm, as expected by JIT::GuardedLoadFromOffset.
void Arm32Assembler::LoadSFromOffset(SRegister reg,
                                     Register base,
                                     int32_t offset,
                                     Condition cond) {
  if (!Address::CanHoldLoadOffsetArm(kLoadSWord, offset)) {
    CHECK_NE(base, IP);
    LoadImmediate(IP, offset, cond);
    add(IP, IP, ShifterOperand(base), cond);
    base = IP;
    offset = 0;
  }
  CHECK(Address::CanHoldLoadOffsetArm(kLoadSWord, offset));
  vldrs(reg, Address(base, offset), cond);
}


// Implementation note: this method must emit at most one instruction when
// Address::CanHoldLoadOffsetArm, as expected by JIT::GuardedLoadFromOffset.
void Arm32Assembler::LoadDFromOffset(DRegister reg,
                                     Register base,
                                     int32_t offset,
                                     Condition cond) {
  if (!Address::CanHoldLoadOffsetArm(kLoadDWord, offset)) {
    CHECK_NE(base, IP);
    LoadImmediate(IP, offset, cond);
    add(IP, IP, ShifterOperand(base), cond);
    base = IP;
    offset = 0;
  }
  CHECK(Address::CanHoldLoadOffsetArm(kLoadDWord, offset));
  vldrd(reg, Address(base, offset), cond);
}


// Implementation note: this method must emit at most one instruction when
// Address::CanHoldStoreOffsetArm.
void Arm32Assembler::StoreToOffset(StoreOperandType type,
                                   Register reg,
                                   Register base,
                                   int32_t offset,
                                   Condition cond) {
  if (!Address::CanHoldStoreOffsetArm(type, offset)) {
    CHECK(reg != IP);
    CHECK(base != IP);
    LoadImmediate(IP, offset, cond);
    add(IP, IP, ShifterOperand(base), cond);
    base = IP;
    offset = 0;
  }
  CHECK(Address::CanHoldStoreOffsetArm(type, offset));
  switch (type) {
    case kStoreByte:
      strb(reg, Address(base, offset), cond);
      break;
    case kStoreHalfword:
      strh(reg, Address(base, offset), cond);
      break;
    case kStoreWord:
      str(reg, Address(base, offset), cond);
      break;
    case kStoreWordPair:
      strd(reg, Address(base, offset), cond);
      break;
    default:
      LOG(FATAL) << "UNREACHABLE";
      UNREACHABLE();
  }
}


// Implementation note: this method must emit at most one instruction when
// Address::CanHoldStoreOffsetArm, as expected by JIT::GuardedStoreToOffset.
void Arm32Assembler::StoreSToOffset(SRegister reg,
                                    Register base,
                                    int32_t offset,
                                    Condition cond) {
  if (!Address::CanHoldStoreOffsetArm(kStoreSWord, offset)) {
    CHECK_NE(base, IP);
    LoadImmediate(IP, offset, cond);
    add(IP, IP, ShifterOperand(base), cond);
    base = IP;
    offset = 0;
  }
  CHECK(Address::CanHoldStoreOffsetArm(kStoreSWord, offset));
  vstrs(reg, Address(base, offset), cond);
}


// Implementation note: this method must emit at most one instruction when
// Address::CanHoldStoreOffsetArm, as expected by JIT::GuardedStoreSToOffset.
void Arm32Assembler::StoreDToOffset(DRegister reg,
                                    Register base,
                                    int32_t offset,
                                    Condition cond) {
  if (!Address::CanHoldStoreOffsetArm(kStoreDWord, offset)) {
    CHECK_NE(base, IP);
    LoadImmediate(IP, offset, cond);
    add(IP, IP, ShifterOperand(base), cond);
    base = IP;
    offset = 0;
  }
  CHECK(Address::CanHoldStoreOffsetArm(kStoreDWord, offset));
  vstrd(reg, Address(base, offset), cond);
}


void Arm32Assembler::MemoryBarrier(ManagedRegister mscratch) {
  CHECK_EQ(mscratch.AsArm().AsCoreRegister(), R12);
  dmb(SY);
}


void Arm32Assembler::dmb(DmbOptions flavor) {
  int32_t encoding = 0xf57ff05f;  // dmb
  Emit(encoding | flavor);
}


void Arm32Assembler::cbz(Register rn ATTRIBUTE_UNUSED, Label* target ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "cbz is not supported on ARM32";
}


void Arm32Assembler::cbnz(Register rn ATTRIBUTE_UNUSED, Label* target ATTRIBUTE_UNUSED) {
  LOG(FATAL) << "cbnz is not supported on ARM32";
}


void Arm32Assembler::CompareAndBranchIfZero(Register r, Label* label) {
  cmp(r, ShifterOperand(0));
  b(label, EQ);
}


void Arm32Assembler::CompareAndBranchIfNonZero(Register r, Label* label) {
  cmp(r, ShifterOperand(0));
  b(label, NE);
}


}  // namespace arm
}  // namespace art
