// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <set>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <variant>

#include <llvm/Support/raw_ostream.h>

#include "core/adt/id.h"
#include "core/adt/hash.h"
#include "core/adt/bitset.h"

class Block;
class CallSite;
class Extern;
class Func;
class Atom;
class SymbolicObject;
class SymbolicFrame;



/**
 * Symbolic address wrapper, used to iterate across pointer contents.
 */
class SymbolicAddress final {
public:
  /// Enumeration of address kinds.
  enum class Kind : uint8_t {
    OBJECT,
    OBJECT_RANGE,
    EXTERN,
    EXTERN_RANGE,
    FUNC,
    BLOCK,
    STACK,
  };

  /// Exact object address.
  class AddrObject {
  public:
    /// Kind of the symbol.
    Kind K;
    /// Object symbol.
    ID<SymbolicObject> Object;
    /// Offset into the symbol.
    int64_t Offset;

  private:
    friend class SymbolicAddress;

    AddrObject(ID<SymbolicObject> object, int64_t offset)
        : K(Kind::OBJECT), Object(object), Offset(offset)
    {
    }
  };

  /// Range of an entire object.
  class AddrObjectRange {
  public:
    /// Kind of the symbol.
    Kind K;
    /// Object symbol.
    ID<SymbolicObject> Object;

  private:
    friend class SymbolicAddress;

    AddrObjectRange(ID<SymbolicObject> object)
        : K(Kind::OBJECT_RANGE), Object(object)
    {
    }
  };

  /// Exact external address.
  class AddrExtern {
  public:
    /// Kind of the symbol.
    Kind K;
    /// Extern symbol.
    Extern *Symbol;
    /// Offset into the symbol.
    int64_t Offset;

  private:
    friend class SymbolicAddress;

    AddrExtern(Extern *symbol, int64_t offset)
        : K(Kind::EXTERN), Symbol(symbol), Offset(offset)
    {
    }
  };

  /// Range of an entire object.
  class AddrExternRange {
  public:
    /// Kind of the symbol.
    Kind K;
    /// Extern symbol.
    Extern *Symbol;

  private:
    friend class SymbolicAddress;

    AddrExternRange(Extern *symbol)
        : K(Kind::EXTERN_RANGE), Symbol(symbol)
    {
    }
  };

  /// Pointer to a function.
  class AddrFunc {
  public:
    /// Kind of the symbol.
    Kind K;
    /// Pointer to the function.
    ID<Func> F;

  private:
    friend class SymbolicAddress;

    AddrFunc(ID<Func> func) : K(Kind::FUNC), F(func) { }
  };

  /// Pointer to a block.
  class AddrBlock {
  public:
    /// Kind of the symbol.
    Kind K;
    /// Pointer to the function.
    Block *B;

  private:
    friend class SymbolicAddress;

    AddrBlock(Block *block) : K(Kind::BLOCK), B(block) { }
  };

  /// Pointer to a stack frame.
  class AddrStack {
  public:
    /// Kind of the symbol.
    Kind K;
    /// Index of the frame.
    ID<SymbolicFrame> Frame;

  private:
    friend class SymbolicAddress;

    AddrStack(ID<SymbolicFrame> frame) : K(Kind::STACK), Frame(frame) { }
  };

public:
  /// Construct an address to a specific location.
  SymbolicAddress(
      const std::pair
        < std::unordered_map<int64_t, BitSet<SymbolicObject>>::const_iterator
        , BitSet<SymbolicObject>::iterator> &arg)
      : v_(*arg.second, arg.first->first)
  {
  }

  /// Construct an address to a specific location.
  SymbolicAddress(BitSet<SymbolicObject>::iterator arg)
    : v_(*arg)
  {
  }
  /// Construct an address to a specific location.
  SymbolicAddress(std::unordered_map<Extern *, int64_t>::const_iterator arg)
    : v_(arg->first, arg->second)
  {
  }
  /// Construct an address to a specific location.
  SymbolicAddress(std::unordered_set<Extern *>::const_iterator arg)
    : v_(*arg)
  {
  }
  /// Constructs an address to a frame object.
  SymbolicAddress(BitSet<Func>::iterator func)
    : v_(*func)
  {
  }
  /// Constructs an address to a block.
  SymbolicAddress(std::unordered_set<Block *>::const_iterator block)
    : v_(*block)
  {
  }
  /// Constructs an address to a stack frame.
  SymbolicAddress(BitSet<SymbolicFrame>::iterator stack)
    : v_(*stack)
  {
  }

