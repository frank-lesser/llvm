//===- AMDGPURegisterBankInfo.cpp -------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the targeting of the RegisterBankInfo class for
/// AMDGPU.
/// \todo This should be generated by TableGen.
//===----------------------------------------------------------------------===//

#include "AMDGPURegisterBankInfo.h"
#include "AMDGPUInstrInfo.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/RegisterBank.h"
#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"

#define GET_TARGET_REGBANK_IMPL
#include "AMDGPUGenRegisterBank.inc"

// This file will be TableGen'ed at some point.
#include "AMDGPUGenRegisterBankInfo.def"

using namespace llvm;

AMDGPURegisterBankInfo::AMDGPURegisterBankInfo(const TargetRegisterInfo &TRI)
    : AMDGPUGenRegisterBankInfo(),
      TRI(static_cast<const SIRegisterInfo*>(&TRI)) {

  // HACK: Until this is fully tablegen'd.
  static bool AlreadyInit = false;
  if (AlreadyInit)
    return;

  AlreadyInit = true;

  const RegisterBank &RBSGPR = getRegBank(AMDGPU::SGPRRegBankID);
  (void)RBSGPR;
  assert(&RBSGPR == &AMDGPU::SGPRRegBank);

  const RegisterBank &RBVGPR = getRegBank(AMDGPU::VGPRRegBankID);
  (void)RBVGPR;
  assert(&RBVGPR == &AMDGPU::VGPRRegBank);

}

static bool isConstant(const MachineOperand &MO, int64_t &C) {
  const MachineFunction *MF = MO.getParent()->getParent()->getParent();
  const MachineRegisterInfo &MRI = MF->getRegInfo();
  const MachineInstr *Def = MRI.getVRegDef(MO.getReg());
  if (!Def)
    return false;

  if (Def->getOpcode() == AMDGPU::G_CONSTANT) {
    C = Def->getOperand(1).getCImm()->getSExtValue();
    return true;
  }

  if (Def->getOpcode() == AMDGPU::COPY)
    return isConstant(Def->getOperand(1), C);

  return false;
}

unsigned AMDGPURegisterBankInfo::copyCost(const RegisterBank &Dst,
                                          const RegisterBank &Src,
                                          unsigned Size) const {
  if (Dst.getID() == AMDGPU::SGPRRegBankID &&
      Src.getID() == AMDGPU::VGPRRegBankID) {
    return std::numeric_limits<unsigned>::max();
  }

  // SGPRRegBank with size 1 is actually vcc or another 64-bit sgpr written by
  // the valu.
  if (Size == 1 && Dst.getID() == AMDGPU::SCCRegBankID &&
      (Src.getID() == AMDGPU::SGPRRegBankID ||
       Src.getID() == AMDGPU::VGPRRegBankID ||
       Src.getID() == AMDGPU::VCCRegBankID))
    return std::numeric_limits<unsigned>::max();

  if (Dst.getID() == AMDGPU::SCCRegBankID &&
      Src.getID() == AMDGPU::VCCRegBankID)
    return std::numeric_limits<unsigned>::max();

  return RegisterBankInfo::copyCost(Dst, Src, Size);
}

unsigned AMDGPURegisterBankInfo::getBreakDownCost(
  const ValueMapping &ValMapping,
  const RegisterBank *CurBank) const {
  assert(ValMapping.NumBreakDowns == 2 &&
         ValMapping.BreakDown[0].Length == 32 &&
         ValMapping.BreakDown[0].StartIdx == 0 &&
         ValMapping.BreakDown[1].Length == 32 &&
         ValMapping.BreakDown[1].StartIdx == 32 &&
         ValMapping.BreakDown[0].RegBank == ValMapping.BreakDown[1].RegBank);

  // 32-bit extract of a 64-bit value is just access of a subregister, so free.
  // TODO: Cost of 0 hits assert, though it's not clear it's what we really
  // want.

  // TODO: 32-bit insert to a 64-bit SGPR may incur a non-free copy due to SGPR
  // alignment restrictions, but this probably isn't important.
  return 1;
}

const RegisterBank &AMDGPURegisterBankInfo::getRegBankFromRegClass(
    const TargetRegisterClass &RC) const {

  if (TRI->isSGPRClass(&RC))
    return getRegBank(AMDGPU::SGPRRegBankID);

  return getRegBank(AMDGPU::VGPRRegBankID);
}

