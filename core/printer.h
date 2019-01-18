// This file if part of the genm-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <ostream>
#include <unordered_map>

#include "core/inst.h"
#include "core/calling_conv.h"

class Prog;
class Func;
class Block;



/**
 * Prints a program.
 */
class Printer {
public:
  /// Initialises the printer.
  Printer(std::ostream &os) : os_(os) {}

  /// Prints a whole program.
  void Print(const Prog *prog);
  /// Prints a function.
  void Print(const Func *func);
  /// Prints a block.
  void Print(const Block *block);
  /// Prints an instruction.
  void Print(const Inst *inst);
  /// Prints a value.
  void Print(const Value *val);
  /// Prints an expression.
  void Print(const Expr *expr);
  /// Prints a type.
  void Print(Type type);
  /// Print a calling convention.
  void Print(CallingConv conv);

private:
  /// Output stream.
  std::ostream &os_;
  /// Instruction to identifier map.
  std::unordered_map<const Inst *, unsigned> insts_;
};
