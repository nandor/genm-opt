// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#include <llvm/ADT/APSInt.h>

#include "core/atom.h"
#include "core/cond.h"
#include "core/func.h"
#include "passes/sccp/eval.h"


// -----------------------------------------------------------------------------
const llvm::fltSemantics &getSemantics(Type ty)
{
  switch (ty) {
    default: llvm_unreachable("not a float type");
    case Type::F32: return llvm::APFloat::IEEEsingle();
    case Type::F64: return llvm::APFloat::IEEEdouble();
    case Type::F80: return llvm::APFloat::x87DoubleExtended();
  }
}

// -----------------------------------------------------------------------------
static Lattice MakeBoolean(bool value, Type ty)
{
  switch (ty) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128:
      return Lattice::CreateInteger(APInt(GetSize(ty) * 8, value, true));
    case Type::F32:
    case Type::F64:
    case Type::F80:
      llvm_unreachable("invalid comparison");
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Extend(const Lattice &arg, Type ty)
{
  switch (arg.GetKind()) {
    case Lattice::Kind::UNKNOWN:
    case Lattice::Kind::OVERDEFINED:
    case Lattice::Kind::UNDEFINED: {
      return arg;
    }
    case Lattice::Kind::INT: {
      const auto &i = arg.GetInt();
      switch (ty) {
        case Type::I8:
        case Type::I16:
        case Type::I32:
        case Type::I64:
        case Type::I128: {
          return Lattice::CreateInteger(i.sextOrTrunc(GetSize(ty) * 8));
        }
        case Type::F32:
        case Type::F64: {
          llvm_unreachable("not implemented");
        }
        case Type::F80: {
          llvm_unreachable("not implemented");
        }
      }
      llvm_unreachable("invalid value kind");
    }
    case Lattice::Kind::FLOAT: {
      const auto &f = arg.GetFloat();
      switch (ty) {
        case Type::I8:
        case Type::I16:
        case Type::I32:
        case Type::I64:
        case Type::I128: {
          llvm_unreachable("not implemented");
        }
        case Type::F32:
        case Type::F64:
        case Type::F80: {
          bool lossy;
          APFloat r(f);
          r.convert(getSemantics(ty), APFloat::rmNearestTiesToEven, &lossy);
          return Lattice::CreateFloat(r);
        }
      }
      llvm_unreachable("invalid value kind");
    }
    case Lattice::Kind::FRAME:
    case Lattice::Kind::GLOBAL: {
      switch (ty) {
        case Type::I64: {
          return arg;
        }
        case Type::I8:
        case Type::I16:
        case Type::I32:
        case Type::I128:
        case Type::F32:
        case Type::F64:
        case Type::F80: {
          llvm_unreachable("not implemented");
        }
      }
      llvm_unreachable("invalid value kind");
    }
  }
  llvm_unreachable("invalid value kind");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Bitcast(const Lattice &arg, Type ty)
{
  switch (arg.GetKind()) {
    case Lattice::Kind::UNKNOWN:
    case Lattice::Kind::OVERDEFINED:
    case Lattice::Kind::UNDEFINED: {
      return arg;
    }
    case Lattice::Kind::INT: {
      switch (ty) {
        case Type::I8:
        case Type::I16:
        case Type::I32:
        case Type::I64:
        case Type::I128: {
          APInt i = arg.GetInt();
          return Lattice::CreateInteger(i.sextOrTrunc(GetSize(ty) * 8));
        }
        case Type::F32:
        case Type::F64:
        case Type::F80: {
          // TODO: implement the bit cast
          return Lattice::Overdefined();
        }
      }
      llvm_unreachable("not implemented");
    }
    case Lattice::Kind::FLOAT: {
      switch (ty) {
        case Type::I8:
        case Type::I16:
        case Type::I32:
        case Type::I64:
        case Type::I128: {
          APInt i = arg.GetFloat().bitcastToAPInt();
          return Lattice::CreateInteger(i.sextOrTrunc(GetSize(ty) * 8));
        }
        case Type::F32:
        case Type::F64:
        case Type::F80: {
          llvm_unreachable("not implemented");
        }
      }
    }
    case Lattice::Kind::FRAME:
    case Lattice::Kind::GLOBAL: {
      switch (ty) {
        case Type::I8:
        case Type::I16:
        case Type::I32:
        case Type::I128: {
          llvm_unreachable("not implemented");
        }
        case Type::I64: {
          return arg;
        }
        case Type::F32:
        case Type::F64:
        case Type::F80: {
          llvm_unreachable("not implemented");
        }
      }
    }
  }
  llvm_unreachable("invalid value kind");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(UnaryInst *inst, Lattice &arg)
{
  assert(!arg.IsUnknown() && "invalid argument");
  if (arg.IsOverdefined()) {
    return Lattice::Overdefined();
  }
  if (arg.IsUndefined()) {
    return Lattice::Undefined();
  }

  switch (inst->GetKind()) {
    default: llvm_unreachable("not a unary instruction");

    case Inst::Kind::ABS:    return Eval(static_cast<AbsInst *>(inst),      arg);
    case Inst::Kind::NEG:    return Eval(static_cast<NegInst *>(inst),      arg);
    case Inst::Kind::SQRT:   return Eval(static_cast<SqrtInst *>(inst),     arg);
    case Inst::Kind::SIN:    return Eval(static_cast<SinInst *>(inst),      arg);
    case Inst::Kind::COS:    return Eval(static_cast<CosInst *>(inst),      arg);
    case Inst::Kind::SEXT:   return Eval(static_cast<SExtInst *>(inst),     arg);
    case Inst::Kind::ZEXT:   return Eval(static_cast<ZExtInst *>(inst),     arg);
    case Inst::Kind::FEXT:   return Eval(static_cast<FExtInst *>(inst),     arg);
    case Inst::Kind::XEXT:   return Eval(static_cast<ZExtInst *>(inst),     arg);
    case Inst::Kind::TRUNC:  return Eval(static_cast<TruncInst *>(inst),    arg);
    case Inst::Kind::EXP:    return Eval(static_cast<ExpInst *>(inst),      arg);
    case Inst::Kind::EXP2:   return Eval(static_cast<Exp2Inst *>(inst),     arg);
    case Inst::Kind::LOG:    return Eval(static_cast<LogInst *>(inst),      arg);
    case Inst::Kind::LOG2:   return Eval(static_cast<Log2Inst *>(inst),     arg);
    case Inst::Kind::LOG10:  return Eval(static_cast<Log10Inst *>(inst),    arg);
    case Inst::Kind::FCEIL:  return Eval(static_cast<FCeilInst *>(inst),    arg);
    case Inst::Kind::FFLOOR: return Eval(static_cast<FFloorInst *>(inst),   arg);
    case Inst::Kind::POPCNT: return Eval(static_cast<PopCountInst *>(inst), arg);
    case Inst::Kind::CLZ:    return Eval(static_cast<CLZInst *>(inst),      arg);
  }
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(BinaryInst *inst, Lattice &l, Lattice &r)
{
  assert(!l.IsUnknown() && "invalid l");
  assert(!r.IsUnknown() && "invalid r");
  if (l.IsOverdefined() || r.IsOverdefined()) {
    return Lattice::Overdefined();
  }
  if (l.IsUndefined() || r.IsUndefined()) {
    return Lattice::Undefined();
  }

  const auto ty = inst->GetType();
  switch (inst->GetKind()) {
    default: llvm_unreachable("not a binary instruction");

    case Inst::Kind::SLL:      return Eval(Bitwise::SLL,  ty, l, r);
    case Inst::Kind::SRA:      return Eval(Bitwise::SRA,  ty, l, r);
    case Inst::Kind::SRL:      return Eval(Bitwise::SRL,  ty, l, r);
    case Inst::Kind::ROTL:     return Eval(Bitwise::ROTL, ty, l, r);

    case Inst::Kind::ADD:      return Eval(static_cast<AddInst *>(inst),      l, r);
    case Inst::Kind::SUB:      return Eval(static_cast<SubInst *>(inst),      l, r);
    case Inst::Kind::AND:      return Eval(static_cast<AndInst *>(inst),      l, r);
    case Inst::Kind::OR:       return Eval(static_cast<OrInst *>(inst),       l, r);
    case Inst::Kind::XOR:      return Eval(static_cast<XorInst *>(inst),      l, r);
    case Inst::Kind::CMP:      return Eval(static_cast<CmpInst *>(inst),      l, r);
    case Inst::Kind::UDIV:     return Eval(static_cast<UDivInst *>(inst),     l, r);
    case Inst::Kind::SDIV:     return Eval(static_cast<SDivInst *>(inst),     l, r);
    case Inst::Kind::UREM:     return Eval(static_cast<URemInst *>(inst),     l, r);
    case Inst::Kind::SREM:     return Eval(static_cast<SRemInst *>(inst),     l, r);
    case Inst::Kind::MUL:      return Eval(static_cast<MulInst *>(inst),      l, r);
    case Inst::Kind::POW:      return Eval(static_cast<PowInst *>(inst),      l, r);
    case Inst::Kind::COPYSIGN: return Eval(static_cast<CopySignInst *>(inst), l, r);
    case Inst::Kind::UADDO:    return Eval(static_cast<AddUOInst *>(inst),    l, r);
    case Inst::Kind::UMULO:    return Eval(static_cast<MulUOInst *>(inst),    l, r);
    case Inst::Kind::USUBO:    return Eval(static_cast<SubUOInst *>(inst),    l, r);
    case Inst::Kind::SADDO:    return Eval(static_cast<AddSOInst *>(inst),    l, r);
    case Inst::Kind::SMULO:    return Eval(static_cast<MulSOInst *>(inst),    l, r);
    case Inst::Kind::SSUBO:    return Eval(static_cast<SubSOInst *>(inst),    l, r);
  }
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(AbsInst *inst, Lattice &arg)
{
  llvm_unreachable("AbsInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(NegInst *inst, Lattice &arg)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128: {
      if (auto i = arg.AsInt()) {
        return Lattice::CreateInteger(-*i);
      }
      llvm_unreachable("cannot negate non-integer");
    }
    case Type::F64: case Type::F32: case Type::F80: {
      if (auto f = arg.AsFloat()) {
        return Lattice::CreateFloat(neg(*f));
      }
      llvm_unreachable("cannot negate non-float");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SqrtInst *inst, Lattice &arg)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128: {
      llvm_unreachable("sqrt expects a float");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      if (auto f = arg.AsFloat()) {
        // TODO: implement sqrt
        return Lattice::Overdefined();
      }
      llvm_unreachable("sqrt expects a float");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SinInst *inst, Lattice &arg)
{
  llvm_unreachable("SinInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(CosInst *inst, Lattice &arg)
{
  llvm_unreachable("CosInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SExtInst *inst, Lattice &arg)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128: {
      if (auto i = arg.AsInt()) {
        return Lattice::CreateInteger(i->sext(GetSize(ty) * 8));
      } else if (auto f = arg.AsFloat()) {
        llvm::APSInt r(APInt(GetSize(ty) * 8, 0, true), false);
        bool exact;
        f->convertToInteger(r, APFloat::rmNearestTiesToEven, &exact);
        return Lattice::CreateInteger(r);
      }
      llvm_unreachable("cannot sext non-integer");
    }

    case Type::F32: case Type::F64: case Type::F80: {
      if (auto i = arg.AsInt()) {
        APFloat r(APFloat::getZero(getSemantics(ty)));
        r.convertFromAPInt(*i, true, APFloat::rmNearestTiesToEven);
        return Lattice::CreateFloat(r);
      }
      llvm_unreachable("cannot sext non-float");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(ZExtInst *inst, Lattice &arg)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128: {
      if (auto i = arg.AsInt()) {
        return Lattice::CreateInteger(i->zext(GetSize(ty) * 8));
      } else if (auto f = arg.AsFloat()) {
        llvm::APSInt r(APInt(GetSize(ty) * 8, 0, false), true);
        bool exact;
        f->convertToInteger(r, APFloat::rmNearestTiesToEven, &exact);
        return Lattice::CreateInteger(r);
      }
      llvm_unreachable("cannot zext non-integer");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      if (auto i = arg.AsInt()) {
        APFloat r(APFloat::getZero(getSemantics(ty)));
        r.convertFromAPInt(*i, false, APFloat::rmNearestTiesToEven);
        return Lattice::CreateFloat(r);
      }
      llvm_unreachable("cannot zext non-float");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(FExtInst *inst, Lattice &arg)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128:  {
      llvm_unreachable("cannot fext integer");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      if (auto f = arg.AsFloat()) {
        bool lossy;
        APFloat r(*f);
        r.convert(getSemantics(ty), APFloat::rmNearestTiesToEven, &lossy);
        return Lattice::CreateFloat(r);
      }
      llvm_unreachable("cannot fext non-floats");
    }
  }
  llvm_unreachable("invalid instruction type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(TruncInst *inst, Lattice &arg)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128:  {
      unsigned bitWidth = GetSize(ty) * 8;
      if (auto i = arg.AsInt()) {
        return Lattice::CreateInteger(i->trunc(bitWidth));
      } else if (auto f = arg.AsFloat()) {
        llvm::APSInt r(APInt(bitWidth, 0, true), false);
        bool exact;
        f->convertToInteger(r, APFloat::rmNearestTiesToEven, &exact);
        return Lattice::CreateInteger(r);
      }
      llvm_unreachable("cannot truncate non-integer");
    }
    case Type::F64: case Type::F32: case Type::F80: {
      if (auto f = arg.AsFloat()) {
        bool lossy;
        APFloat r(*f);
        r.convert(getSemantics(ty), APFloat::rmNearestTiesToEven, &lossy);
        return Lattice::CreateFloat(r);
      }
      llvm_unreachable("cannot truncate non-float");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(ExpInst *inst, Lattice &arg)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128: {
      llvm_unreachable("cannot exp integer");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      if (auto f = arg.AsFloat()) {
        return Lattice::Overdefined();
      }
      llvm_unreachable("cannot exp non-floats");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(Exp2Inst *inst, Lattice &arg)
{
  llvm_unreachable("Exp2Inst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(LogInst *inst, Lattice &arg)
{
  llvm_unreachable("LogInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(Log2Inst *inst, Lattice &arg)
{
  llvm_unreachable("Log2Inst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(Log10Inst *inst, Lattice &arg)
{
  llvm_unreachable("Log10Inst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(FCeilInst *inst, Lattice &arg)
{
  llvm_unreachable("FCeilInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(FFloorInst *inst, Lattice &arg)
{
  llvm_unreachable("FFloorInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(PopCountInst *inst, Lattice &arg)
{
  llvm_unreachable("PopCountInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(CLZInst *inst, Lattice &arg)
{
  llvm_unreachable("CLZInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(AddInst *inst, Lattice &lhs, Lattice &rhs)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8: case Type::I16: case Type::I32: case Type::I128: {
      if (auto l = lhs.AsInt()) {
        if (auto r = rhs.AsInt()) {
          return Lattice::CreateInteger(*l + *r);
        }
      }
      llvm_unreachable("cannot add non-integers");
    }
    case Type::I64: {
      if (auto l = lhs.AsInt()) {
        if (rhs.IsFrame()) {
          return Lattice::CreateFrame(
              rhs.GetFrameObject(),
              rhs.GetFrameOffset() + l->getSExtValue()
          );
        } else if (rhs.IsGlobal()) {
          return Lattice::CreateGlobal(
              rhs.GetGlobalSymbol(),
              rhs.GetGlobalOffset() + l->getSExtValue()
          );
        } else if (auto r = rhs.AsInt()) {
          return Lattice::CreateInteger(*l + *r);
        }
      } else if (lhs.IsFrame()) {
        if (auto r = rhs.AsInt()) {
          return Lattice::CreateFrame(
              lhs.GetFrameObject(),
              lhs.GetFrameOffset() + r->getSExtValue()
          );
        }
      } else if (lhs.IsGlobal()) {
        if (auto r = rhs.AsInt()) {
          return Lattice::CreateGlobal(
              lhs.GetGlobalSymbol(),
              lhs.GetGlobalOffset() + r->getSExtValue()
          );
        }
      }
      llvm_unreachable("cannot add non-integers");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      if (auto fl = lhs.AsFloat()) {
        if (auto fr = rhs.AsFloat()) {
          APFloat result = *fl;
          result.add(*fr, APFloat::rmNearestTiesToEven);
          return Lattice::CreateFloat(result);
        }
      }
      llvm_unreachable("cannot add floats");
    }
  }
  llvm_unreachable("invalid value kind");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SubInst *inst, Lattice &lhs, Lattice &rhs)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8: case Type::I16: case Type::I32: case Type::I128: {
      if (auto l = lhs.AsInt()) {
        if (auto r = rhs.AsInt()) {
          return Lattice::CreateInteger(*l - *r);
        }
      }
      llvm_unreachable("cannot subtract non-integers");
    }
    case Type::I64: {
      if (auto l = lhs.AsInt()) {
        if (auto r = rhs.AsInt()) {
          return Lattice::CreateInteger(*l - *r);
        }
      } else if (lhs.IsFrame()) {
        if (auto r = rhs.AsInt()) {
          return Lattice::CreateFrame(
              lhs.GetFrameObject(),
              lhs.GetFrameOffset() - r->getSExtValue()
          );
        }
      } else if (lhs.IsGlobal()) {
        if (auto r = rhs.AsInt()) {
          return Lattice::CreateGlobal(
              lhs.GetGlobalSymbol(),
              lhs.GetGlobalOffset() - r->getSExtValue()
          );
        }
      }
      llvm_unreachable("cannot subtract non-integers");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      if (auto fl = lhs.AsFloat()) {
        if (auto fr = rhs.AsFloat()) {
          APFloat result(*fl);
          result.subtract(*fr, APFloat::rmNearestTiesToEven);
          return Lattice::CreateFloat(result);
        }
      }
      llvm_unreachable("cannot subtract floats");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(AndInst *inst, Lattice &lhs, Lattice &rhs)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I128: {
      if (auto l = lhs.AsInt()) {
        if (auto r = rhs.AsInt()) {
          return Lattice::CreateInteger(*l & *r);
        }
      }
      llvm_unreachable("cannot and non-integers");
    }
    case Type::I64: {
      if (auto l = lhs.AsInt()) {
        if (auto r = rhs.AsInt()) {
          return Lattice::CreateInteger(*l & *r);
        }
      } else if (lhs.IsGlobal()) {
        int64_t offset = lhs.GetGlobalOffset();
        unsigned align = lhs.GetGlobalSymbol()->GetAlignment();
        if (auto r = rhs.AsInt()) {
          uint64_t mask = r->getSExtValue();
          if (offset == 0 && mask < align) {
            return Lattice::CreateInteger(0);
          }
          return Lattice::Overdefined();
        }
      }

      llvm_unreachable("cannot and non-integers");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      llvm_unreachable("cannot and floats");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
static Lattice FrameOr(OrInst *i, unsigned obj, int64_t off, const APInt &v) {
  auto *func = i->getParent()->getParent();
  const auto &stackObj = func->object(obj);
  const auto align = stackObj.Alignment;
  const uint64_t value = v.getZExtValue();
  if (off % align == 0 && value < align) {
    return Lattice::CreateFrame(obj, off + value);
  }
  return Lattice::Overdefined();
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(OrInst *inst, Lattice &lhs, Lattice &rhs)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I128: {
      if (auto il = lhs.AsInt()) {
        if (auto ir = rhs.AsInt()) {
          return Lattice::CreateInteger(*il | *ir);
        }
      }
      llvm_unreachable("cannot or non-integers");
    }

    case Type::I64: {
      const unsigned bits = GetSize(ty) * 8;
      if (auto il = lhs.AsInt()) {
        if (auto ir = rhs.AsInt()) {
          return Lattice::CreateInteger(*il | *ir);
        } else if (rhs.IsFrame()) {
          return FrameOr(inst, rhs.GetFrameObject(), rhs.GetFrameOffset(), *il);
        }
      } else if (lhs.IsFrame()) {
        if (auto ir = rhs.AsInt()) {
          return FrameOr(inst, lhs.GetFrameObject(), lhs.GetFrameOffset(), *ir);
        }
      }
      llvm_unreachable("cannot or non-integers or frames");
    }

    case Type::F32: case Type::F64: case Type::F80: {
      llvm_unreachable("cannot or float types");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(XorInst *inst, Lattice &lhs, Lattice &rhs)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128: {
      if (auto il = lhs.AsInt()) {
        if (auto ir = rhs.AsInt()) {
          return Lattice::CreateInteger(*il ^ *ir);
        }
      }
      llvm_unreachable("cannot xor non-integer types");
    }

    case Type::F32: case Type::F64: case Type::F80: {
      llvm_unreachable("cannot xor float types");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(PowInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("PowInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(CopySignInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("CopySignInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(AddUOInst *inst, Lattice &lhs, Lattice &rhs)
{
  if (auto l = lhs.AsInt()) {
    if (auto r = rhs.AsInt()) {
      bool overflow;
      (void) l->uadd_ov(*r, overflow);
      return MakeBoolean(overflow, inst->GetType());
    }
  }
  llvm_unreachable("AddUOInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(MulUOInst *inst, Lattice &lhs, Lattice &rhs)
{
  if (auto l = lhs.AsInt()) {
    if (auto r = rhs.AsInt()) {
      bool overflow;
      (void) l->umul_ov(*r, overflow);
      return MakeBoolean(overflow, inst->GetType());
    }
  }
  llvm_unreachable("MulUOInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SubUOInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("SubUOInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(AddSOInst *inst, Lattice &lhs, Lattice &rhs)
{
  if (auto l = lhs.AsInt()) {
    if (auto r = rhs.AsInt()) {
      bool overflow;
      (void) l->sadd_ov(*r, overflow);
      return MakeBoolean(overflow, inst->GetType());
    }
  }
  llvm_unreachable("AddSOInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(MulSOInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("MulSOInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SubSOInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("SubSOInst");
}

// -----------------------------------------------------------------------------
static Lattice Compare(const APFloat &lhs, const APFloat &rhs, Cond cc, Type ty)
{
  switch (cc) {
    case Cond::EQ: case Cond::OEQ: case Cond::UEQ: {
      switch (lhs.compare(rhs)) {
        case llvm::APFloatBase::cmpEqual:
          return MakeBoolean(true, ty);
        case llvm::APFloatBase::cmpLessThan:
        case llvm::APFloatBase::cmpGreaterThan:
          return MakeBoolean(false, ty);
        case llvm::APFloatBase::cmpUnordered:
          return MakeBoolean(cc == Cond::UEQ, ty);
      }
      llvm_unreachable("invalid comparison result");
    }
    case Cond::NE: case Cond::ONE: case Cond::UNE: {
      switch (lhs.compare(rhs)) {
        case llvm::APFloatBase::cmpEqual:
          return MakeBoolean(false, ty);
        case llvm::APFloatBase::cmpLessThan:
        case llvm::APFloatBase::cmpGreaterThan:
          return MakeBoolean(true, ty);
        case llvm::APFloatBase::cmpUnordered:
          return MakeBoolean(cc == Cond::UNE, ty);
      }
      llvm_unreachable("invalid comparison result");
    }
    case Cond::LT: case Cond::OLT: case Cond::ULT:{
      switch (lhs.compare(rhs)) {
        case llvm::APFloatBase::cmpEqual:
        case llvm::APFloatBase::cmpGreaterThan:
          return MakeBoolean(false, ty);
        case llvm::APFloatBase::cmpLessThan:
          return MakeBoolean(true, ty);
        case llvm::APFloatBase::cmpUnordered:
          return MakeBoolean(cc == Cond::ULT, ty);
      }
      llvm_unreachable("invalid comparison result");
    }
    case Cond::GT: case Cond::OGT: case Cond::UGT:{
      switch (lhs.compare(rhs)) {
        case llvm::APFloatBase::cmpEqual:
        case llvm::APFloatBase::cmpLessThan:
          return MakeBoolean(false, ty);
        case llvm::APFloatBase::cmpGreaterThan:
          return MakeBoolean(true, ty);
        case llvm::APFloatBase::cmpUnordered:
          return MakeBoolean(cc == Cond::UGT, ty);
      }
      llvm_unreachable("invalid comparison result");
    }
    case Cond::LE: case Cond::OLE: case Cond::ULE:{
      switch (lhs.compare(rhs)) {
        case llvm::APFloatBase::cmpGreaterThan:
          return MakeBoolean(false, ty);
        case llvm::APFloatBase::cmpLessThan:
        case llvm::APFloatBase::cmpEqual:
          return MakeBoolean(true, ty);
        case llvm::APFloatBase::cmpUnordered:
          return MakeBoolean(cc == Cond::ULE, ty);
      }
      llvm_unreachable("invalid comparison result");
    }
    case Cond::GE: case Cond::OGE: case Cond::UGE:{
      switch (lhs.compare(rhs)) {
        case llvm::APFloatBase::cmpLessThan:
          return MakeBoolean(false, ty);
        case llvm::APFloatBase::cmpEqual:
        case llvm::APFloatBase::cmpGreaterThan:
          return MakeBoolean(true, ty);
        case llvm::APFloatBase::cmpUnordered:
          return MakeBoolean(cc == Cond::UGE, ty);
      }
      llvm_unreachable("invalid comparison result");
    }
  }
  llvm_unreachable("invalid condition code");
}

// -----------------------------------------------------------------------------
static bool Compare(const APInt &lhs, const APInt &rhs, Cond cc)
{
  switch (cc) {
    case Cond::EQ: case Cond::OEQ: case Cond::UEQ: return lhs == rhs;
    case Cond::NE: case Cond::ONE: case Cond::UNE: return lhs != rhs;
    case Cond::LT: case Cond::OLT: return lhs.slt(rhs);
    case Cond::ULT:                return lhs.ult(rhs);
    case Cond::GT: case Cond::OGT: return lhs.sgt(rhs);
    case Cond::UGT:                return lhs.ugt(rhs);
    case Cond::LE: case Cond::OLE: return lhs.sle(rhs);
    case Cond::ULE:                return lhs.ule(rhs);
    case Cond::GE: case Cond::OGE: return lhs.sge(rhs);
    case Cond::UGE:                return lhs.uge(rhs);
  }
  llvm_unreachable("invalid condition code");
}

// -----------------------------------------------------------------------------
static Lattice Compare(
    unsigned lobj,
    int64_t loff,
    unsigned robj,
    int64_t roff,
    Cond cc,
    Type ty)
{
  auto Flag = [ty](bool value) {
    return MakeBoolean(value, ty);
  };
  auto Cmp = [ty, &Flag, lobj, robj](bool value) {
    return (lobj == robj) ? Flag(value) : Lattice::Undefined();
  };

  bool equal = lobj == robj && loff == roff;
  switch (cc) {
    case Cond::EQ: case Cond::OEQ: case Cond::UEQ: return Flag(equal);
    case Cond::NE: case Cond::ONE: case Cond::UNE: return Flag(!equal);
    case Cond::LT: case Cond::OLT: case Cond::ULT: return Cmp(loff < roff);
    case Cond::GT: case Cond::OGT: case Cond::UGT: return Cmp(loff > roff);
    case Cond::LE: case Cond::OLE: case Cond::ULE: return Cmp(loff <= roff);
    case Cond::GE: case Cond::OGE: case Cond::UGE: return Cmp(loff >= roff);
  }
  llvm_unreachable("invalid condition code");
}

// -----------------------------------------------------------------------------
static Lattice Compare(
    Global *lg,
    int64_t loff,
    Global *rg,
    int64_t roff,
    Cond cc,
    Type ty)
{
  auto Flag = [ty](bool value) {
    return MakeBoolean(value, ty);
  };
  auto Cmp = [ty, &Flag, lg, rg](bool value) {
    return (lg == rg) ? Flag(value) : Lattice::Undefined();
  };

  switch (lg->GetKind()) {
    case Global::Kind::EXTERN:
    case Global::Kind::FUNC:
    case Global::Kind::BLOCK: {
      // Ordering is undefined for these types - equality is allowed.
      bool equal = lg == rg && loff == roff;
      switch (cc) {
        case Cond::EQ: case Cond::OEQ: case Cond::UEQ: return Flag(equal);
        case Cond::NE: case Cond::ONE: case Cond::UNE: return Flag(!equal);
        default: return Lattice::Overdefined();
      }
      llvm_unreachable("invalid condition code");
    }
    case Global::Kind::ATOM: {
      switch (rg->GetKind()) {
        case Global::Kind::EXTERN:
        case Global::Kind::FUNC:
        case Global::Kind::BLOCK: {
          switch (cc) {
            case Cond::EQ: case Cond::OEQ: case Cond::UEQ: return Flag(false);
            case Cond::NE: case Cond::ONE: case Cond::UNE: return Flag(true);
            default: return Lattice::Overdefined();
          }
          llvm_unreachable("invalid condition code");
        }
        case Global::Kind::ATOM: {
          auto *al = static_cast<Atom *>(lg);
          auto *ar = static_cast<Atom *>(rg);
          bool equal = lg == rg && loff == roff;
          switch (cc) {
            case Cond::EQ: case Cond::OEQ: case Cond::UEQ: return Flag(equal);
            case Cond::NE: case Cond::ONE: case Cond::UNE: return Flag(!equal);
            case Cond::LT: case Cond::OLT: case Cond::ULT: return Cmp(loff < roff);
            case Cond::GT: case Cond::OGT: case Cond::UGT: return Cmp(loff > roff);
            case Cond::LE: case Cond::OLE: case Cond::ULE: return Cmp(loff <= roff);
            case Cond::GE: case Cond::OGE: case Cond::UGE: return Cmp(loff >= roff);
          }
          llvm_unreachable("invalid condition code");
        }
      }
      llvm_unreachable("invalid global kind");
    }
  }
  llvm_unreachable("invalid global kind");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(CmpInst *inst, Lattice &lhs, Lattice &rhs)
{
  Cond cc = inst->GetCC();
  Type ty = inst->GetType();

  auto Unequal = [ty, cc] {
    switch (cc) {
      case Cond::EQ: case Cond::OEQ: case Cond::UEQ:
        return MakeBoolean(false, ty);
      case Cond::NE: case Cond::ONE: case Cond::UNE:
        return MakeBoolean(true, ty);
      default:
        return Lattice::Undefined();
    }
    llvm_unreachable("invalid condition code");
  };

  auto IntOrder = [ty, cc] (bool Lower) {
    switch (cc) {
      case Cond::EQ: case Cond::OEQ: case Cond::UEQ:
        return MakeBoolean(false, ty);
      case Cond::NE: case Cond::ONE: case Cond::UNE:
        return MakeBoolean(true, ty);
      case Cond::LE: case Cond::OLE: case Cond::ULE:
      case Cond::LT: case Cond::OLT: case Cond::ULT:
        return MakeBoolean(Lower, ty);
      case Cond::GE: case Cond::OGE: case Cond::UGE:
      case Cond::GT: case Cond::OGT: case Cond::UGT:
        return MakeBoolean(!Lower, ty);
    }
    llvm_unreachable("invalid condition code");
  };

  switch (lhs.GetKind()) {
    case Lattice::Kind::UNKNOWN:
    case Lattice::Kind::OVERDEFINED: {
      llvm_unreachable("value cannot be compared");
    }
    case Lattice::Kind::UNDEFINED: {
      return Lattice::Undefined();
    }
    case Lattice::Kind::FLOAT: {
      switch (rhs.GetKind()) {
        case Lattice::Kind::UNKNOWN:
        case Lattice::Kind::OVERDEFINED:
        case Lattice::Kind::INT:
        case Lattice::Kind::GLOBAL:
        case Lattice::Kind::FRAME: {
          llvm_unreachable("value cannot be compared");
        }
        case Lattice::Kind::UNDEFINED: {
          return Lattice::Undefined();
        }
        case Lattice::Kind::FLOAT: {
          return Compare(lhs.GetFloat(), rhs.GetFloat(), cc, ty);
        }
      }
      llvm_unreachable("invalid rhs kind");
    }
    case Lattice::Kind::INT: {
      switch (rhs.GetKind()) {
        case Lattice::Kind::FLOAT:
        case Lattice::Kind::UNKNOWN:
        case Lattice::Kind::OVERDEFINED: {
          llvm_unreachable("value cannot be compared");
        }
        case Lattice::Kind::UNDEFINED: {
          return Lattice::Undefined();
        }
        case Lattice::Kind::FRAME:
        case Lattice::Kind::GLOBAL: {
          return IntOrder(true);
        }
        case Lattice::Kind::INT: {
          return MakeBoolean(Compare(lhs.GetInt(), rhs.GetInt(), cc), ty);
        }
      }
      llvm_unreachable("invalid rhs kind");
    }
    case Lattice::Kind::FRAME: {
      switch (rhs.GetKind()) {
        case Lattice::Kind::FLOAT:
        case Lattice::Kind::UNKNOWN:
        case Lattice::Kind::OVERDEFINED: {
          llvm_unreachable("value cannot be compared");
        }
        case Lattice::Kind::UNDEFINED: {
          return Lattice::Undefined();
        }
        case Lattice::Kind::GLOBAL: {
          return Unequal();
        }
        case Lattice::Kind::INT: {
          return IntOrder(false);
        }
        case Lattice::Kind::FRAME: {
          return Compare(
              lhs.GetFrameObject(),
              lhs.GetFrameOffset(),
              rhs.GetFrameObject(),
              rhs.GetFrameOffset(),
              cc,
              ty
          );
        }
      }
      llvm_unreachable("invalid rhs kind");
    }
    case Lattice::Kind::GLOBAL: {
      switch (rhs.GetKind()) {
        case Lattice::Kind::UNKNOWN:
        case Lattice::Kind::OVERDEFINED:
        case Lattice::Kind::FLOAT: {
          llvm_unreachable("value cannot be compared");
        }
        case Lattice::Kind::UNDEFINED: {
          return Lattice::Undefined();
        }
        case Lattice::Kind::FRAME: {
          return Unequal();
        }
        case Lattice::Kind::GLOBAL: {
          return Compare(
              lhs.GetGlobalSymbol(),
              lhs.GetGlobalOffset(),
              rhs.GetGlobalSymbol(),
              rhs.GetGlobalOffset(),
              cc,
              ty
          );
        }
        case Lattice::Kind::INT: {
          return IntOrder(false);
        }
      }
      llvm_unreachable("invalid rhs kind");
    }
  }
  llvm_unreachable("invalid lhs kind");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(UDivInst *inst, Lattice &lhs, Lattice &rhs)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128: {
      auto bitWidth = GetSize(ty) * 8;
      if (auto il = lhs.AsInt()) {
        if (auto ir = rhs.AsInt()) {
          if (*ir != 0) {
            return Lattice::CreateInteger(il->udiv(*ir));
          }
          return Lattice::Undefined();
        }
      }
      llvm_unreachable("cannot divide non-integers");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      if (auto fl = lhs.AsFloat()) {
        if (auto fr = rhs.AsFloat()) {
          return Lattice::CreateFloat(*fl / *fr);
        }
      }
      llvm_unreachable("cannot multiply non-floats");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SDivInst *inst, Lattice &lhs, Lattice &rhs)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128: {
      auto bitWidth = GetSize(ty) * 8;
      if (auto il = lhs.AsInt()) {
        if (auto ir = rhs.AsInt()) {
          if (*ir != 0) {
            return Lattice::CreateInteger(il->sdiv(*ir));
          }
          return Lattice::Undefined();
        }
      }
      llvm_unreachable("cannot divide non-integers");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      if (auto fl = lhs.AsFloat()) {
        if (auto fr = rhs.AsFloat()) {
          return Lattice::CreateFloat(*fl / *fr);
        }
      }
      llvm_unreachable("cannot multiply non-floats");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(URemInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("URemInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SRemInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("SRemInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(MulInst *inst, Lattice &lhs, Lattice &rhs)
{
  switch (auto ty = inst->GetType()) {
    case Type::I8:
    case Type::I16:
    case Type::I32:
    case Type::I64:
    case Type::I128: {
      auto bitWidth = GetSize(ty) * 8;
      if (auto il = lhs.AsInt()) {
        if (auto ir = rhs.AsInt()) {
          return Lattice::CreateInteger(*il * *ir);
        }
      }
      llvm_unreachable("cannot multiply non-integers");
    }
    case Type::F32: case Type::F64: case Type::F80: {
      if (auto fl = lhs.AsFloat()) {
        if (auto fr = rhs.AsFloat()) {
          return Lattice::CreateFloat(*fl * *fr);
        }
      }
      llvm_unreachable("cannot multiply non-floats");
    }
  }
  llvm_unreachable("invalid type");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(RotlInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("RotlInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(RotrInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("RotrInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SllInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("SllInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SraInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("SraInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(SrlInst *inst, Lattice &lhs, Lattice &rhs)
{
  llvm_unreachable("SrlInst");
}

// -----------------------------------------------------------------------------
Lattice SCCPEval::Eval(Bitwise kind, Type ty, Lattice &lhs, Lattice &rhs)
{
  if (auto si = rhs.AsInt()) {
    switch (ty) {
      case Type::I8:
      case Type::I16:
      case Type::I32:
      case Type::I64:
      case Type::I128: {
        switch (lhs.GetKind()) {
          case Lattice::Kind::UNKNOWN:
          case Lattice::Kind::OVERDEFINED:
          case Lattice::Kind::UNDEFINED: {
            return lhs;
          }
          case Lattice::Kind::INT: {
            auto i = lhs.GetInt();
            switch (kind) {
              case Bitwise::SRL:  return Lattice::CreateInteger(i.lshr(*si));
              case Bitwise::SRA:  return Lattice::CreateInteger(i.ashr(*si));
              case Bitwise::SLL:  return Lattice::CreateInteger(i.shl(*si));
              case Bitwise::ROTL: return Lattice::CreateInteger(i.rotl(*si));
            }
            llvm_unreachable("not a shift instruction");
          }
          case Lattice::Kind::GLOBAL:
          case Lattice::Kind::FRAME: {
            return (*si == 0) ? lhs : Lattice::Overdefined();
          }
          case Lattice::Kind::FLOAT: {
            llvm_unreachable("cannot shift floats");
          }
        }
        llvm_unreachable("invalid shift argument");
      }
      case Type::F32:
      case Type::F64:
      case Type::F80: {
        llvm_unreachable("cannot shift floats");
      }
    }
  }
  llvm_unreachable("invalid shift amount");
}
