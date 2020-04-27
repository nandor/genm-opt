// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <llvm/Support/MemoryBuffer.h>

class Prog;



std::unique_ptr<Prog> Parse(llvm::MemoryBufferRef buffer);