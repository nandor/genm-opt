// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#include <llvm/ADT/SCCIterator.h>

#include "core/atom.h"
#include "core/block.h"
#include "core/object.h"
#include "core/extern.h"
#include "passes/pre_eval/pointer_closure.h"
#include "passes/pre_eval/symbolic_context.h"
#include "passes/pre_eval/symbolic_heap.h"



// -----------------------------------------------------------------------------
template <>
struct llvm::GraphTraits<PointerClosure::Node *> {
  using NodeRef = PointerClosure::Node *;
  using ChildIteratorType = PointerClosure::Node::node_iterator;

  static ChildIteratorType child_begin(NodeRef n) { return n->nodes_begin(); }
  static ChildIteratorType child_end(NodeRef n) { return n->nodes_end(); }
};

// -----------------------------------------------------------------------------
template <>
struct llvm::GraphTraits<PointerClosure *> : public llvm::GraphTraits<PointerClosure::Node *> {
  static NodeRef getEntryNode(PointerClosure *g) { return g->GetRoot(); }
};

// -----------------------------------------------------------------------------
PointerClosure::PointerClosure(SymbolicHeap &heap, SymbolicContext &ctx)
  : heap_(heap)
  , ctx_(ctx)
{
  nodes_.Emplace(*this);

  for (auto &object : ctx.objects()) {
    Build(GetNode(object.GetID()), object);
    objects_.insert(&object);
  }
  Compact();
}

// -----------------------------------------------------------------------------
void PointerClosure::Add(const SymbolicValue &value)
{
  auto addNode = [&, this] (auto id)
  {
    const auto *n = nodes_.Map(id);
    funcs_.Union(n->funcs_);
    stacks_.Union(n->stacks_);
    escapes_.Union(n->self_);
    tainted_.Union(n->self_);
    escapes_.Union(n->refs_);
    tainted_.Union(n->refs_);
  };

  if (auto ptr = value.AsPointer()) {
    for (auto &addr : *ptr) {
      switch (addr.GetKind()) {
        case SymbolicAddress::Kind::OBJECT: {
          auto &h = addr.AsObject();
          addNode(GetNode(h.Object));
          continue;
        }
        case SymbolicAddress::Kind::OBJECT_RANGE: {
          auto &h = addr.AsObjectRange();
          addNode(GetNode(h.Object));
          continue;
        }
        case SymbolicAddress::Kind::EXTERN: {
          auto &ext = addr.AsExtern().Symbol;
          //llvm::errs() << "TODO: " << ext->getName() << "\n";
          continue;
        }
        case SymbolicAddress::Kind::EXTERN_RANGE: {
          auto &ext = addr.AsExtern().Symbol;
          //llvm::errs() << "TODO: " << ext->getName() << "\n";
          continue;
        }
        case SymbolicAddress::Kind::FUNC: {
          funcs_.Insert(addr.AsFunc().F);
          continue;
        }
        case SymbolicAddress::Kind::BLOCK: {
          auto &block = addr.AsBlock().B;
          //llvm::errs() << "TODO: " << block->getName() << "\n";
          continue;
        }
        case SymbolicAddress::Kind::STACK: {
          stacks_.Insert(addr.AsStack().Frame);
          continue;
        }
      }
      llvm_unreachable("invalid address kind");
    }
  }
}

// -----------------------------------------------------------------------------
void PointerClosure::AddRead(Object *g)
{
  const auto *n = nodes_.Map(GetNode(g));
  funcs_.Union(n->funcs_);
  stacks_.Union(n->stacks_);
  escapes_.Union(n->refs_);
}

// -----------------------------------------------------------------------------
void PointerClosure::AddWritten(Object *g)
{
  const auto *n = nodes_.Map(GetNode(g));
  funcs_.Union(n->funcs_);
  tainted_.Union(n->refs_);
  tainted_.Union(n->self_);
}

// -----------------------------------------------------------------------------
void PointerClosure::AddEscaped(Object *g)
{
  const auto *n = nodes_.Map(GetNode(g));
  funcs_.Union(n->funcs_);
  escapes_.Union(n->self_);
  escapes_.Union(n->refs_);
  tainted_.Union(n->self_);
  tainted_.Union(n->refs_);
}

