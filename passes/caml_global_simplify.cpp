// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#include <set>
#include <queue>
#include <unordered_set>
#include <unordered_map>

#include <llvm/ADT/SCCIterator.h>
#include <llvm/ADT/Statistic.h>
#include <llvm/Support/Debug.h>

#include "core/block.h"
#include "core/cast.h"
#include "core/data.h"
#include "core/prog.h"
#include "core/insts.h"
#include "core/pass_manager.h"
#include "core/analysis/init_path.h"
#include "core/analysis/object_graph.h"
#include "passes/caml_global_simplify.h"

#define DEBUG_TYPE "caml-global-simplify"

STATISTIC(NumReferencesRemoved, "References removed");
STATISTIC(NumLoadsFolded, "Loads folded");
STATISTIC(NumAllocsRemoved, "Allocations eliminated");


// -----------------------------------------------------------------------------
static std::optional<int64_t> GetConstant(Ref<Inst> inst)
{
  if (auto movInst = ::cast_or_null<MovInst>(inst)) {
    if (auto movValue = ::cast_or_null<ConstantInt>(movInst->GetArg())) {
      if (movValue->GetValue().getMinSignedBits() <= 64) {
        return movValue->GetInt();
      }
    }
  }
  return {};
}

// -----------------------------------------------------------------------------
class CamlGlobalSimplifier final {
public:
  /// Set up the transformation.
  CamlGlobalSimplifier(Prog &prog, Object *root, Func *entry)
    : prog_(prog)
    , root_(root)
    , init_(prog, entry)
    , changed_(false)
  {
  }

  /// Run the simplifier.
  bool Simplify()
  {
    SimplifyObjects();
    SimplifyAllocs();
    return changed_;
  }

private:
  /// Simplify global objects.
  void SimplifyObjects();
  /// Move allocations to the constant section.
  void SimplifyAllocs();

  /// Escape the read or written fields of an object.
  void Escape(Object &obj);
  /// Recursively simplify objects starting at caml_globals.
  void Simplify(Object &object);

  /// Replace a caml_alloc* allocation.
  void ReplaceAlloc(CallInst *call, unsigned n); 
  /// Replace a caml_allocN allocation.
  void ReplaceAllocN(CallInst *call, Ref<Inst> young, unsigned n);

  using StoreMap = std::map<int64_t, std::set<MemoryStoreInst *>>;
  using LoadMap = std::map<int64_t, std::set<MemoryLoadInst *>>;

private:
  /// Reference to the underlying program.
  Prog &prog_;
  /// Reference to the root object.
  Object *root_;
  /// Set of nodes on the entry path.
  InitPath init_;
  /// Set of objects which are indirectly referenced.
  std::unordered_set<Object *> escapes_;
  /// Flag to indicate that the object cannot be eliminated.
  std::unordered_map<Object *, bool> pinned_;
  /// Flag to indicate whether the program changed.
  bool changed_;
  /// Stores which anchor a heap object.
  std::unordered_set<Inst *> anchor_;
};

// -----------------------------------------------------------------------------
enum class EscapeKind {
  NO_ESCAPE,
  ESCAPE,
  LOAD_ONLY,
  STORE_ONLY,
  LOAD_STORE
};

// -----------------------------------------------------------------------------
static EscapeKind Escapes(Object &obj)
{
  std::set<std::pair<MovInst *, int64_t>> instUsers;
  for (Atom &atom : obj) {
    if (!atom.IsLocal()) {
      return EscapeKind::ESCAPE;
    }
    for (User *user : atom.users()) {
      switch (user->GetKind()) {
        case Value::Kind::INST: {
          auto *inst = ::cast<MovInst>(user);
          instUsers.emplace(inst, 0);
          continue;
        }
        case Value::Kind::EXPR: {
          switch (static_cast<Expr *>(user)->GetKind()) {
            case Expr::Kind::SYMBOL_OFFSET: {
              auto *expr = static_cast<SymbolOffsetExpr *>(user);
              for (auto *exprUser : expr->users()) {
                if (auto *inst = ::cast_or_null<MovInst>(exprUser)) {
                  instUsers.emplace(inst, expr->GetOffset());
                }
              }
              continue;
            }
          }
          llvm_unreachable("invalid expression kind");
        }
        case Value::Kind::GLOBAL:
        case Value::Kind::CONST: {
          continue;
        }
      }
      llvm_unreachable("invalid value kind");
    }
  }

  unsigned numLoads = 0, numStores = 0;
  for (auto [inst, offset] : instUsers) {
    for (User *user : inst->users()) {
      if (auto *store = ::cast_or_null<MemoryStoreInst>(user)) {
        if (store->GetValue() == inst->GetSubValue(0)) {
          return EscapeKind::ESCAPE;
        }
        ++numStores;
        continue;
      }
      if (auto *load = ::cast_or_null<MemoryLoadInst>(user)) {
        ++numLoads;
        continue;
      }
      return EscapeKind::ESCAPE;
    }
  }
  if (numLoads && numStores) {
    return EscapeKind::LOAD_STORE;
  } else if (numLoads) {
    return EscapeKind::LOAD_ONLY;
  } else if (numStores) {
    return EscapeKind::STORE_ONLY;
  } else {
    return EscapeKind::NO_ESCAPE;
  }
}