  /// Returns the address kind.
  Kind GetKind() const { return v_.K; }

  /// Access the actual pointer kind.
  const AddrObject &AsObject() const { return v_.O; }
  const AddrObjectRange &AsObjectRange() const { return v_.OR; }
  const AddrExtern &AsExtern() const { return v_.E; }
  const AddrExternRange &AsExternRange() const { return v_.ER; }
  const AddrFunc &AsFunc() const { return v_.F; }
  const AddrBlock &AsBlock() const { return v_.B; }
  const AddrStack &AsStack() const { return v_.S; }

  /// Checks whether the pointer is precise.
  bool IsPrecise() const;

  /// Attempt to convert to a global.
  const AddrObject *ToObject() const
  {
    return v_.K == Kind::OBJECT ? &v_.O : nullptr;
  }

  /// Attempt to convert to a global.
  const AddrObjectRange *ToObjectRange() const
  {
    return v_.K == Kind::OBJECT_RANGE ? &v_.OR : nullptr;
  }

  /// Compares two sets of pointers for equality.
  bool operator==(const SymbolicAddress &that) const;

  /// Prints the address.
  void dump(llvm::raw_ostream &os) const;

private:
  /// Base symbol.
  union P {
    Kind K;
    AddrObject O;
    AddrObjectRange OR;
    AddrExtern E;
    AddrExternRange ER;
    AddrFunc F;
    AddrBlock B;
    AddrStack S;

    P(ID<SymbolicObject> object, int64_t offset) : O(object, offset) { }
    P(ID<SymbolicObject> object) : OR(object) { }

    P(Extern *symbol, int64_t off) : E(symbol, off) { }
    P(Extern *symbol) : ER(symbol) { }

    P(ID<Func> func) : F(func) { }
    P(Block *block) : B(block) { }
    P(ID<SymbolicFrame> stack) : S(stack) { }
  } v_;
};

/// Print the pointer to a stream.
inline llvm::raw_ostream &operator<<(
    llvm::raw_ostream &os,
    const SymbolicAddress &address)
{
  address.dump(os);
  return os;
}


/**
 * An address or a range of addresses.
 */
class SymbolicPointer final {
public:
  using Ref = std::shared_ptr<SymbolicPointer>;

  using ObjectMap = std::unordered_map<int64_t, BitSet<SymbolicObject>>;
  using ObjectRangeMap = BitSet<SymbolicObject>;
  using ExternMap = std::unordered_map<Extern *, int64_t>;
  using ExternRangeMap = std::unordered_set<Extern *>;
  using FuncMap = BitSet<Func>;
  using BlockMap = std::unordered_set<Block *>;
  using StackMap = BitSet<SymbolicFrame>;