// -----------------------------------------------------------------------------
void PointerClosure::Add(Func *f)
{
  funcs_.Insert(heap_.Function(f));
}

// -----------------------------------------------------------------------------
std::shared_ptr<SymbolicPointer> PointerClosure::BuildTainted()
{
  if (tainted_.Empty()) {
    return nullptr;
  }
  auto ptr = std::make_shared<SymbolicPointer>();
  ptr->Add(tainted_);
  return ptr;
}

// -----------------------------------------------------------------------------
std::shared_ptr<SymbolicPointer> PointerClosure::BuildTaint()
{
  if (funcs_.Empty() && stacks_.Empty() && escapes_.Empty()) {
    return nullptr;
  }
  auto ptr = std::make_shared<SymbolicPointer>();
  ptr->Add(funcs_);
  ptr->Add(stacks_);
  ptr->Add(escapes_);
  return ptr;
}

// -----------------------------------------------------------------------------
ID<PointerClosure::Node> PointerClosure::GetNode(ID<SymbolicObject> id)
{
  auto it = objectToNode_.emplace(id, 0);
  if (it.second) {
    auto nodeID = nodes_.Emplace(*this);
    it.first->second = nodeID;
    nodes_.Map(nodeID)->self_.Insert(id);
    nodes_.Map(0)->nodes_.Insert(nodeID);
  }
  return it.first->second;
}

// -----------------------------------------------------------------------------
ID<PointerClosure::Node> PointerClosure::GetNode(Object *object)
{
  auto &repr = ctx_.GetObject(object);
  if (objects_.insert(&repr).second) {
    Compact();
  }
  return GetNode(repr.GetID());
}

// -----------------------------------------------------------------------------
void PointerClosure::Build(ID<Node> id, SymbolicObject &object)
{
  auto *node = nodes_.Map(id);
  for (const auto &value : object) {
    if (auto ptr = value.AsPointer()) {
      for (auto &addr : *ptr) {
        switch (addr.GetKind()) {
          case SymbolicAddress::Kind::OBJECT: {
            auto objectID = GetNode(addr.AsObject().Object);
            node->nodes_.Insert(objectID);
            continue;
          }
          case SymbolicAddress::Kind::OBJECT_RANGE: {
            auto objectID = GetNode(addr.AsObjectRange().Object);
            node->nodes_.Insert(objectID);
            continue;
          }
          case SymbolicAddress::Kind::EXTERN: {
            auto &ext = addr.AsExtern().Symbol;
            // TODO
            continue;
          }
          case SymbolicAddress::Kind::EXTERN_RANGE: {
            auto &ext = addr.AsExternRange().Symbol;
            // TODO
            continue;
          }
          case SymbolicAddress::Kind::FUNC: {
            node->funcs_.Insert(addr.AsFunc().F);
            continue;
          }
          case SymbolicAddress::Kind::BLOCK: {
            auto *block = addr.AsBlock().B;
            // TODO
            continue;
          }
          case SymbolicAddress::Kind::STACK: {
            node->stacks_.Insert(addr.AsStack().Frame);
            continue;
          }
        }
        llvm_unreachable("invalid address kind");
      }
    }
  }
}

void PointerClosure::Compact()
{
  std::vector<std::vector<ID<Node>>> nodes;
  for (auto it = llvm::scc_begin(this); !it.isAtEnd(); ++it) {
    auto &scc = nodes.emplace_back();
    for (Node *node : *it) {
      scc.push_back(node->id_);
    }
  }

  for (const auto &scc : nodes) {
    for (unsigned i = 1, n = scc.size(); i < n; ++i) {
      nodes_.Union(scc[0], scc[i]);
    }
    auto *node = nodes_.Map(scc[0]);
    for (ID<Node> refID : node->nodes_) {
      if (auto *ref = nodes_.Get(refID)) {
        node->refs_.Union(ref->self_);
        node->refs_.Union(ref->refs_);
        node->stacks_.Union(ref->stacks_);
        node->funcs_.Union(ref->funcs_);
      }
    }
  }

  nodes_.Get(0)->nodes_.Clear();
}
