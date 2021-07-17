// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <llvm/ADT/Triple.h>
#include <llvm/Option/ArgList.h>
#include <llvm/Support/Error.h>
#include <optional>

#include "linker.h"

class Prog;



/// Enumeration of optimisation levels.
enum class OptLevel {
  /// No optimisations.
  O0,
  /// Simple optimisations.
  O1,
  /// Aggressive optimisations.
  O2,
  /// Slow optimisations.
  O3,
  /// All optimisations.
  O4,
  /// Optimise for size.
  Os
};

/**
 * Helper to create a string error.
 */
llvm::Error MakeError(llvm::Twine msg);

/**
 * Helper class to drive linking.
 */
class Driver final {
public:
  /// Set up the driver.
  Driver(
      const llvm::Triple &triple,
      const llvm::Triple &base,
      llvm::opt::InputArgList &args
  );

  /// Cleanup.
  ~Driver();

  /// Run the linker.
  llvm::Error Link();

private:
  /// Helper to load an archive.
  llvm::Expected<Linker::Archive>
  LoadArchive(llvm::MemoryBufferRef buffer);

  /// Try to load an archive, if it is in the right format.
  llvm::Expected<std::optional<Linker::Archive>>
  TryLoadArchive(const std::string &path);

private:
  /// Enumeration of output formats.
  enum class OutputType {
    EXE,
    OBJ,
    ASM,
    LLIR,
    LLBC
  };
  /// Determine the output type.
  OutputType GetOutputType();

  /// Emit the output.
  llvm::Error Output(OutputType type, Prog &prog);
  // Run the optimiser on a binary.
  llvm::Error RunOpt(
      llvm::StringRef input,
      llvm::StringRef output,
      OutputType type
  );
  /// Run an external process.
  llvm::Error RunExecutable(
      llvm::StringRef exe,
      llvm::ArrayRef<llvm::StringRef> args
  );

private:
  /// Triple with the LLIR prefix.
  const llvm::Triple &llirTriple_;
  /// Triple without the LLIR prefix.
  const llvm::Triple &baseTriple_;

  /// List of arugments.
  llvm::opt::InputArgList &args_;

  /// Path to the output file.
  const std::string output_;
  /// Flag to indicate a shared library is to be built.
  bool shared_;
  /// Flag to indicate a static binary is to be built.
  bool static_;
  /// Flag to ban linking against shared libraries.
  bool noShared_;
  /// Flag to indicate a relocatable ELF object is to be built.
  bool relocatable_;
  /// Flag to indicate dynamic symbols are to be exported.
  bool exportDynamic_;
  /// Target CPU flag.
  std::string targetCPU_;
  /// Target ABI flag.
  std::string targetABI_;
  /// Target feature strings.
  std::string targetFS_;
  /// Entry point.
  std::string entry_;
  /// DT_RPATH
  std::string rpath_;
  /// Optimisation level.
  OptLevel optLevel_;
  /// Paths to libraries.
  std::vector<std::string> libraryPaths_;

  /// Buffers to retain in memory.
  std::vector<std::unique_ptr<llvm::MemoryBuffer>> buffers_;
  /// External files to link.
  std::vector<llvm::sys::fs::TempFile> tempFiles_;
  /// External libraries.
  std::vector<std::string> externLibs_;
  /// Forwarded arguments to the linker.
  llvm::opt::ArgStringList forwarded_;
};
