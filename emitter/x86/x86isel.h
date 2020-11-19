// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <set>
#include <unordered_map>

#include <llvm/ADT/DenseMap.h>
#include <llvm/Analysis/OptimizationRemarkEmitter.h>
#include <llvm/CodeGen/MachineBasicBlock.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Pass.h>
#include <llvm/Target/X86/X86InstrInfo.h>
#include <llvm/Target/X86/X86ISelDAGToDAG.h>
#include <llvm/Target/X86/X86MachineFunctionInfo.h>
#include <llvm/Target/X86/X86RegisterInfo.h>
#include <llvm/Target/X86/X86Subtarget.h>
#include <llvm/Target/X86/X86TargetMachine.h>

#include "core/insts.h"
#include "emitter/isel.h"
#include "emitter/x86/x86call.h"

class Data;
class Func;
class Inst;
class Prog;



/**
 * Custom pass to generate MIR from LLIR instead of LLVM IR.
 */
class X86ISel final : public llvm::X86DAGMatcher, public ISel {
public:
  static char ID;

  X86ISel(
      llvm::X86TargetMachine *TM,
      llvm::X86Subtarget *STI,
      const llvm::X86InstrInfo *TII,
      const llvm::X86RegisterInfo *TRI,
      const llvm::TargetLowering *TLI,
      llvm::TargetLibraryInfo *LibInfo,
      const Prog &prog,
      llvm::CodeGenOpt::Level OL,
      bool shared
  );

private:
  /// Start lowering a function.
  void Lower(llvm::MachineFunction &mf) override
  {
    MF = &mf;
    FuncInfo_ = MF->getInfo<llvm::X86MachineFunctionInfo>();
  }

  /// Lowers a call target.
  llvm::SDValue LowerCallee(ConstRef<Inst> inst);

  /// Lowers a target-specific instruction.
  void LowerArch(const Inst *inst) override;

  /// Lowers a system call instruction.
  void LowerSyscall(const SyscallInst *inst) override;
  /// Lowers a process clone instruction.
  void LowerClone(const CloneInst *inst) override;
  /// Lowers a return.
  void LowerReturn(const ReturnInst *inst) override;
  /// Lowers an indirect jump.
  void LowerRaise(const RaiseInst *inst) override;
  /// Lowers a fixed register set instruction.
  void LowerSet(const SetInst *inst) override;

  /// Lowers an atomic exchange instruction.
  void LowerXchg(const X86_XchgInst *inst);
  /// Lowers a compare and exchange instruction.
  void LowerCmpXchg(const X86_CmpXchgInst *inst);
  /// Lowers a FnStCw instruction.
  void LowerFnClEx(const X86_FnClExInst *inst);
  /// Lowers an FPU control instruction.
  void LowerFPUControl(
      unsigned opcode,
      unsigned bytes,
      bool store,
      const Inst *inst
  );
  /// Lowers a RDTSC instruction.
  void LowerRdtsc(const X86_RdtscInst *inst);

  /// Lowers the arguments.
  void LowerArguments(bool hasVAStart) override;
  /// Lowers variable argument list frame setup.
  void LowerVASetup(const X86Call &lowering);

private:
  /// Reads the value from %fs:0
  SDValue LoadRegArch(ConstantReg::Kind reg) override;
  /// Lowers a write to RSP.
  void LowerSetSP(SDValue value);
  /// Lowers a raise construct.
  void LowerRaise(SDValue spVal, SDValue pcVal, SDValue glue);

private:
  /// Returns the X86 target machine.
  const llvm::X86TargetMachine &getTargetMachine() const override
  {
    return *TM_;
  }

  /// Returns the current DAG.
  llvm::SelectionDAG &GetDAG() override { return *CurDAG; }
  /// Returns the optimisation level.
  llvm::CodeGenOpt::Level GetOptLevel() override { return OptLevel; }

  /// Returns the instruction info object.
  const llvm::TargetInstrInfo &GetInstrInfo() override { return *TII; }
  /// Returns the target lowering.
  const llvm::TargetLowering &GetTargetLowering() override { return *TLI; }
  /// Creates a new scheduler.
  llvm::ScheduleDAGSDNodes *CreateScheduler() override;
  /// Returns the register info.
  const llvm::MCRegisterInfo &GetRegisterInfo() override { return *TRI_; }

  /// Target-specific DAG pre-processing.
  void PreprocessISelDAG() override
  {
    return X86DAGMatcher::PreprocessISelDAG();
  }
  /// Target-specific DAG post-processing.
  void PostprocessISelDAG() override
  {
    return X86DAGMatcher::PostprocessISelDAG();
  }

  /// Implementation of node selection.
  void Select(SDNode *node) override { return X86DAGMatcher::Select(node); }

  /// Returns the target-specific pointer type.
  llvm::MVT GetPtrTy() const override { return llvm::MVT::i64; }
  /// Returns the target-specific condition code type.
  llvm::MVT GetFlagTy() const override { return llvm::MVT::i8; }
  /// Returns the type of shift operands.
  llvm::MVT GetShiftTy() const override { return llvm::MVT::i8; }
  /// Returns the stack pointer.
  llvm::Register GetStackRegister() const override { return llvm::X86::RSP; }

private:
  /// Lowers a call instruction.
  void LowerCallSite(llvm::SDValue chain, const CallSite *call) override;

private:
  /// Target machine.
  const llvm::X86TargetMachine *TM_;
  /// Target register info.
  const llvm::X86RegisterInfo *TRI_;
  /// Machine function info of the current function.
  llvm::X86MachineFunctionInfo *FuncInfo_;
  /// Generate OCaml trampoline, if necessary.
  llvm::Function *trampoline_;
  /// Flag to indicate whether the target is a shared object.
  bool shared_;
};
