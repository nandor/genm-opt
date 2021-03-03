// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#include "passes/sccp/lattice.h"
#include "passes/sccp/solver.h"



// -----------------------------------------------------------------------------
static Lattice LoadInt(Atom::iterator it, unsigned off, unsigned size)
{
  switch (it->GetKind()) {
    case Item::Kind::INT8: {
      if (size == 1) {
        return Lattice::CreateInteger(llvm::APInt(8, it->GetInt8(), true));
      }
      break;
    }
    case Item::Kind::INT16: {
      if (size == 2) {
        return Lattice::CreateInteger(llvm::APInt(16, it->GetInt16(), true));
      }
      break;
    }
    case Item::Kind::INT32: {
      if (size == 4) {
        return Lattice::CreateInteger(llvm::APInt(32, it->GetInt32(), true));
      }
      break;
    }
    case Item::Kind::INT64: {
      if (size == 8) {
        return Lattice::CreateInteger(llvm::APInt(64, it->GetInt64(), true));
      }
      break;
    }
    case Item::Kind::STRING: {
      if (size == 1) {
        char chr = it->getString()[off];
        return Lattice::CreateInteger(llvm::APInt(8, chr, true));
      }
      break;
    }
    case Item::Kind::SPACE: {
      if (off + size <= it->GetSpace()) {
        return Lattice::CreateInteger(llvm::APInt(size * 8, 0, true));
      }
      break;
    }
    case Item::Kind::FLOAT64: {
      break;
    }
    case Item::Kind::EXPR32:
    case Item::Kind::EXPR64: {
      auto *expr = it->GetExpr();
      switch (expr->GetKind()) {
        case Expr::Kind::SYMBOL_OFFSET: {
          auto *sym = static_cast<SymbolOffsetExpr *>(expr);
          if (size == it->GetSize()) {
            return Lattice::CreateGlobal(sym->GetSymbol(), sym->GetOffset());
          }
          break;
        }
      }
      break;
    }
  }
  // TODO: implement loads based on endianness.
  return Lattice::Overdefined();
}

// -----------------------------------------------------------------------------
static Lattice LoadFloat(Atom::const_iterator it, unsigned off, unsigned size)
{
  if (it->GetKind() == Item::Kind::FLOAT64 && size == 8) {
    return Lattice::CreateFloat(it->GetFloat64());
  }
  if (it->GetKind() == Item::Kind::INT64 && size == 8) {
    union { int64_t i; double d; } u;
    u.i = it->GetInt64();
    return Lattice::CreateFloat(llvm::APFloat(u.d));
  }
  if (it->GetKind() == Item::Kind::INT32 && size == 4) {
    union { int32_t i; float f; } u;
    u.i = it->GetInt32();
    return Lattice::CreateFloat(llvm::APFloat(u.f));
  }
  return Lattice::Overdefined();
}

// -----------------------------------------------------------------------------
void SCCPSolver::VisitLoadInst(LoadInst &inst)
{
  auto &addr = GetValue(inst.GetAddr());
  auto ty = inst.GetType();
  switch (addr.GetKind()) {
    case Lattice::Kind::UNKNOWN:
    case Lattice::Kind::OVERDEFINED:
    case Lattice::Kind::UNDEFINED: {
      Mark(inst, addr);
      return;
    }
    case Lattice::Kind::INT:
    case Lattice::Kind::MASK:
    case Lattice::Kind::FLOAT:
    case Lattice::Kind::FLOAT_ZERO:
    case Lattice::Kind::FRAME:
    case Lattice::Kind::POINTER: {
      MarkOverdefined(inst);
      return;
    }
    case Lattice::Kind::RANGE: {
      auto *g = addr.GetRange();
      switch (g->GetKind()) {
        case Global::Kind::EXTERN: {
          MarkOverdefined(inst);
          return;
        }
        case Global::Kind::FUNC:
        case Global::Kind::BLOCK: {
          llvm_unreachable("not implemented");
        }
        case Global::Kind::ATOM: {
          auto *atom = static_cast<Atom *>(g);
          auto *object = atom->getParent();
          auto *data = object->getParent();
          if (!data->IsConstant()) {
            MarkOverdefined(inst);
            return;
          }
          if (object->size() != 1 || atom->size() != 1) {
            MarkOverdefined(inst);
            return;
          }
          if (!atom->begin()->IsSpace()) {
            MarkOverdefined(inst);
            return;
          }
          APInt v(GetBitWidth(ty), 0, true);
          Mark(inst, Lattice::CreateInteger(v));
          return;
        }
      }
      llvm_unreachable("invalid global kind");
    }
    case Lattice::Kind::GLOBAL: {
      auto *g = addr.GetGlobalSymbol();
      int64_t base = addr.GetGlobalOffset();
      switch (g->GetKind()) {
        case Global::Kind::EXTERN: {
          MarkOverdefined(inst);
          return;
        }
        case Global::Kind::FUNC:
        case Global::Kind::BLOCK: {
          llvm_unreachable("not implemented");
        }
        case Global::Kind::ATOM: {
          auto *atom = static_cast<Atom *>(g);
          auto *object = atom->getParent();
          auto *data = object->getParent();
          if (data->IsConstant()) {
            // Find the item at the given offset, along with the offset into it.
            auto it = atom->begin();
            int64_t itemOff;
            if (base < 0) {
              // TODO: allow negative offsets.
              MarkOverdefined(inst);
              return;
            } else {
              int64_t i;
              for (i = 0; it != atom->end() && i + it->GetSize() <= base; ++it) {
                if (it == atom->end()) {
                  // TODO: jump to next atom.
                  MarkOverdefined(inst);
                  return;
                }
                i += it->GetSize();
              }
              if (it == atom->end()) {
                MarkOverdefined(inst);
                return;
              }
              itemOff = base - i;
            }
            switch (ty) {
              case Type::I8: {
                Mark(inst, LoadInt(it, itemOff, 1));
                return;
              }
              case Type::I16: {
                Mark(inst, LoadInt(it, itemOff, 2));
                return;
              }
              case Type::I32: {
                Mark(inst, LoadInt(it, itemOff, 4));
                return;
              }
              case Type::I64: case Type::V64: {
                Mark(inst, LoadInt(it, itemOff, 8));
                return;
              }
              case Type::F32: {
                Mark(inst, LoadFloat(it, itemOff, 4));
                return;
              }
              case Type::F64: {
                Mark(inst, LoadFloat(it, itemOff, 8));
                return;
              }
              case Type::I128: case Type::F80: case Type::F128: {
                MarkOverdefined(inst);
                return;
              }
            }
          } else {
            MarkOverdefined(inst);
            return;
          }
        }
      }
      llvm_unreachable("invalid global kind");
    }
  }
  llvm_unreachable("invalid value kind");
}