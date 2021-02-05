// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <set>
#include <unordered_map>

#include <llvm/Support/WithColor.h>

class Prog;
class Func;
class Data;
class Xtor;
class Global;



/**
 * Helper class to link object files and executables.
 */
class Linker {
public:
  /// Initialise the linker.
  Linker(
      std::vector<std::unique_ptr<Prog>> &&objects,
      std::vector<std::unique_ptr<Prog>> &&archives,
      std::string_view output
  );

  /// Links a program.
  std::unique_ptr<Prog> Link();

private:
  /// Merge a module into the program.
  bool Merge(Prog &source);
  /// Merge a function.
  bool Merge(Func &func);
  /// Merge a data segment.
  bool Merge(Data &data);
  /// Merge a constructor/destructor.
  bool Merge(Xtor &xtor);
  /// Resolve a newly added name.
  void Resolve(Global &global);

private:
  /// Name of the program, for diagnostics.
  llvm::StringRef argv0_;
  /// List of object files to link.
  std::vector<std::unique_ptr<Prog>> objects_;
  /// List of archives to link.
  std::vector<std::unique_ptr<Prog>> archives_;
  /// Program to return.
  std::unique_ptr<Prog> prog_;
  /// Set of unresolved symbols.
  std::set<std::string> unresolved_;
};
