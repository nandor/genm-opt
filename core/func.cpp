// This file if part of the genm-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#include "core/func.h"
#include "core/block.h"



// -----------------------------------------------------------------------------
Func::Func(Prog *prog, const std::string_view name)
  : Global(Global::Kind::FUNC, name)
  , prog_(prog)
  , stackSize_(0ull)
  , callConv_(CallingConv::C)
  , numFixedArgs_(0ul)
{
}

// -----------------------------------------------------------------------------
void Func::AddBlock(Block *block)
{
  blocks_.push_back(block);
}

// -----------------------------------------------------------------------------
void Func::SetStackSize(size_t stackSize)
{
  stackSize_ = stackSize;
}