// -----------------------------------------------------------------------------
void CamlGlobalSimplifier::SimplifyObjects()
{
  // Find individual objects which escape: they are either referenced from
  // outside of the OCaml data section or pointers to them escape in code,
  // meaning they are used as anything but addresses to loads or stores.
  for (Data &data : prog_.data()) {
    for (Object &obj : data) {
      if (&obj == root_) {
        continue;
      }

      if (root_->getParent() == &data) {
        switch (Escapes(obj)) {
          case EscapeKind::ESCAPE: {
            // Pointers to this object escape - record it.
            escapes_.insert(&obj);
            continue;
          }
          case EscapeKind::LOAD_STORE:
          case EscapeKind::LOAD_ONLY: {
            // Object does not escape, but pointees do.
            Escape(obj);
            continue;
          }
          case EscapeKind::STORE_ONLY:
          case EscapeKind::NO_ESCAPE: {
            // If the object is store-only, the only noticeable  side effect
            // is whether the pointers stored into them are live or not.
            continue;
          }
        }
        llvm_unreachable("invalid escape kind");
      } else {
        for (Atom &atom : obj) {
          for (Item &item : atom) {
            if (auto *expr = item.AsExpr()) {
              switch (expr->GetKind()) {
                case Expr::Kind::SYMBOL_OFFSET: {
                  auto g = static_cast<SymbolOffsetExpr *>(expr)->GetSymbol();
                  if (auto *ref = ::cast_or_null<Atom>(g)) {
                    if (ref->getParent()->getParent() == root_->getParent()) {
                      escapes_.insert(ref->getParent());
                    }
                  }
                  continue;
                }
              }
              llvm_unreachable("invalid expression kind");
            }
          }
        }
      }
    }
  }

  // Propagate referenced information to all objects reachable from the
  // referenced set. Since these objects escape, they cannot be simplified.
  std::queue<Object *> q;
  for (Object *obj : escapes_) {
    q.push(obj);
  }

  while (!q.empty()) {
    Object &obj = *q.front();
    q.pop();

    for (Atom &atom : obj) {
      for (Item &item : atom) {
        if (auto *expr = item.AsExpr()) {
          switch (expr->GetKind()) {
            case Expr::Kind::SYMBOL_OFFSET: {
              auto g = static_cast<SymbolOffsetExpr *>(expr)->GetSymbol();
              if (auto *refAtom = ::cast_or_null<Atom>(g)) {
                auto *refObj = refAtom->getParent();
                if (escapes_.insert(refObj).second) {
                  q.push(refObj);
                }
              }
              continue;
            }
          }
          llvm_unreachable("invalid expression kind");
        }
      }
    }
  }

  ObjectGraph og(prog_);
  for (auto it = llvm::scc_begin(&og); !it.isAtEnd(); ++it) {
    if (it->size() == 1) {
      auto *obj = (*it)[0]->GetObject();
      if (!obj) {
        continue;
      }
      if (obj == root_ || obj->getParent() != root_->getParent()) {
        continue;
      }
      if (escapes_.count(obj)) {
        pinned_.emplace(obj, true);
      } else {
        Simplify(*obj);
      }
    } else {
      llvm_unreachable("not implemented");
    }
  }
}

