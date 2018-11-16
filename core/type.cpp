// This file if part of the genm-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#include "core/type.h"



// -----------------------------------------------------------------------------
bool IsIntegerType(Type type)
{
  switch (type) {
    case Type::F32: case Type::F64: {
      return false;
    }
    case Type::I8:  case Type::U8:
    case Type::I16: case Type::U16:
    case Type::I32: case Type::U32:
    case Type::I64: case Type::U64: {
      return true;
    }
  }
}

// -----------------------------------------------------------------------------
bool IsFloatType(Type type)
{
  return !IsIntegerType(type);
}
