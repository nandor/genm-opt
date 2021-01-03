// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>
#include <llvm/Support/raw_ostream.h>

#include "passes/pre_eval/symbolic_pointer.h"



using APInt = llvm::APInt;
using APFloat = llvm::APFloat;



/**
 * Representation for a symbolic value.
 */
class SymbolicValue final {
public:
  enum class Kind {
    /// A integer of an unknown value.
    UNKNOWN_INTEGER,
    /// A undefined value.
    UNDEFINED,
    /// A specific integer.
    INTEGER,
    /// An unknown integer with a lower bound.
    LOWER_BOUNDED_INTEGER,
    /// A pointer or a range of pointers.
    POINTER,
    /// Value - unknown integer or pointer.
    VALUE,
  };

public:
  /// Copy constructor.
  SymbolicValue(const SymbolicValue &that);
  /// Cleanup.
  ~SymbolicValue();

  /// Copy assignment operator.
  SymbolicValue &operator=(const SymbolicValue &that);

  static SymbolicValue UnknownInteger();
  static SymbolicValue Undefined();
  static SymbolicValue Integer(const APInt &val);
  static SymbolicValue LowerBoundedInteger(const APInt &bound);
  static SymbolicValue Pointer(Func *func);
  static SymbolicValue Pointer(Global *symbol, int64_t offset);
  static SymbolicValue Pointer(unsigned frame, unsigned object, int64_t offset);
  static SymbolicValue Pointer(SymbolicPointer &&pointer);
  static SymbolicValue Pointer(const SymbolicPointer &pointer);
  static SymbolicValue Value(const SymbolicPointer &pointer);

  Kind GetKind() const { return kind_; }

  bool IsInteger() const { return GetKind() == Kind::INTEGER; }
  bool IsUnknownInteger() const { return GetKind() == Kind::UNKNOWN_INTEGER; }
  bool IsLowerBoundedInteger() const { return GetKind() == Kind::LOWER_BOUNDED_INTEGER; }
  bool IsPointer() const { return GetKind() == Kind::POINTER; }
  bool IsValue() const { return GetKind() == Kind::VALUE; }

  bool IsPointerLike() const { return IsPointer() || IsValue(); }
  bool IsIntegerLike() const { return IsInteger() || IsLowerBoundedInteger(); }

  APInt GetInteger() const { assert(IsIntegerLike()); return intVal_; }

  SymbolicPointer GetPointer() const
  {
    assert(IsPointerLike());
    return ptrVal_;
  }

  std::optional<APInt> AsInt() const
  {
    if (IsInteger()) {
      return std::optional<APInt>(GetInteger());
    } else {
      return std::nullopt;
    }
  }

  std::optional<SymbolicPointer> AsPointer() const
  {
    if (IsPointerLike()) {
      return std::optional<SymbolicPointer>(GetPointer());
    } else {
      return std::nullopt;
    }
  }

  /// Checks whether the value evaluates to true.
  bool IsTrue() const;
  /// Checks whether the value evaluates to false.
  bool IsFalse() const;

  /// Computes the least-upper-bound.
  SymbolicValue LUB(const SymbolicValue &that) const;

  /// Compares two values for equality.
  bool operator==(const SymbolicValue &that) const;
  bool operator!=(const SymbolicValue &that) const { return !(*this == that); }

  /// Dump the textual representation to a stream.
  void dump(llvm::raw_ostream &os) const;

private:
  /// Constructor which sets the kind.
  SymbolicValue(Kind kind) : kind_(kind) {}

  /// Cleanup.
  void Destroy();

private:
  /// Kind of the underlying value.
  Kind kind_;
  /// Union of all possible values.
  union {
    /// Value if kind is integer.
    APInt intVal_;
    /// Value if kind is pointer.
    SymbolicPointer ptrVal_;
  };
};

/// Print the value to a stream.
inline llvm::raw_ostream &operator<<(
    llvm::raw_ostream &os,
    const SymbolicValue &sym)
{
  sym.dump(os);
  return os;
}