// -----------------------------------------------------------------------------
void CamlGlobalSimplifier::Escape(Object &object)
{
  // Find the loads and stores using the object.
  std::set<std::pair<int64_t, int64_t>> offsets;
  {
    std::set<std::pair<MovInst *, int64_t>> instUsers;
    for (Atom &atom : object) {
      for (User *user : atom.users()) {
        if (auto *inst = ::cast_or_null<MovInst>(user)) {
          instUsers.emplace(inst, 0);
          continue;
        }
        if (auto *expr = ::cast_or_null<SymbolOffsetExpr>(user)) {
          for (auto *exprUser : expr->users()) {
            if (auto *inst = ::cast_or_null<MovInst>(exprUser)) {
              instUsers.emplace(inst, expr->GetOffset());
            }
          }
          continue;
        }
      }
    }

    for (auto [inst, offset] : instUsers) {
      for (User *user : inst->users()) {
        if (auto *store = ::cast_or_null<MemoryStoreInst>(user)) {
          auto value = store->GetValue();
          assert(value != inst->GetSubValue(0) && "pointer escapes");
          offsets.emplace(offset, offset + GetSize(value.GetType()));
          continue;
        }
        if (auto *load = ::cast_or_null<MemoryLoadInst>(user)) {
          offsets.emplace(offset, offset + GetSize(load->GetType()));
          continue;
        }
        llvm_unreachable("pointer escapes");
      }
    }
  }

  std::optional<int64_t> offset = 0;
  for (Atom &atom : object) {
    for (auto it = atom.begin(); it != atom.end(); ) {
      Item &item = *it++;

      // Check whether the item overlaps with any loads/stores.
      bool reachable = false;
      if (offset) {
        int64_t start = *offset;
        int64_t end = start + item.GetSize();
        offset = end;
        for (auto [accStart, accEnd] : offsets) {
          if (end <= accStart || accEnd <= start) {
            continue;
          }
          reachable = true;
          break;
        }
      }
      if (offset && !reachable) {
        continue;
      }

      // Skip non-atoms.
      auto *expr = ::cast_or_null<SymbolOffsetExpr>(item.AsExpr());
      if (!expr) {
        continue;
      }
      auto *refAtom = ::cast_or_null<Atom>(expr->GetSymbol());
      if (!refAtom) {
        continue;
      }
      auto *refObj = refAtom->getParent();
      if (refObj->getParent() == root_->getParent()) {
        escapes_.insert(refObj);
      }
    }
    // TODO: align according to the alignment of the next object.
    offset = std::nullopt;
  }
}