RegisterBankInfo::InstructionMappings
AMDGPURegisterBankInfo::getInstrAlternativeMappings(
    const MachineInstr &MI) const {

  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();


  InstructionMappings AltMappings;
  switch (MI.getOpcode()) {
  case TargetOpcode::G_AND:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_XOR: {
    unsigned Size = getSizeInBits(MI.getOperand(0).getReg(), MRI, *TRI);
    if (Size != 64)
      break;

    const InstructionMapping &SSMapping = getInstructionMapping(
      1, 1, getOperandsMapping(
        {AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
         AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
         AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size)}),
      3); // Num Operands
    AltMappings.push_back(&SSMapping);

    const InstructionMapping &VVMapping = getInstructionMapping(
      2, 2, getOperandsMapping(
        {AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size),
         AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size),
         AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size)}),
      3); // Num Operands
    AltMappings.push_back(&VVMapping);

    const InstructionMapping &SVMapping = getInstructionMapping(
      3, 3, getOperandsMapping(
        {AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size),
         AMDGPU::getValueMappingSGPR64Only(AMDGPU::SGPRRegBankID, Size),
         AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size)}),
      3); // Num Operands
    AltMappings.push_back(&SVMapping);

    // SGPR in LHS is slightly preferrable, so make it VS more expnesive than
    // SV.
    const InstructionMapping &VSMapping = getInstructionMapping(
      3, 4, getOperandsMapping(
        {AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size),
         AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size),
         AMDGPU::getValueMappingSGPR64Only(AMDGPU::SGPRRegBankID, Size)}),
      3); // Num Operands
    AltMappings.push_back(&VSMapping);
    break;
  }
  case TargetOpcode::G_LOAD: {
    unsigned Size = getSizeInBits(MI.getOperand(0).getReg(), MRI, *TRI);
    // FIXME: Should we be hard coding the size for these mappings?
    const InstructionMapping &SSMapping = getInstructionMapping(
        1, 1, getOperandsMapping(
                  {AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
                   AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 64)}),
        2); // Num Operands
    AltMappings.push_back(&SSMapping);

    const InstructionMapping &VVMapping = getInstructionMapping(
        2, 1, getOperandsMapping(
                  {AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size),
                   AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, 64)}),
        2); // Num Operands
    AltMappings.push_back(&VVMapping);

    // FIXME: Should this be the pointer-size (64-bits) or the size of the
    // register that will hold the bufffer resourc (128-bits).
    const InstructionMapping &VSMapping = getInstructionMapping(
        3, 1, getOperandsMapping(
                  {AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size),
                   AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 64)}),
        2); // Num Operands
    AltMappings.push_back(&VSMapping);

    return AltMappings;

  }
  case TargetOpcode::G_ICMP: {
    unsigned Size = getSizeInBits(MI.getOperand(2).getReg(), MRI, *TRI);
    const InstructionMapping &SSMapping = getInstructionMapping(1, 1,
      getOperandsMapping({AMDGPU::getValueMapping(AMDGPU::SCCRegBankID, 1),
                          nullptr, // Predicate operand.
                          AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size)}),
      4); // Num Operands
    AltMappings.push_back(&SSMapping);

    const InstructionMapping &SVMapping = getInstructionMapping(2, 1,
      getOperandsMapping({AMDGPU::getValueMapping(AMDGPU::VCCRegBankID, 1),
                          nullptr, // Predicate operand.
                          AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size)}),
      4); // Num Operands
    AltMappings.push_back(&SVMapping);

    const InstructionMapping &VSMapping = getInstructionMapping(3, 1,
      getOperandsMapping({AMDGPU::getValueMapping(AMDGPU::VCCRegBankID, 1),
                          nullptr, // Predicate operand.
                          AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size)}),
      4); // Num Operands
    AltMappings.push_back(&VSMapping);

    const InstructionMapping &VVMapping = getInstructionMapping(4, 1,
      getOperandsMapping({AMDGPU::getValueMapping(AMDGPU::VCCRegBankID, 1),
                          nullptr, // Predicate operand.
                          AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size)}),
      4); // Num Operands
    AltMappings.push_back(&VVMapping);

    return AltMappings;
  }
  case TargetOpcode::G_SELECT: {
    unsigned Size = getSizeInBits(MI.getOperand(0).getReg(), MRI, *TRI);
    const InstructionMapping &SSMapping = getInstructionMapping(1, 1,
      getOperandsMapping({AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::SCCRegBankID, 1),
                          AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size)}),
      4); // Num Operands
    AltMappings.push_back(&SSMapping);

    const InstructionMapping &VVMapping = getInstructionMapping(2, 1,
      getOperandsMapping({AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::VCCRegBankID, 1),
                          AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size),
                          AMDGPU::getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size)}),
      4); // Num Operands
    AltMappings.push_back(&VVMapping);

    return AltMappings;
  }
  case TargetOpcode::G_UADDE:
  case TargetOpcode::G_USUBE:
  case TargetOpcode::G_SADDE:
  case TargetOpcode::G_SSUBE: {
    unsigned Size = getSizeInBits(MI.getOperand(0).getReg(), MRI, *TRI);
    const InstructionMapping &SSMapping = getInstructionMapping(1, 1,
      getOperandsMapping(
        {AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
         AMDGPU::getValueMapping(AMDGPU::SCCRegBankID, 1),
         AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
         AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size),
         AMDGPU::getValueMapping(AMDGPU::SCCRegBankID, 1)}),
      5); // Num Operands
    AltMappings.push_back(&SSMapping);

    const InstructionMapping &VVMapping = getInstructionMapping(2, 1,
      getOperandsMapping({AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::VCCRegBankID, 1),
                          AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size),
                          AMDGPU::getValueMapping(AMDGPU::VCCRegBankID, 1)}),
      5); // Num Operands
    AltMappings.push_back(&VVMapping);
    return AltMappings;
  }
  case AMDGPU::G_BRCOND: {
    assert(MRI.getType(MI.getOperand(0).getReg()).getSizeInBits() == 1);

    const InstructionMapping &SMapping = getInstructionMapping(
      1, 1, getOperandsMapping(
        {AMDGPU::getValueMapping(AMDGPU::SCCRegBankID, 1), nullptr}),
      2); // Num Operands
    AltMappings.push_back(&SMapping);

    const InstructionMapping &VMapping = getInstructionMapping(
      1, 1, getOperandsMapping(
        {AMDGPU::getValueMapping(AMDGPU::VCCRegBankID, 1), nullptr }),
      2); // Num Operands
    AltMappings.push_back(&VMapping);
    return AltMappings;
  }
  default:
    break;
  }
  return RegisterBankInfo::getInstrAlternativeMappings(MI);
}

