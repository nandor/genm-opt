// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <set>
#include <optional>

#include <llvm/Pass.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCObjectFileInfo.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/TargetSubtargetInfo.h>

#include "core/adt/hash.h"
#include "core/annot.h"

class Prog;
class MovInst;
class X86ISel;
class ISelMapping;



/**
 * Annotation emitter pass.
 *
 * Emits the metadata required by OCaml for garbage collection and stack traces.
 * For each call site, the label after the call (the return address) is mapped
 * to a descriptor, which in turn can link to debug information.
 *
 * The descriptor contains is composed of a flag, offsets and allocation sizes.
 * The flag contains the stack frame size, which must be a multiple of 8.
 * the 1st bit indicates whether the call allocates, while the 0th bit indicates
 * the presence of debug information.
 *
 * If the call allocates, the record encodes the sizes of all objects allocated
 * at that point.
 *
 * If debug information is present, a list of a single index to a debug node
 * is added to non-allocating calls, while allocating clals have a debug entry
 * for the individual allocations bundled into the call.
 */
class AnnotPrinter : public llvm::ModulePass {
public:
  /// Initialises the pass which prints data sections.
  AnnotPrinter(
      char &ID,
      llvm::MCContext *ctx,
      llvm::MCStreamer *os,
      const llvm::MCObjectFileInfo *objInfo,
      const llvm::DataLayout &layout,
      const ISelMapping &mapping,
      bool shared
  );

protected:
  /// Returns the GC index of a register.
  virtual std::optional<unsigned> GetRegisterIndex(llvm::Register reg) = 0;
  /// Returns the name of a register.
  virtual llvm::StringRef GetRegisterName(unsigned reg) = 0;
  /// Returns the stack pointer of the target.
  virtual llvm::Register GetStackPointer() = 0;
  /// Returns the implicit stack size, besides the frame adjustment.
  virtual unsigned GetImplicitStackSize() const = 0;

  /// Return the offset to apply to a label.
  virtual int64_t GetFrameOffset(const llvm::MachineInstr &MI) const
  {
    return 0;
  }

private:
  /// Creates MachineFunctions from LLIR IR.
  bool runOnModule(llvm::Module &M) override;
  /// Requires MachineModuleInfo.
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

private:
  /// Information about a call frame.
  struct FrameInfo {
    /// Label after a function call.
    llvm::MCSymbol *Label;
    /// Offset from the symbol.
    int64_t Offset;
    /// Number of bytes allocated in the frame.
    int16_t FrameSize;
    /// Information about live offsets.
    std::set<uint16_t> Live;
    /// Allocation sizes.
    std::vector<size_t> Allocs;
    /// Debug info symbols.
    std::vector<llvm::MCSymbol *> Debug;
  };

  /// Information about a root frame.
  struct RootInfo {
    /// Label after a function call.
    llvm::MCSymbol *Label;
    /// Offset from the symbol.
    int64_t Offset;

    RootInfo(llvm::MCSymbol *label, int64_t offset)
        : Label(label), Offset(offset)
    {
    }
  };

  /// Debug information key.
  struct DebugKey {
    /// Bundle of debug infos.
    const CamlFrame::DebugInfos &Debug;

    bool operator==(const DebugKey &that) const {
      return Debug == that.Debug;
    }
  };

  /// Debug information hasher.
  struct DebugKeyHash {
    size_t operator() (const DebugKey &key) const {
      size_t hash = 0;
      for (const auto &debug : key.Debug) {
        ::hash_combine(hash, std::hash<int64_t>{}(debug.Location));
        ::hash_combine(hash, std::hash<std::string>{}(debug.File));
        ::hash_combine(hash, std::hash<std::string>{}(debug.Definition));
      }
      return hash;
    }
  };

  /// Debug value.
  struct DebugInfo {
    llvm::MCSymbol *Definition;
    int64_t Location;
  };

  /// Debug value group.
  struct DebugInfos {
    llvm::MCSymbol *Symbol;
    std::vector<DebugInfo> Debug;
  };

  /// Definition.
  struct DefinitionInfo {
    llvm::MCSymbol *Symbol;
    llvm::MCSymbol *File;
    std::string Definition;
  };

  /// Lowers a frameinfo structure.
  void LowerFrame(const FrameInfo &frame);
  /// Lowers a symbol name.
  llvm::MCSymbol *LowerSymbol(const std::string_view name);
  /// Records a debug info object.
  llvm::MCSymbol *RecordDebug(const CamlFrame::DebugInfos &debug);
  /// Record a definition.
  llvm::MCSymbol *RecordDefinition(
      const std::string &file,
      const std::string &def
  );
  /// Record a file name.
  llvm::MCSymbol *RecordFile(const std::string &file);
  /// Emits a value which is relative to the current address.
  void EmitDiff(llvm::MCSymbol *symbol, unsigned size = 4);
  /// Emits a symbol with an offset.
  void EmitOffset(llvm::MCSymbol *symbol, int64_t off);

protected:
  /// Instruction selector pass containing info for annotations.
  const ISelMapping &mapping_;
  /// LLVM context.
  llvm::MCContext *ctx_;
  /// Streamer to emit output to.
  llvm::MCStreamer *os_;
  /// Object-file specific information.
  const llvm::MCObjectFileInfo *objInfo_;
  /// Data layout.
  const llvm::DataLayout layout_;
  /// List of frames to emit information for.
  std::vector<FrameInfo> frames_;
  /// List of root frames.
  std::vector<RootInfo> roots_;
  /// Mapping of debug objects.
  std::unordered_map<DebugKey, DebugInfos, DebugKeyHash> debug_;
  /// Mapping from definitions to labels.
  std::unordered_map<
    std::pair<std::string, std::string>,
    DefinitionInfo
  > defs_;
  /// Mapping from file names to labels.
  std::unordered_map<std::string, llvm::MCSymbol *> files_;
  /// Flag to indicate whether a shared library is emitted.
  bool shared_;
};