// -----------------------------------------------------------------------------
void CamlGlobalSimplifier::Simplify(Object &object)
{
  bool pinned = false;

  // Find the loads and stores using the object.
  StoreMap stores;
  LoadMap loads;
  {
    std::set<std::pair<MovInst *, int64_t>> instUsers;
    for (Atom &atom : object) {
      for (User *user : atom.users()) {
        if (auto *inst = ::cast_or_null<MovInst>(user)) {
          instUsers.emplace(inst, 0);
          continue;
        }
        if (auto *expr = ::cast_or_null<SymbolOffsetExpr>(user)) {
          for (auto *exprUser : expr->users()) {
            if (auto *inst = ::cast_or_null<MovInst>(exprUser)) {
              instUsers.emplace(inst, expr->GetOffset());
            }
          }
          continue;
        }
      }
    }

    for (auto [inst, offset] : instUsers) {
      for (User *user : inst->users()) {
        if (auto *store = ::cast_or_null<MemoryStoreInst>(user)) {
          assert(store->GetValue() != inst->GetSubValue(0) && "pointer escapes");
          stores[offset].insert(store);
          continue;
        }
        if (auto *load = ::cast_or_null<MemoryLoadInst>(user)) {
          loads[offset].insert(load);
          continue;
        }
        llvm_unreachable("pointer escapes");
      }
    }
  }

  if (!stores.empty() && !loads.empty()) {
    pinned = true;
    for (auto &[off, insts] : stores) {
      if (insts.size() == 1) {
        for (auto *inst : insts) {
          anchor_.insert(inst);
        }
      }
    }
  } else if (!loads.empty()) {
    // If there are no stores, propagate all loads.
    for (auto it = loads.begin(); it != loads.end(); ) {
      auto &[off, insts] = *it;
      for (auto jt = insts.begin(); jt != insts.end(); ) {
        MemoryLoadInst *inst = *jt;
        auto ty = inst->GetType();
        if (auto *v = object.Load(off, ty)) {
          auto *mov = new MovInst(ty, v, inst->GetAnnots());
          inst->getParent()->AddInst(mov, inst);
          inst->replaceAllUsesWith(mov);
          inst->eraseFromParent();
          NumLoadsFolded++;
          changed_ = true;
          insts.erase(jt++);
        } else {
          ++jt;
        }
      }
      if (insts.empty()) {
        loads.erase(it++);
      } else {
        ++it;
      }
    }
    pinned = !loads.empty() || pinned;
  } else if (!stores.empty()) {
    pinned = true;
  }

  // Collect the offsets touched by loads and stores.
  std::set<std::pair<int64_t, int64_t>> offsets, stored, loaded;
  bool overlap = false;
  {
    // Find the offsets where values are stored to and loaded from.
    // Bail out if there is any overlap or size mismatch between fields.
    for (auto &[start, insts] : stores) {
      for (auto *store : insts) {
        auto end = start + GetSize(store->GetValue().GetType());
        offsets.emplace(start, end);
        stored.emplace(start, end);
        for (auto [offStart, offEnd] : offsets) {
          if (end <= offStart || offEnd <= start) {
            continue;
          }
          if (start == offStart && end == offEnd) {
            continue;
          }
          overlap = true;
        }
      }
    }
    for (auto &[start, insts] : loads) {
      for (auto *load : insts) {
        auto end = start + GetSize(load->GetType());
        offsets.emplace(start, end);
        loaded.emplace(start, end);
        for (auto [offStart, offEnd] : offsets) {
          if (end <= offStart || offEnd <= start) {
            continue;
          }
          if (start == offStart && end == offEnd) {
            continue;
          }
          overlap = true;
        }
      }
    }
  }

  // Eliminate individual fields.
  auto *data = object.getParent();
  std::optional<int64_t> offset = 0;
  for (Atom &atom : object) {
    auto name = atom.getName();
    for (auto it = atom.begin(); it != atom.end(); ) {
      Item &item = *it++;
      // Check whether the item overlaps with any loads/stores.
      bool reachable = false;
      if (offset) {
        int64_t start = *offset;
        int64_t end = start + item.GetSize();
        offset = end;
        for (auto [accStart, accEnd] : offsets) {
          if (end <= accStart || accEnd <= start) {
            continue;
          }
          reachable = true;
          break;
        }
      }
      if (reachable) {
        pinned = true;
        continue;
      }

      // Skip non-references - only pointers are simplified.
      auto *expr = ::cast_or_null<SymbolOffsetExpr>(item.AsExpr());
      if (!expr) {
        continue;
      }

      // If the field is not accessed, try to simplify it.
      auto *sym = expr->GetSymbol();
      if (auto *refAtom = ::cast_or_null<Atom>(sym)) {
        auto *refObj = refAtom->getParent();
        auto *refData = refObj->getParent();
        if (refObj == &object) {
          continue;
        }
        if (refData == data) {
          auto it = pinned_.find(refObj);
          assert(it != pinned_.end() && "node not traversed");
          if (it->second) {
            pinned = true;
            continue;
          }
        }
      }

      LLVM_DEBUG(llvm::dbgs() << sym->getName() << " from " << name << "\n");
      NumReferencesRemoved++;
      if (name.endswith("__gc_roots")) {
        item.eraseFromParent();
      } else {
        atom.AddItem(Item::CreateInt64(1));
        item.eraseFromParent();
      }
      changed_ = true;
    }
    // TODO: align according to the alignment of the next object.
    offset = std::nullopt;
  }

  pinned_.emplace(&object, pinned);
}