void AMDGPURegisterBankInfo::split64BitValueForMapping(
  MachineIRBuilder &B,
  SmallVector<unsigned, 2> &Regs,
  LLT HalfTy,
  unsigned Reg) const {
  assert(HalfTy.getSizeInBits() == 32);
  MachineRegisterInfo *MRI = B.getMRI();
  unsigned LoLHS = MRI->createGenericVirtualRegister(HalfTy);
  unsigned HiLHS = MRI->createGenericVirtualRegister(HalfTy);
  const RegisterBank *Bank = getRegBank(Reg, *MRI, *TRI);
  MRI->setRegBank(LoLHS, *Bank);
  MRI->setRegBank(HiLHS, *Bank);

  Regs.push_back(LoLHS);
  Regs.push_back(HiLHS);

  B.buildInstr(AMDGPU::G_UNMERGE_VALUES)
    .addDef(LoLHS)
    .addDef(HiLHS)
    .addUse(Reg);
}

/// Replace the current type each register in \p Regs has with \p NewTy
static void setRegsToType(MachineRegisterInfo &MRI, ArrayRef<unsigned> Regs,
                          LLT NewTy) {
  for (unsigned Reg : Regs) {
    assert(MRI.getType(Reg).getSizeInBits() == NewTy.getSizeInBits());
    MRI.setType(Reg, NewTy);
  }
}

static LLT getHalfSizedType(LLT Ty) {
  if (Ty.isVector()) {
    assert(Ty.getNumElements() % 2 == 0);
    return LLT::scalarOrVector(Ty.getNumElements() / 2, Ty.getElementType());
  }

  assert(Ty.getSizeInBits() % 2 == 0);
  return LLT::scalar(Ty.getSizeInBits() / 2);
}

void AMDGPURegisterBankInfo::applyMappingImpl(
    const OperandsMapper &OpdMapper) const {
  MachineInstr &MI = OpdMapper.getMI();
  unsigned Opc = MI.getOpcode();
  MachineRegisterInfo &MRI = OpdMapper.getMRI();
  switch (Opc) {
  case AMDGPU::G_SELECT: {
    unsigned DstReg = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(DstReg);
    if (DstTy.getSizeInBits() != 64)
      break;

    LLT HalfTy = getHalfSizedType(DstTy);

    SmallVector<unsigned, 2> DefRegs(OpdMapper.getVRegs(0));
    SmallVector<unsigned, 1> Src0Regs(OpdMapper.getVRegs(1));
    SmallVector<unsigned, 2> Src1Regs(OpdMapper.getVRegs(2));
    SmallVector<unsigned, 2> Src2Regs(OpdMapper.getVRegs(3));

    // All inputs are SGPRs, nothing special to do.
    if (DefRegs.empty()) {
      assert(Src1Regs.empty() && Src2Regs.empty());
      break;
    }

    MachineIRBuilder B(MI);
    if (Src0Regs.empty())
      Src0Regs.push_back(MI.getOperand(1).getReg());
    else {
      assert(Src0Regs.size() == 1);
    }

    if (Src1Regs.empty())
      split64BitValueForMapping(B, Src1Regs, HalfTy, MI.getOperand(2).getReg());
    else {
      setRegsToType(MRI, Src1Regs, HalfTy);
    }

    if (Src2Regs.empty())
      split64BitValueForMapping(B, Src2Regs, HalfTy, MI.getOperand(3).getReg());
    else
      setRegsToType(MRI, Src2Regs, HalfTy);

    setRegsToType(MRI, DefRegs, HalfTy);

    B.buildSelect(DefRegs[0], Src0Regs[0], Src1Regs[0], Src2Regs[0]);
    B.buildSelect(DefRegs[1], Src0Regs[0], Src1Regs[1], Src2Regs[1]);

    MRI.setRegBank(DstReg, getRegBank(AMDGPU::VGPRRegBankID));
    MI.eraseFromParent();
    return;
  }
  case AMDGPU::G_AND:
  case AMDGPU::G_OR:
  case AMDGPU::G_XOR: {
    // 64-bit and is only available on the SALU, so split into 2 32-bit ops if
    // there is a VGPR input.
    unsigned DstReg = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(DstReg);
    if (DstTy.getSizeInBits() != 64)
      break;

    LLT HalfTy = getHalfSizedType(DstTy);
    SmallVector<unsigned, 2> DefRegs(OpdMapper.getVRegs(0));
    SmallVector<unsigned, 2> Src0Regs(OpdMapper.getVRegs(1));
    SmallVector<unsigned, 2> Src1Regs(OpdMapper.getVRegs(2));

    // All inputs are SGPRs, nothing special to do.
    if (DefRegs.empty()) {
      assert(Src0Regs.empty() && Src1Regs.empty());
      break;
    }

    assert(DefRegs.size() == 2);
    assert(Src0Regs.size() == Src1Regs.size() &&
           (Src0Regs.empty() || Src0Regs.size() == 2));

    // Depending on where the source registers came from, the generic code may
    // have decided to split the inputs already or not. If not, we still need to
    // extract the values.
    MachineIRBuilder B(MI);

    if (Src0Regs.empty())
      split64BitValueForMapping(B, Src0Regs, HalfTy, MI.getOperand(1).getReg());
    else
      setRegsToType(MRI, Src0Regs, HalfTy);

    if (Src1Regs.empty())
      split64BitValueForMapping(B, Src1Regs, HalfTy, MI.getOperand(2).getReg());
    else
      setRegsToType(MRI, Src1Regs, HalfTy);

    setRegsToType(MRI, DefRegs, HalfTy);

    B.buildInstr(Opc)
      .addDef(DefRegs[0])
      .addUse(Src0Regs[0])
      .addUse(Src1Regs[0]);

    B.buildInstr(Opc)
      .addDef(DefRegs[1])
      .addUse(Src0Regs[1])
      .addUse(Src1Regs[1]);

    MRI.setRegBank(DstReg, getRegBank(AMDGPU::VGPRRegBankID));
    MI.eraseFromParent();
    return;
  }
  default:
    break;
  }

  return applyDefaultMapping(OpdMapper);
}

