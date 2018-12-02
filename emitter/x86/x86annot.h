// This file if part of the genm-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <llvm/Pass.h>

class Prog;
class X86ISel;



/**
 * X86 Annotation Handler.
 */
class X86Annot final : public llvm::ModulePass {
public:
  static char ID;

  /// Initialises the pass which prints data sections.
  X86Annot(
      const Prog *prog,
      const X86ISel *isel,
      llvm::MCContext *ctx,
      llvm::MCStreamer *os,
      const llvm::MCObjectFileInfo *objInfo
  );

private:
  /// Creates MachineFunctions from GenM IR.
  bool runOnModule(llvm::Module &M) override;
  /// Hardcoded name.
  llvm::StringRef getPassName() const override;
  /// Requires MachineModuleInfo.
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

private:
  /// Information about a call frame.
  struct FrameInfo {
    /// Label after a function call.
    llvm::MCSymbol *Label;
    /// Number of bytes allocated in the frame.
    unsigned FrameSize;
    /// Information about live offsets.
    std::vector<unsigned> Live;
  };

  /// @caml_call_frame
  void LowerCallFrame(const Inst *inst);
  /// @caml_raise_frame
  void LowerRaiseFrame(const Inst *inst);

  /// Lowers a frameinfo structure.
  void LowerFrame(const FrameInfo &frame);

private:
  /// Program being lowered.
  const Prog *prog_;
  /// Instruction selector pass containing info for annotations.
  const X86ISel *isel_;
  /// LLVM context.
  llvm::MCContext *ctx_;
  /// Streamer to emit output to.
  llvm::MCStreamer *os_;
  /// Object-file specific information.
  const llvm::MCObjectFileInfo *objInfo_;

  /// List of frames to emit information for.
  std::vector<FrameInfo> frames_;
};