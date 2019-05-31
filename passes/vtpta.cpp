// This file if part of the genm-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#include <cstdlib>

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/SmallPtrSet.h>

#include "core/block.h"
#include "core/cast.h"
#include "core/cfg.h"
#include "core/constant.h"
#include "core/data.h"
#include "core/func.h"
#include "core/insts.h"
#include "core/prog.h"
#include "passes/vtpta.h"



// -----------------------------------------------------------------------------
void VariantTypePointsToAnalysis::Run(Prog *prog)
{
}

// -----------------------------------------------------------------------------
const char *VariantTypePointsToAnalysis::GetPassName() const
{
  return "Variant Type Points-To Analysis";
}

// -----------------------------------------------------------------------------
char AnalysisID<VariantTypePointsToAnalysis>::ID;
