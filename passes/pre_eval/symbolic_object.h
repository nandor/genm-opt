// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <llvm/Support/Alignment.h>

#include "core/type.h"

#include "passes/pre_eval/symbolic_value.h"

class Atom;
class Prog;
class Object;
class SymbolicFrame;
class SymbolicValue;



/**
 * Object in the abstract heap.
 */
class SymbolicObject {
public:
  /// Constructs a symbolic object.
  SymbolicObject(llvm::Align align);
  /// Cleanup.
  virtual ~SymbolicObject();

protected:
  /// Stores to the object.
  bool WritePrecise(int64_t offset, const SymbolicValue &val, Type type);
  /// Loads from the object.
  SymbolicValue ReadPrecise(int64_t offset, Type type);
  /// Stores to the object without knowing the actual value.
  bool WriteImprecise(int64_t offset, const SymbolicValue &val, Type type);
  /// Stores to the object with a given mutator.
  bool Write(
      int64_t offset,
      const SymbolicValue &val,
      Type type,
      bool (SymbolicObject::*mutate)(unsigned, const SymbolicValue &)
  );

  /// Sets a value in a bucket.
  bool Set(unsigned bucket, const SymbolicValue &val);
  /// Unifies a value in a bucket.
  bool Merge(unsigned bucket, const SymbolicValue &val);

  /// Merges another object into this one.
  void LUB(const SymbolicObject &that);

protected:
  /// Base alignment of the object.
  llvm::Align align_;
  /// Set of pointer-sized buckets.
  std::vector<SymbolicValue> buckets_;
  /// Size of the modelled part.
  size_t size_;
};

/**
 * Object backing a global data item.
 */
class SymbolicDataObject final : public SymbolicObject {
public:
  using bucket_iterator = std::vector<SymbolicValue>::const_iterator;

public:
  /// Creates the symbolic representation of the object.
  SymbolicDataObject(Object &object);

  /// Performs a store to an atom inside the object.
  bool Store(Atom *a, int64_t offset, const SymbolicValue &val, Type type);
  /// Performs a load from an atom inside the object.
  SymbolicValue Load(Atom *a, int64_t offset, Type type);
  /// Clobbers the value at an exact location.
  bool StoreImprecise(Atom *a, int64_t offset, const SymbolicValue &val, Type type);
  /// Stores a value to an unknown location in the object.
  bool StoreImprecise(const SymbolicValue &val, Type type);

  /// Merges another object into this one.
  void LUB(const SymbolicDataObject &that)
  {
    SymbolicObject::LUB(that);
  }

  /// Iterator over buckets.
  bucket_iterator begin() const { return buckets_.begin(); }
  bucket_iterator end() const { return buckets_.end(); }

protected:
  /// Reference to the object represented here.
  Object &object_;
  /// Start bucket and offset into a bucket.
  std::unordered_map<Atom *, std::pair<unsigned, unsigned>> start_;
};

/**
 * Object backing a frame item.
 */
class SymbolicFrameObject final : public SymbolicObject {
public:
  using bucket_iterator = std::vector<SymbolicValue>::const_iterator;

public:
  SymbolicFrameObject(
      SymbolicFrame &frame,
      unsigned object,
      size_t size,
      llvm::Align align
  );

  SymbolicFrameObject(SymbolicFrame &frame, const SymbolicFrameObject &that);

  /// Performs a store to an atom inside the object.
  bool Store(int64_t offset, const SymbolicValue &val, Type type);
  /// Performs a load from an atom inside the object.
  SymbolicValue Load(int64_t offset, Type type);
  /// Clobbers the value at an exact location.
  bool StoreImprecise(int64_t offset, const SymbolicValue &val, Type type);
  /// Stores a value to an unknown location in the object.
  bool StoreImprecise(const SymbolicValue &val, Type type);
  /// Reads a value from all possible locations in the object.
  SymbolicValue LoadImprecise(Type type);

  /// Merges another object into this one.
  void LUB(const SymbolicFrameObject &that)
  {
    SymbolicObject::LUB(that);
  }

  /// Iterator over buckets.
  bucket_iterator begin() const { return buckets_.begin(); }
  bucket_iterator end() const { return buckets_.end(); }

private:
  /// Frame the object is part of.
  SymbolicFrame &frame_;
  /// ID of the object in the frame.
  unsigned object_;
};

/**
 * Dynamically-allocated heap object.
 */
class SymbolicHeapObject final : public SymbolicObject {
public:
  using bucket_iterator = std::vector<SymbolicValue>::const_iterator;

public:
  /// Creates a symbolic heap object.
  SymbolicHeapObject(CallSite &alloc, std::optional<size_t> size);

  /// Performs a store to an atom inside the object.
  bool Store(int64_t offset, const SymbolicValue &val, Type type);
  /// Performs a load from an atom inside the object.
  SymbolicValue Load(int64_t offset, Type type);
  /// Clobbers the value at an exact location.
  bool StoreImprecise(int64_t offset, const SymbolicValue &val, Type type);
  /// Stores a value to an unknown location in the object.
  bool StoreImprecise(const SymbolicValue &val, Type type);
  /// Reads a value from all possible locations in the object.
  SymbolicValue LoadImprecise(Type type);


  /// Iterator over buckets.
  bucket_iterator begin() const
  {
    return buckets_.end();
    //return bounded_ ? &*buckets_.begin() : &*approx_;
  }
  bucket_iterator end() const
  {
    return buckets_.end();
    //return bounded_ ? &*buckets_.end() : &*approx_ + 1;
  }

private:
  /// Set the approximate value.
  bool Merge(const SymbolicValue &value);

private:
  /// Originating allocating site.
  CallSite &alloc_;
  /// Flag to indicate if size is known.
  bool bounded_;
  /// Object aggregating all stored values.
  std::optional<SymbolicValue> approx_;
};