// -----------------------------------------------------------------------------
void CamlGlobalSimplifier::SimplifyAllocs()
{
  for (Func &func : prog_) {
    for (Block &block : func) {
      // Find blocks which are executed at most once that
      // end with a call to an OCaml allocation.
      if (!init_[block]) {
        continue;
      }
      auto *call = ::cast_or_null<CallInst>(block.GetTerminator());
      if (!call) {
        continue;
      }
      auto calleeMov = ::cast_or_null<MovInst>(call->GetCallee());
      if (!calleeMov) {
        continue;
      }
      auto callee = ::cast_or_null<Global>(calleeMov->GetArg());
      if (!callee || !callee->getName().startswith("caml_alloc")) {
        continue;
      }
      // All the indirect uses of the allocation site should be anchored.
      std::queue<std::pair<Inst *, Ref<Inst>>> q;
      for (Use &use : call->uses()) {
        if ((*use).Index() != call->GetNumRets() - 1) {
          continue;
        }
        if (auto add = ::cast_or_null<AddInst>(use.getUser())) {
          q.emplace(add, ::cast<Inst>(*use));
        }
      }
      // Ensure pointers to the object escape only to objects which are
      // never de-allocated to locations which are written at most once.
      bool anchored = true;
      while (anchored && !q.empty()) {
        auto [i, ref] = q.front();
        q.pop();
        switch (i->GetKind()) {
          default: {
            anchored = false;
            break;
          }
          case Inst::Kind::STORE: {
            auto *store = ::cast<StoreInst>(i);
            if (store->GetValue() == ref) {
              anchored = anchored && anchor_.count(i) != 0;
            }
            break;
          }
          case Inst::Kind::ADD: {
            for (auto *user : i->users()) {
              q.emplace(::cast<Inst>(user), i);
            }
            break;
          }
        }
      }
      if (!anchored) {
        continue;
      }

      if (callee->getName() == "caml_alloc1") {
        ReplaceAlloc(call, 1);
        continue;
      }
      if (callee->getName() == "caml_alloc2") {
        ReplaceAlloc(call, 2);
        continue;
      }
      if (callee->getName() == "caml_alloc3") {
        ReplaceAlloc(call, 3);
        continue;
      }
      if (callee->getName() == "caml_allocN") {
        if (auto sub = ::cast_or_null<SubInst>(call->arg(call->arg_size() - 1))) {
          if (auto n = GetConstant(sub->GetRHS())) {
            ReplaceAllocN(call, sub->GetLHS(), *n / 8 - 1);
            continue;
          }
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
void CamlGlobalSimplifier::ReplaceAlloc(CallInst *call, unsigned size)
{
  auto &block = *call->getParent();
  
  auto *obj = new Object();
  root_->getParent()->AddObject(obj);
  std::string name;
  llvm::raw_string_ostream os(name);
  os << block.getName() << "$alloc";
  auto *atom = new Atom(name);
  obj->AddAtom(atom);
  atom->AddItem(Item::CreateInt64(size << 10));
  for (unsigned i = 0; i < size; ++i) {
    atom->AddItem(Item::CreateInt64(1));
  }

  unsigned idx = call->GetNumRets() - 1;
  auto *mov = new MovInst(call->type(idx), atom, {});
  block.AddInst(mov);
  block.AddInst(new JumpInst(call->GetCont(), {}));

  for (auto ut = call->use_begin(); ut != call->use_end(); ) {
    Use &use = *ut++;
    auto inst = ::cast<Inst>(use.getUser());
    auto useIdx = (*use).Index();
    if (::cast_or_null<AddInst>(inst) && useIdx == idx) {
      use = mov;
    } else {
      use = call->arg(useIdx);
    }
  }
  call->eraseFromParent();
  NumAllocsRemoved++;
}

// -----------------------------------------------------------------------------
void CamlGlobalSimplifier::ReplaceAllocN(
    CallInst *call, 
    Ref<Inst> young,
    unsigned size)
{
  auto &block = *call->getParent();
  
  auto *obj = new Object();
  root_->getParent()->AddObject(obj);
  std::string name;
  llvm::raw_string_ostream os(name);
  os << block.getName() << "$alloc";
  auto *atom = new Atom(name);
  obj->AddAtom(atom);
  atom->AddItem(Item::CreateInt64(size << 10));
  for (unsigned i = 0; i < size; ++i) {
    atom->AddItem(Item::CreateInt64(1));
  }

  unsigned idx = call->GetNumRets() - 1;
  auto *mov = new MovInst(call->type(idx), atom, {});
  block.AddInst(mov);
  block.AddInst(new JumpInst(call->GetCont(), {}));

  for (auto ut = call->use_begin(); ut != call->use_end(); ) {
    Use &use = *ut++;
    auto inst = ::cast<Inst>(use.getUser());
    auto useIdx = (*use).Index();
    if (::cast_or_null<AddInst>(inst) && useIdx == idx) {
      use = mov;
    } else {
      if (useIdx == idx) {
        use = young;
      } else {
        use = call->arg(useIdx);
      }
    }
  }
  call->eraseFromParent();
  NumAllocsRemoved++;
}

// -----------------------------------------------------------------------------
const char *CamlGlobalSimplifyPass::kPassID = "caml-global-simplify";

// -----------------------------------------------------------------------------
const char *CamlGlobalSimplifyPass::GetPassName() const
{
  return "OCaml Global Data Item Simplification";
}

// -----------------------------------------------------------------------------
bool CamlGlobalSimplifyPass::Run(Prog &prog)
{
  const auto &cfg = GetConfig();
  if (!cfg.Static) {
    return false;
  }
  auto *globals = ::cast_or_null<Atom>(prog.GetGlobal("caml_globals"));
  if (!globals) {
    return false;
  }

  const std::string entryName = cfg.Entry.empty() ? "_start" : cfg.Entry;
  auto *entry = ::cast_or_null<Func>(prog.GetGlobal(entryName));
  auto *root = globals->getParent();

  bool changed = false;
  while (CamlGlobalSimplifier(prog, root, entry).Simplify()) {
    changed = true;
  }
  return changed;
}