static bool isInstrUniform(const MachineInstr &MI) {
  if (!MI.hasOneMemOperand())
    return false;

  const MachineMemOperand *MMO = *MI.memoperands_begin();
  return AMDGPUInstrInfo::isUniformMMO(MMO);
}

bool AMDGPURegisterBankInfo::isSALUMapping(const MachineInstr &MI) const {
  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned i = 0, e = MI.getNumOperands();i != e; ++i) {
    if (!MI.getOperand(i).isReg())
      continue;
    unsigned Reg = MI.getOperand(i).getReg();
    if (const RegisterBank *Bank = getRegBank(Reg, MRI, *TRI)) {
      if (Bank->getID() == AMDGPU::VGPRRegBankID)
        return false;

      assert(Bank->getID() == AMDGPU::SGPRRegBankID ||
             Bank->getID() == AMDGPU::SCCRegBankID);
    }
  }
  return true;
}

const RegisterBankInfo::InstructionMapping &
AMDGPURegisterBankInfo::getDefaultMappingSOP(const MachineInstr &MI) const {
  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  SmallVector<const ValueMapping*, 8> OpdsMapping(MI.getNumOperands());

  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    unsigned Size = getSizeInBits(MI.getOperand(i).getReg(), MRI, *TRI);
    unsigned BankID = Size == 1 ? AMDGPU::SCCRegBankID : AMDGPU::SGPRRegBankID;
    OpdsMapping[i] = AMDGPU::getValueMapping(BankID, Size);
  }
  return getInstructionMapping(1, 1, getOperandsMapping(OpdsMapping),
                               MI.getNumOperands());
}

const RegisterBankInfo::InstructionMapping &
AMDGPURegisterBankInfo::getDefaultMappingVOP(const MachineInstr &MI) const {
  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  SmallVector<const ValueMapping*, 8> OpdsMapping(MI.getNumOperands());
  unsigned OpdIdx = 0;

  unsigned Size0 = getSizeInBits(MI.getOperand(0).getReg(), MRI, *TRI);
  OpdsMapping[OpdIdx++] = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size0);

  if (MI.getOperand(OpdIdx).isIntrinsicID())
    OpdsMapping[OpdIdx++] = nullptr;

  unsigned Reg1 = MI.getOperand(OpdIdx).getReg();
  unsigned Size1 = getSizeInBits(Reg1, MRI, *TRI);

  unsigned DefaultBankID = Size1 == 1 ?
    AMDGPU::VCCRegBankID : AMDGPU::VGPRRegBankID;
  unsigned Bank1 = getRegBankID(Reg1, MRI, *TRI, DefaultBankID);

  OpdsMapping[OpdIdx++] = AMDGPU::getValueMapping(Bank1, Size1);

  for (unsigned e = MI.getNumOperands(); OpdIdx != e; ++OpdIdx) {
    unsigned Size = getSizeInBits(MI.getOperand(OpdIdx).getReg(), MRI, *TRI);
    unsigned BankID = Size == 1 ? AMDGPU::VCCRegBankID : AMDGPU::VGPRRegBankID;
    OpdsMapping[OpdIdx] = AMDGPU::getValueMapping(BankID, Size);
  }

  return getInstructionMapping(1, 1, getOperandsMapping(OpdsMapping),
                               MI.getNumOperands());
}