  class address_iterator : public std::iterator
    < std::forward_iterator_tag
    , SymbolicAddress
    >
  {
  public:
    address_iterator() : pointer_(nullptr) {}

    template <typename It>
    address_iterator(It it, const SymbolicPointer *pointer)
      : pointer_(pointer)
      , it_(it)
      , current_(SymbolicAddress(it))
    {
    }

    bool operator==(const address_iterator &that) const
    {
      return current_ == that.current_;
    }
    bool operator!=(const address_iterator &that) const
    {
      return !(*this == that);
    }

    address_iterator &operator++();
    address_iterator operator++(int)
    {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    const SymbolicAddress &operator*() const { return *current_; }
    const SymbolicAddress *operator->() const { return &operator*(); }

  private:
    /// Reference to the parent pointer.
    const SymbolicPointer *pointer_;
    /// On-the-fly address.
    std::optional<SymbolicAddress> current_;
    /// Current iterator.
    std::optional<std::variant<
        std::pair<ObjectMap::const_iterator, BitSet<SymbolicObject>::iterator>,
        ObjectRangeMap::iterator,
        ExternMap::const_iterator,
        ExternRangeMap::const_iterator,
        FuncMap::iterator,
        BlockMap::const_iterator,
        StackMap::iterator
    >> it_;
  };

  using func_iterator = FuncMap::iterator;
  using block_iterator = BlockMap::const_iterator;
  using stack_iterator = StackMap::iterator;

public:
  SymbolicPointer();
  SymbolicPointer(ID<SymbolicObject> object, int64_t offset);
  SymbolicPointer(Extern *object, int64_t offset);
  SymbolicPointer(ID<Func> func);
  SymbolicPointer(Block *block);
  SymbolicPointer(ID<SymbolicFrame> frame);
  ~SymbolicPointer();

  /// Compares two sets of pointers for equality.
  bool operator==(const SymbolicPointer &that) const;

  /// Add a global to the pointer.
  void Add(ID<SymbolicObject> object) { objectRanges_.Insert(object); }
  /// Add a global to the pointer.
  void Add(ID<SymbolicObject> object, int64_t offset);
  /// Adds an extern to the pointer.
  void Add(Extern *e, int64_t offset);
  /// Adds an extern to the pointer.
  void Add(Extern *e) { externRanges_.insert(e); }
  /// Add a function to the pointer.
  void Add(ID<Func> f) { funcPointers_.Insert(f); }
  /// Add a range of functions to the pointer.
  void Add(const BitSet<Func> &funcs) { funcPointers_.Union(funcs); }
  /// Adds a block to the pointer.
  void Add(Block *b) { blockPointers_.insert(b); }
  /// Adds a stack frame to the pointer.
  void Add(ID<SymbolicFrame> frame) { stackPointers_.Insert(frame); }
  /// Add a range of stack pointers.
  void Add(const BitSet<SymbolicFrame> &frames) { stackPointers_.Union(frames); }
  /// Add a range of pointers.
  void Add(const BitSet<SymbolicObject> &range) { objectRanges_.Union(range); }

  /// Offset the pointer.
  Ref Offset(int64_t offset) const;
  /// Decays the pointer to ranges.
  Ref Decay() const;

  /// Computes the least-upper-bound in place.
  void Merge(const Ref &that);

  /// Computes the least-upper-bound.
  [[nodiscard]] Ref LUB(const Ref &that) const
  {
    Ref lub = std::make_shared<SymbolicPointer>(*this);
    lub->Merge(that);
    return lub;
  }

  /// Dump the textual representation to a stream.
  void dump(llvm::raw_ostream &os) const;

  /// Checks whether the pointer points to anything.
  bool empty() const { return begin() == end(); }
  /// Iterator over pointers contained in the set.
  address_iterator begin() const;
  address_iterator end() const { return address_iterator(); }

  /// Iterator over functions.
  size_t func_size() const { return funcPointers_.Size(); }
  func_iterator func_begin() const { return funcPointers_.begin(); }
  func_iterator func_end() const { return funcPointers_.end(); }
  llvm::iterator_range<func_iterator> funcs() const
  {
    return llvm::make_range(func_begin(), func_end());
  }

  /// Iterator over blocks.
  size_t block_size() const { return std::distance(block_begin(), block_end()); }
  block_iterator block_begin() const { return blockPointers_.begin(); }
  block_iterator block_end() const { return blockPointers_.end(); }
  llvm::iterator_range<block_iterator> blocks() const
  {
    return llvm::make_range(block_begin(), block_end());
  }

  /// Iterator over stacks.
  size_t stack_size() const { return stackPointers_.Size(); }
  stack_iterator stack_begin() const { return stackPointers_.begin(); }
  stack_iterator stack_end() const { return stackPointers_.end(); }
  llvm::iterator_range<stack_iterator> stacks() const
  {
    return llvm::make_range(stack_begin(), stack_end());
  }

private:
  friend class address_iterator;
  /// Set of direct object pointers.
  ObjectMap objectPointers_;
  /// Set of imprecise object ranges.
  ObjectRangeMap objectRanges_;
  /// Set of precise external pointers.
  ExternMap externPointers_;
  /// Set of external pointer ranges.
  ExternRangeMap externRanges_;
  /// Set of functions.
  FuncMap funcPointers_;
  /// Set of blocks.
  BlockMap blockPointers_;
  /// Set of stack frames.
  StackMap stackPointers_;
};

/// Print the pointer to a stream.
inline llvm::raw_ostream &operator<<(
    llvm::raw_ostream &os,
    const SymbolicPointer &sym)
{
  sym.dump(os);
  return os;
}
