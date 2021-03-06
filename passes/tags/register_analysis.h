// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <llvm/Support/raw_ostream.h>

#include "core/inst_visitor.h"
#include "core/target.h"
#include "core/analysis/dominator.h"
#include "passes/tags/tagged_type.h"



namespace tags {
class Init;
class Step;

/// Cache of dominator/post-dominator trees and frontiers.
struct DominatorCache {
  /// Dominator tree.
  DominatorTree DT;
  /// Dominance frontier.
  DominanceFrontier DF;
  /// Post-Dominator Tree.
  PostDominatorTree PDT;
  /// Post-Dominance Frontier.
  PostDominanceFrontier PDF;

  DominatorCache(Func &func);
};

class RegisterAnalysis {
public:
  RegisterAnalysis(Prog &prog, const Target *target, bool banPolymorphism)
    : prog_(prog)
    , target_(target)
    , banPolymorphism_(banPolymorphism)
  {
    Solve();
  }

  void Solve();

  /// Dump the results of the analysis.
  void dump(llvm::raw_ostream &os = llvm::errs());

  /// Find the type assigned to a vreg.
  TaggedType Find(ConstRef<Inst> ref)
  {
    auto it = types_.find(ref);
    return it == types_.end() ? TaggedType::Unknown() : it->second;
  }

  /// Set the type, typically after rewriting an instruction.
  void Replace(Ref<Inst> oldInst, Ref<Inst> newInst, const TaggedType &type);
  /// Replace an instruction.
  void Replace(Inst *oldInst, Inst *newInst);
  /// Erase a type after deleting an instruction.
  void Erase(Ref<Inst> oldInst);

private:
  friend class Init;
  friend class Step;
  friend class Refinement;
  friend class ConstraintSolver;

  /// Mark an instruction with a type.
  bool Mark(Ref<Inst> inst, const TaggedType &type);
  /// Mark operators with a type.
  bool Mark(Inst &inst, const TaggedType &type)
  {
    return Mark(inst.GetSubValue(0), type);
  }

  /// Define a new instruction with existing users.
  bool Define(Ref<Inst> inst, const TaggedType &type);
  /// Refine an instruction with a type.
  bool Refine(Ref<Inst> inst, const TaggedType &type);
  /// Refine operators with a type.
  bool Refine(Inst &inst, const TaggedType &type)
  {
    return Refine(inst.GetSubValue(0), type);
  }
  /// Check whether an instruction was defined by refinement.
  bool IsDefined(Ref<Inst> inst)
  {
    return defs_.count(inst) != 0;
  }

  /// Queue the users of an instruction to be updated.
  void ForwardQueue(Ref<Inst> inst);
  /// Queue for the backward pass.
  void BackwardQueue(Ref<Inst > inst);

  /// Check whether an instruction can be polymorphic.
  static bool IsPolymorphic(const Inst &inst);

private:
  /// Return cached dominance information.
  DominatorCache &GetDoms(Func &func)
  {
    auto it = doms_.emplace(&func, nullptr);
    if (it.second) {
      it.first->second.reset(new DominatorCache(func));
    }
    return *it.first->second;
  }
  /// Rebuild cached dominance info.
  DominatorCache &RebuildDoms(Func &func)
  {
    auto it = doms_.emplace(&func, nullptr);
    it.first->second.reset(new DominatorCache(func));
    return *it.first->second;
  }

private:
  /// Reference to the underlying program.
  Prog &prog_;
  /// Reference to the target arch.
  const Target *target_;
  /// Ban polymorphic arithmetic operators.
  bool banPolymorphism_;
  /// Queue of instructions to propagate information from.
  std::queue<Inst *> forwardQueue_;
  /// Queue of PHI nodes, evaluated after other instructions.
  std::queue<PhiInst *> forwardPhiQueue_;
  /// Set of instructions in the queue.
  std::unordered_set<Inst *> inForwardQueue_;
  /// Queue of functions for backward propagation.
  std::queue<Func *> backwardQueue_;
  /// Set of functions in the backward queue.
  std::unordered_set<Func *> inBackwardQueue_;
  /// Queue of functions for refine propagation.
  std::queue<Inst *> refineQueue_;
  /// Set of functions in the refine queue.
  std::unordered_set<Inst *> inRefineQueue_;
  /// Mapping from instructions to their types.
  std::unordered_map<ConstRef<Inst>, TaggedType> types_;
  /// Mapping from indices to arguments.
  std::unordered_map
    < std::pair<const Func *, unsigned>
    , std::vector<ArgInst *>
    > args_;
  /// Mapping from functions to their return values.
  std::unordered_map<const Func *, std::vector<TaggedType>> rets_;
  /// Cache of dominators for functions.
  std::unordered_map<Func *, std::unique_ptr<DominatorCache>> doms_;
  /// Set of defined references.
  std::unordered_set<Ref<Inst>> defs_;
};

} // end namespace