const RegisterBankInfo::InstructionMapping &
AMDGPURegisterBankInfo::getDefaultMappingAllVGPR(const MachineInstr &MI) const {
  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  SmallVector<const ValueMapping*, 8> OpdsMapping(MI.getNumOperands());

  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    unsigned Size = getSizeInBits(MI.getOperand(I).getReg(), MRI, *TRI);
    OpdsMapping[I] = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size);
  }

  return getInstructionMapping(1, 1, getOperandsMapping(OpdsMapping),
                               MI.getNumOperands());
}

const RegisterBankInfo::InstructionMapping &
AMDGPURegisterBankInfo::getInstrMappingForLoad(const MachineInstr &MI) const {

  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  SmallVector<const ValueMapping*, 8> OpdsMapping(MI.getNumOperands());
  unsigned Size = getSizeInBits(MI.getOperand(0).getReg(), MRI, *TRI);
  unsigned PtrSize = getSizeInBits(MI.getOperand(1).getReg(), MRI, *TRI);

  const ValueMapping *ValMapping;
  const ValueMapping *PtrMapping;

  if (isInstrUniform(MI)) {
    // We have a uniform instruction so we want to use an SMRD load
    ValMapping = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size);
    PtrMapping = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, PtrSize);
  } else {
    ValMapping = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size);
    // FIXME: What would happen if we used SGPRRegBankID here?
    PtrMapping = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, PtrSize);
  }

  OpdsMapping[0] = ValMapping;
  OpdsMapping[1] = PtrMapping;
  const RegisterBankInfo::InstructionMapping &Mapping = getInstructionMapping(
      1, 1, getOperandsMapping(OpdsMapping), MI.getNumOperands());
  return Mapping;

  // FIXME: Do we want to add a mapping for FLAT load, or should we just
  // handle that during instruction selection?
}

unsigned
AMDGPURegisterBankInfo::getRegBankID(unsigned Reg,
                                     const MachineRegisterInfo &MRI,
                                     const TargetRegisterInfo &TRI,
                                     unsigned Default) const {

  const RegisterBank *Bank = getRegBank(Reg, MRI, TRI);
  return Bank ? Bank->getID() : Default;
}

