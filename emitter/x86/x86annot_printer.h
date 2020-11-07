// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include "emitter/annot_printer.h"

/**
 * Annotation emitter pass.
 */
class X86AnnotPrinter final : public AnnotPrinter {
public:
  static char ID;

  /// Initialises the pass which prints data sections.
  X86AnnotPrinter(
      llvm::MCContext *ctx,
      llvm::MCStreamer *os,
      const llvm::MCObjectFileInfo *objInfo,
      const llvm::DataLayout &layout,
      const ISelMapping &mapping,
      bool shared
  );

  /// Cleanup.
  ~X86AnnotPrinter();

  /// Hardcoded name.
  llvm::StringRef getPassName() const override;

private:
  /// Returns the GC index of a register.
  std::optional<unsigned> GetRegisterIndex(llvm::Register reg) override;
  /// Returns the name of a register.
  llvm::StringRef GetRegisterName(unsigned reg) override;
  /// Returns the stack pointer.
  llvm::Register GetStackPointer() override;
  /// 8 bytes are left on stack when return address is pushed.
  unsigned GetImplicitStackSize() const override { return 8; }
};
