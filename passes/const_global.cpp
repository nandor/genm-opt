// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#include <queue>

#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SCCIterator.h>

#include "core/block.h"
#include "core/cast.h"
#include "core/cfg.h"
#include "core/prog.h"
#include "core/insts.h"
#include "passes/const_global.h"



// -----------------------------------------------------------------------------
const char *ConstGlobalPass::kPassID = "const-global";

// -----------------------------------------------------------------------------
enum class AtomUseKind
{
  UNUSED,
  UNKNOWN,
  READ_ONLY,
  WRITE_ONLY
};

// -----------------------------------------------------------------------------
static AtomUseKind Classify(const Atom &atom)
{
  if (!atom.IsLocal()) {
    return AtomUseKind::UNKNOWN;
  }

  std::queue<const User *> qu;
  std::queue<std::pair<const Inst *, ConstRef<Inst>>> qi;
  for (const User *user : atom.users()) {
    qu.push(user);
  }
  while (!qu.empty()) {
    const User *u = qu.front();
    qu.pop();
    if (!u) {
      return AtomUseKind::UNKNOWN;
    }

    switch (u->GetKind()) {
      case Value::Kind::INST: {
        auto *inst = static_cast<const Inst *>(u);
        qi.emplace(inst, nullptr);
        continue;
      }
      case Value::Kind::EXPR: {
        auto *expr = static_cast<const Expr *>(u);
        for (const User *user : expr->users()) {
          qu.push(user);
        }
        continue;
      }
      case Value::Kind::GLOBAL: {
        switch (static_cast<const Global *>(u)->GetKind()) {
          case Global::Kind::FUNC:
          case Global::Kind::BLOCK:
          case Global::Kind::ATOM: {
            llvm_unreachable("invalid item user");
          }
          case Global::Kind::EXTERN: {
            return AtomUseKind::UNKNOWN;
          }
        }
        llvm_unreachable("invalid global kind");
      }
      case Value::Kind::CONST: {
        llvm_unreachable("invalid user");
      }
    }
    llvm_unreachable("invalid value kind");
  }

  unsigned loadCount = 0;
  unsigned storeCount = 0;
  std::set<const Inst *> vi;
  while (!qi.empty()) {
    auto [i, ref] = qi.front();
    qi.pop();
    if (!vi.insert(i).second) {
      continue;
    }
    switch (i->GetKind()) {
      default: return AtomUseKind::UNKNOWN;
      case Inst::Kind::LOAD: {
        loadCount++;
        continue;
      }
      case Inst::Kind::STORE: {
        auto *store = static_cast<const StoreInst *>(i);
        if (store->GetValue() == ref) {
          return AtomUseKind::UNKNOWN;
        }
        storeCount++;
        continue;
      }
      case Inst::Kind::MOV:
      case Inst::Kind::ADD:
      case Inst::Kind::SUB:
      case Inst::Kind::PHI: {
        for (const User *user : i->users()) {
          if (auto *inst = ::cast_or_null<const Inst>(user)) {
            qi.emplace(inst, i);
          }
        }
        continue;
      }
    }
  }
  if (loadCount == 0 && storeCount == 0) {
    return AtomUseKind::UNUSED;
  }
  if (loadCount == 0 && storeCount) {
    return AtomUseKind::WRITE_ONLY;
  }
  if (storeCount == 0 && loadCount) {
    return AtomUseKind::READ_ONLY;
  }
  return AtomUseKind::UNKNOWN;
}

// -----------------------------------------------------------------------------
static bool EraseStores(Atom &atom)
{
  std::queue<User *> qu;
  std::queue<Inst *> qi;
  for (User *user : atom.users()) {
    qu.push(user);
  }

  bool changed = false;
  while (!qu.empty()) {
    User *u = qu.front();
    assert(u && "invalid use");
    qu.pop();

    switch (u->GetKind()) {
      case Value::Kind::INST: {
        qi.emplace(static_cast<Inst *>(u));
        continue;
      }
      case Value::Kind::EXPR: {
        for (User *user : static_cast<Expr *>(u)->users()) {
          qu.push(user);
        }
        continue;
      }
      case Value::Kind::CONST:
      case Value::Kind::GLOBAL: {
        llvm_unreachable("invalid user");
      }
    }
    llvm_unreachable("invalid value kind");
  }

  llvm::DenseSet<PhiInst *> visited;
  while (!qi.empty()) {
    auto i = qi.front();
    qi.pop();
    switch (i->GetKind()) {
      default: llvm_unreachable("invalid instruction");
      case Inst::Kind::STORE: {
        i->eraseFromParent();
        changed = true;
        continue;
      }
      case Inst::Kind::MOV:
      case Inst::Kind::ADD:
      case Inst::Kind::SUB: {
        for (User *user : i->users()) {
          if (auto *inst = ::cast_or_null<Inst>(user)) {
            qi.emplace(inst);
          }
        }
        continue;
      }
      case Inst::Kind::PHI: {
        auto *phi = static_cast<PhiInst *>(i);
        if (!visited.insert(phi).second) {
          for (User *user : phi->users()) {
            if (auto *inst = ::cast_or_null<Inst>(user)) {
              qi.emplace(inst);
            }
          }
        }
        continue;
      }
    }
  }
  return changed;
}

// -----------------------------------------------------------------------------
bool ConstGlobalPass::Run(Prog &prog)
{
  std::vector<Object *> unusedObjects, readOnlyObjects, writeOnlyObjects;
  for (Data &data : prog.data()) {
    for (Object &obj : data) {
      bool isReadOnly = true;
      bool isWriteOnly = true;
      for (const Atom &atom : obj) {
        switch (Classify(atom)) {
          case AtomUseKind::UNUSED: {
            continue;
          }
          case AtomUseKind::UNKNOWN: {
            isReadOnly = false;
            isWriteOnly = false;
            break;
          }
          case AtomUseKind::READ_ONLY: {
            isWriteOnly = false;
            continue;
          }
          case AtomUseKind::WRITE_ONLY: {
            isReadOnly = false;
            continue;
          }
        }
        break;
      }

      if (isReadOnly && !isWriteOnly) {
        if (!obj.getParent()->IsConstant()) {
          readOnlyObjects.push_back(&obj);
        }
      }
      if (isWriteOnly && !isReadOnly) {
        assert(!isReadOnly && "invalid classification");
        writeOnlyObjects.push_back(&obj);
      }
    }
  }

  bool changed = false;
  for (Object *object : readOnlyObjects) {
    if (object->getParent()->getName() == ".data") {
      Data *rodata = prog.GetData(".const");
      object->removeFromParent();
      rodata->AddObject(object);
      changed = true;
    }
  }
  for (Object *object : writeOnlyObjects) {
    for (Atom &atom : *object) {
      changed = EraseStores(atom) || changed;
    }
  }
  return changed;
}

// -----------------------------------------------------------------------------
const char *ConstGlobalPass::GetPassName() const
{
  return "Trivial Global Elimination";
}