///
/// This function must return a legal mapping, because
/// AMDGPURegisterBankInfo::getInstrAlternativeMappings() is not called
/// in RegBankSelect::Mode::Fast.  Any mapping that would cause a
/// VGPR to SGPR generated is illegal.
///
const RegisterBankInfo::InstructionMapping &
AMDGPURegisterBankInfo::getInstrMapping(const MachineInstr &MI) const {
  const RegisterBankInfo::InstructionMapping &Mapping = getInstrMappingImpl(MI);

  if (Mapping.isValid())
    return Mapping;

  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  SmallVector<const ValueMapping*, 8> OpdsMapping(MI.getNumOperands());

  switch (MI.getOpcode()) {
  default:
    return getInvalidInstructionMapping();

  case AMDGPU::G_AND:
  case AMDGPU::G_OR:
  case AMDGPU::G_XOR: {
    unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    if (Size == 1) {
      OpdsMapping[0] = OpdsMapping[1] =
        OpdsMapping[2] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size);
      break;
    }

    if (Size == 64) {

      if (isSALUMapping(MI)) {
        OpdsMapping[0] = getValueMappingSGPR64Only(AMDGPU::SGPRRegBankID, Size);
        OpdsMapping[1] = OpdsMapping[2] = OpdsMapping[0];
      } else {
        OpdsMapping[0] = getValueMappingSGPR64Only(AMDGPU::VGPRRegBankID, Size);
        unsigned Bank1 = getRegBankID(MI.getOperand(1).getReg(), MRI, *TRI/*, DefaultBankID*/);
        OpdsMapping[1] = AMDGPU::getValueMapping(Bank1, Size);

        unsigned Bank2 = getRegBankID(MI.getOperand(2).getReg(), MRI, *TRI/*, DefaultBankID*/);
        OpdsMapping[2] = AMDGPU::getValueMapping(Bank2, Size);
      }

      break;
    }

    LLVM_FALLTHROUGH;
  }

  case AMDGPU::G_GEP:
  case AMDGPU::G_ADD:
  case AMDGPU::G_SUB:
  case AMDGPU::G_MUL:
  case AMDGPU::G_SHL:
  case AMDGPU::G_LSHR:
  case AMDGPU::G_ASHR:
  case AMDGPU::G_UADDO:
  case AMDGPU::G_SADDO:
  case AMDGPU::G_USUBO:
  case AMDGPU::G_SSUBO:
  case AMDGPU::G_UADDE:
  case AMDGPU::G_SADDE:
  case AMDGPU::G_USUBE:
  case AMDGPU::G_SSUBE:
  case AMDGPU::G_UMULH:
  case AMDGPU::G_SMULH:
    if (isSALUMapping(MI))
      return getDefaultMappingSOP(MI);
    LLVM_FALLTHROUGH;

  case AMDGPU::G_FADD:
  case AMDGPU::G_FSUB:
  case AMDGPU::G_FPTOSI:
  case AMDGPU::G_FPTOUI:
  case AMDGPU::G_FMUL:
  case AMDGPU::G_FMA:
  case AMDGPU::G_FSQRT:
  case AMDGPU::G_SITOFP:
  case AMDGPU::G_UITOFP:
  case AMDGPU::G_FPTRUNC:
  case AMDGPU::G_FPEXT:
  case AMDGPU::G_FEXP2:
  case AMDGPU::G_FLOG2:
  case AMDGPU::G_INTRINSIC_TRUNC:
  case AMDGPU::G_INTRINSIC_ROUND:
    return getDefaultMappingVOP(MI);
  case AMDGPU::G_IMPLICIT_DEF: {
    unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    OpdsMapping[0] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size);
    break;
  }
  case AMDGPU::G_FCONSTANT:
  case AMDGPU::G_CONSTANT:
  case AMDGPU::G_FRAME_INDEX:
  case AMDGPU::G_BLOCK_ADDR: {
    unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    OpdsMapping[0] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size);
    break;
  }
  case AMDGPU::G_INSERT: {
    unsigned BankID = isSALUMapping(MI) ? AMDGPU::SGPRRegBankID :
                                          AMDGPU::VGPRRegBankID;
    unsigned DstSize = getSizeInBits(MI.getOperand(0).getReg(), MRI, *TRI);
    unsigned SrcSize = getSizeInBits(MI.getOperand(1).getReg(), MRI, *TRI);
    unsigned EltSize = getSizeInBits(MI.getOperand(2).getReg(), MRI, *TRI);
    OpdsMapping[0] = AMDGPU::getValueMapping(BankID, DstSize);
    OpdsMapping[1] = AMDGPU::getValueMapping(BankID, SrcSize);
    OpdsMapping[2] = AMDGPU::getValueMapping(BankID, EltSize);
    OpdsMapping[3] = nullptr;
    break;
  }
  case AMDGPU::G_EXTRACT: {
    unsigned BankID = getRegBankID(MI.getOperand(1).getReg(), MRI, *TRI);
    unsigned DstSize = getSizeInBits(MI.getOperand(0).getReg(), MRI, *TRI);
    unsigned SrcSize = getSizeInBits(MI.getOperand(1).getReg(), MRI, *TRI);
    OpdsMapping[0] = AMDGPU::getValueMapping(BankID, DstSize);
    OpdsMapping[1] = AMDGPU::getValueMapping(BankID, SrcSize);
    OpdsMapping[2] = nullptr;
    break;
  }
  case AMDGPU::G_MERGE_VALUES: {
    unsigned Bank = isSALUMapping(MI) ?
      AMDGPU::SGPRRegBankID : AMDGPU::VGPRRegBankID;
    unsigned DstSize = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    unsigned SrcSize = MRI.getType(MI.getOperand(1).getReg()).getSizeInBits();

    OpdsMapping[0] = AMDGPU::getValueMapping(Bank, DstSize);
    // Op1 and Dst should use the same register bank.
    for (unsigned i = 1, e = MI.getNumOperands(); i != e; ++i)
      OpdsMapping[i] = AMDGPU::getValueMapping(Bank, SrcSize);
    break;
  }
  case AMDGPU::G_BITCAST:
  case AMDGPU::G_INTTOPTR:
  case AMDGPU::G_PTRTOINT:
  case AMDGPU::G_CTLZ:
  case AMDGPU::G_CTLZ_ZERO_UNDEF:
  case AMDGPU::G_CTTZ:
  case AMDGPU::G_CTTZ_ZERO_UNDEF:
  case AMDGPU::G_CTPOP:
  case AMDGPU::G_BSWAP:
  case AMDGPU::G_FABS:
  case AMDGPU::G_FNEG: {
    unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    unsigned BankID = getRegBankID(MI.getOperand(1).getReg(), MRI, *TRI);
    OpdsMapping[0] = OpdsMapping[1] = AMDGPU::getValueMapping(BankID, Size);
    break;
  }
  case AMDGPU::G_TRUNC: {
    unsigned Dst = MI.getOperand(0).getReg();
    unsigned Src = MI.getOperand(1).getReg();
    unsigned Bank = getRegBankID(Src, MRI, *TRI);
    unsigned DstSize = getSizeInBits(Dst, MRI, *TRI);
    unsigned SrcSize = getSizeInBits(Src, MRI, *TRI);
    OpdsMapping[0] = AMDGPU::getValueMapping(Bank, DstSize);
    OpdsMapping[1] = AMDGPU::getValueMapping(Bank, SrcSize);
    break;
  }
  case AMDGPU::G_ZEXT:
  case AMDGPU::G_SEXT:
  case AMDGPU::G_ANYEXT: {
    unsigned Dst = MI.getOperand(0).getReg();
    unsigned Src = MI.getOperand(1).getReg();
    unsigned DstSize = getSizeInBits(Dst, MRI, *TRI);
    unsigned SrcSize = getSizeInBits(Src, MRI, *TRI);
    unsigned SrcBank = getRegBankID(Src, MRI, *TRI,
                                    SrcSize == 1 ? AMDGPU::SGPRRegBankID :
                                    AMDGPU::VGPRRegBankID);
    unsigned DstBank = SrcBank;
    if (SrcSize == 1) {
      if (SrcBank == AMDGPU::SGPRRegBankID)
        DstBank = AMDGPU::VGPRRegBankID;
      else
        DstBank = AMDGPU::SGPRRegBankID;
    }

    OpdsMapping[0] = AMDGPU::getValueMapping(DstBank, DstSize);
    OpdsMapping[1] = AMDGPU::getValueMapping(SrcBank, SrcSize);
    break;
  }
  case AMDGPU::G_FCMP: {
    unsigned Size = MRI.getType(MI.getOperand(2).getReg()).getSizeInBits();
    unsigned Op2Bank = getRegBankID(MI.getOperand(2).getReg(), MRI, *TRI);
    OpdsMapping[0] = AMDGPU::getValueMapping(AMDGPU::VCCRegBankID, 1);
    OpdsMapping[1] = nullptr; // Predicate Operand.
    OpdsMapping[2] = AMDGPU::getValueMapping(Op2Bank, Size);
    OpdsMapping[3] = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size);
    break;
  }
  case AMDGPU::G_STORE: {
    assert(MI.getOperand(0).isReg());
    unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    // FIXME: We need to specify a different reg bank once scalar stores
    // are supported.
    const ValueMapping *ValMapping =
        AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, Size);
    // FIXME: Depending on the type of store, the pointer could be in
    // the SGPR Reg bank.
    // FIXME: Pointer size should be based on the address space.
    const ValueMapping *PtrMapping =
        AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, 64);

    OpdsMapping[0] = ValMapping;
    OpdsMapping[1] = PtrMapping;
    break;
  }

  case AMDGPU::G_ICMP: {
    unsigned Size = MRI.getType(MI.getOperand(2).getReg()).getSizeInBits();
    unsigned Op2Bank = getRegBankID(MI.getOperand(2).getReg(), MRI, *TRI);
    unsigned Op3Bank = getRegBankID(MI.getOperand(3).getReg(), MRI, *TRI);
    unsigned Op0Bank = Op2Bank == AMDGPU::SGPRRegBankID &&
                       Op3Bank == AMDGPU::SGPRRegBankID ?
                       AMDGPU::SCCRegBankID : AMDGPU::VCCRegBankID;
    OpdsMapping[0] = AMDGPU::getValueMapping(Op0Bank, 1);
    OpdsMapping[1] = nullptr; // Predicate Operand.
    OpdsMapping[2] = AMDGPU::getValueMapping(Op2Bank, Size);
    OpdsMapping[3] = AMDGPU::getValueMapping(Op3Bank, Size);
    break;
  }


  case AMDGPU::G_EXTRACT_VECTOR_ELT: {
    unsigned IdxOp = 2;
    int64_t Imm;
    // XXX - Do we really need to fully handle these? The constant case should
    // be legalized away before RegBankSelect?

    unsigned OutputBankID = isSALUMapping(MI) && isConstant(MI.getOperand(IdxOp), Imm) ?
                            AMDGPU::SGPRRegBankID : AMDGPU::VGPRRegBankID;

    unsigned IdxBank = getRegBankID(MI.getOperand(2).getReg(), MRI, *TRI);
    OpdsMapping[0] = AMDGPU::getValueMapping(OutputBankID, MRI.getType(MI.getOperand(0).getReg()).getSizeInBits());
    OpdsMapping[1] = AMDGPU::getValueMapping(OutputBankID, MRI.getType(MI.getOperand(1).getReg()).getSizeInBits());

    // The index can be either if the source vector is VGPR.
    OpdsMapping[2] = AMDGPU::getValueMapping(IdxBank, MRI.getType(MI.getOperand(2).getReg()).getSizeInBits());
    break;
  }
  case AMDGPU::G_INSERT_VECTOR_ELT: {
    // XXX - Do we really need to fully handle these? The constant case should
    // be legalized away before RegBankSelect?

    int64_t Imm;

    unsigned IdxOp = MI.getOpcode() == AMDGPU::G_EXTRACT_VECTOR_ELT ? 2 : 3;
    unsigned BankID = isSALUMapping(MI) && isConstant(MI.getOperand(IdxOp), Imm) ?
                      AMDGPU::SGPRRegBankID : AMDGPU::VGPRRegBankID;



    // TODO: Can do SGPR indexing, which would obviate the need for the
    // isConstant check.
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      unsigned Size = getSizeInBits(MI.getOperand(i).getReg(), MRI, *TRI);
      OpdsMapping[i] = AMDGPU::getValueMapping(BankID, Size);
    }


    break;
  }
  case AMDGPU::G_UNMERGE_VALUES: {
    unsigned Bank = isSALUMapping(MI) ? AMDGPU::SGPRRegBankID :
      AMDGPU::VGPRRegBankID;

    // Op1 and Dst should use the same register bank.
    // FIXME: Shouldn't this be the default? Why do we need to handle this?
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      unsigned Size = getSizeInBits(MI.getOperand(i).getReg(), MRI, *TRI);
      OpdsMapping[i] = AMDGPU::getValueMapping(Bank, Size);
    }
    break;
  }
  case AMDGPU::G_INTRINSIC: {
    switch (MI.getOperand(1).getIntrinsicID()) {
    default:
      return getInvalidInstructionMapping();
    case Intrinsic::maxnum:
    case Intrinsic::minnum:
    case Intrinsic::amdgcn_cvt_pkrtz:
      return getDefaultMappingVOP(MI);
    case Intrinsic::amdgcn_kernarg_segment_ptr: {
      unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
      OpdsMapping[0] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size);
      break;
    }
    case Intrinsic::amdgcn_wqm_vote: {
      unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
      OpdsMapping[0] = OpdsMapping[2]
        = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, Size);
      break;
    }
    }
    break;
  }
  case AMDGPU::G_INTRINSIC_W_SIDE_EFFECTS: {
    switch (MI.getOperand(0).getIntrinsicID()) {
    default:
      return getInvalidInstructionMapping();
    case Intrinsic::amdgcn_exp_compr:
      OpdsMapping[0] = nullptr; // IntrinsicID
      // FIXME: These are immediate values which can't be read from registers.
      OpdsMapping[1] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 32);
      OpdsMapping[2] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 32);
      // FIXME: Could we support packed types here?
      OpdsMapping[3] = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, 32);
      OpdsMapping[4] = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, 32);
      // FIXME: These are immediate values which can't be read from registers.
      OpdsMapping[5] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 32);
      OpdsMapping[6] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 32);
      break;
    case Intrinsic::amdgcn_exp:
      OpdsMapping[0] = nullptr; // IntrinsicID
      // FIXME: These are immediate values which can't be read from registers.
      OpdsMapping[1] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 32);
      OpdsMapping[2] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 32);
      // FIXME: Could we support packed types here?
      OpdsMapping[3] = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, 32);
      OpdsMapping[4] = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, 32);
      OpdsMapping[5] = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, 32);
      OpdsMapping[6] = AMDGPU::getValueMapping(AMDGPU::VGPRRegBankID, 32);
      // FIXME: These are immediate values which can't be read from registers.
      OpdsMapping[7] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 32);
      OpdsMapping[8] = AMDGPU::getValueMapping(AMDGPU::SGPRRegBankID, 32);
      break;
    }
    break;
  }
  case AMDGPU::G_SELECT: {
    unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    unsigned Op1Bank = getRegBankID(MI.getOperand(1).getReg(), MRI, *TRI,
                                    AMDGPU::SGPRRegBankID);
    unsigned Op2Bank = getRegBankID(MI.getOperand(2).getReg(), MRI, *TRI);
    unsigned Op3Bank = getRegBankID(MI.getOperand(3).getReg(), MRI, *TRI);
    bool SGPRSrcs = Op1Bank == AMDGPU::SCCRegBankID &&
                    Op2Bank == AMDGPU::SGPRRegBankID &&
                    Op3Bank == AMDGPU::SGPRRegBankID;
    unsigned Bank = SGPRSrcs ? AMDGPU::SGPRRegBankID : AMDGPU::VGPRRegBankID;
    Op1Bank = SGPRSrcs ? AMDGPU::SCCRegBankID : AMDGPU::VCCRegBankID;

    if (Size == 64) {
      OpdsMapping[0] = AMDGPU::getValueMappingSGPR64Only(Bank, Size);
      OpdsMapping[1] = AMDGPU::getValueMapping(Op1Bank, 1);
      OpdsMapping[2] = AMDGPU::getValueMappingSGPR64Only(Bank, Size);
      OpdsMapping[3] = AMDGPU::getValueMappingSGPR64Only(Bank, Size);
    } else {
      OpdsMapping[0] = AMDGPU::getValueMapping(Bank, Size);
      OpdsMapping[1] = AMDGPU::getValueMapping(Op1Bank, 1);
      OpdsMapping[2] = AMDGPU::getValueMapping(Bank, Size);
      OpdsMapping[3] = AMDGPU::getValueMapping(Bank, Size);
    }

    break;
  }

  case AMDGPU::G_LOAD:
    return getInstrMappingForLoad(MI);

  case AMDGPU::G_ATOMICRMW_XCHG:
  case AMDGPU::G_ATOMICRMW_ADD:
  case AMDGPU::G_ATOMICRMW_SUB:
  case AMDGPU::G_ATOMICRMW_AND:
  case AMDGPU::G_ATOMICRMW_OR:
  case AMDGPU::G_ATOMICRMW_XOR:
  case AMDGPU::G_ATOMICRMW_MAX:
  case AMDGPU::G_ATOMICRMW_MIN:
  case AMDGPU::G_ATOMICRMW_UMAX:
  case AMDGPU::G_ATOMICRMW_UMIN:
  case AMDGPU::G_ATOMIC_CMPXCHG: {
    return getDefaultMappingAllVGPR(MI);
  }
  case AMDGPU::G_BRCOND: {
    unsigned Bank = getRegBankID(MI.getOperand(0).getReg(), MRI, *TRI,
                                 AMDGPU::SGPRRegBankID);
    assert(MRI.getType(MI.getOperand(0).getReg()).getSizeInBits() == 1);
    if (Bank != AMDGPU::SCCRegBankID)
      Bank = AMDGPU::VCCRegBankID;

    OpdsMapping[0] = AMDGPU::getValueMapping(Bank, 1);
    break;
  }
  }

  return getInstructionMapping(1, 1, getOperandsMapping(OpdsMapping),
                               MI.getNumOperands());
}

