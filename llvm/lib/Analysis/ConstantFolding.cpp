//===-- ConstantFolding.cpp - Analyze constant folding possibilities ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This family of functions determines the possibility of performing constant
// folding.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Intrinsics.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Support/MathExtras.h"
#include <cerrno>
#include <cmath>
using namespace llvm;

/// ConstantFoldInstruction - Attempt to constant fold the specified
/// instruction.  If successful, the constant result is returned, if not, null
/// is returned.  Note that this function can only fail when attempting to fold
/// instructions like loads and stores, which have no constant expression form.
///
Constant *llvm::ConstantFoldInstruction(Instruction *I, const TargetData *TD) {
  if (PHINode *PN = dyn_cast<PHINode>(I)) {
    if (PN->getNumIncomingValues() == 0)
      return Constant::getNullValue(PN->getType());

    Constant *Result = dyn_cast<Constant>(PN->getIncomingValue(0));
    if (Result == 0) return 0;

    // Handle PHI nodes specially here...
    for (unsigned i = 1, e = PN->getNumIncomingValues(); i != e; ++i)
      if (PN->getIncomingValue(i) != Result && PN->getIncomingValue(i) != PN)
        return 0;   // Not all the same incoming constants...

    // If we reach here, all incoming values are the same constant.
    return Result;
  }

  // Scan the operand list, checking to see if they are all constants, if so,
  // hand off to ConstantFoldInstOperands.
  SmallVector<Constant*, 8> Ops;
  for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i)
    if (Constant *Op = dyn_cast<Constant>(I->getOperand(i)))
      Ops.push_back(Op);
    else
      return 0;  // All operands not constant!

  return ConstantFoldInstOperands(I, &Ops[0], Ops.size());
}

/// ConstantFoldInstOperands - Attempt to constant fold an instruction with the
/// specified opcode and operands.  If successful, the constant result is
/// returned, if not, null is returned.  Note that this function can fail when
/// attempting to fold instructions like loads and stores, which have no
/// constant expression form.
///
Constant *llvm::ConstantFoldInstOperands(const Instruction* I, 
                                         Constant** Ops, unsigned NumOps,
                                         const TargetData *TD) {
  unsigned Opc = I->getOpcode();
  const Type *DestTy = I->getType();

  // Handle easy binops first
  if (isa<BinaryOperator>(I))
    return ConstantExpr::get(Opc, Ops[0], Ops[1]);
  
  switch (Opc) {
  default: return 0;
  case Instruction::Call:
    if (Function *F = dyn_cast<Function>(Ops[0]))
      if (canConstantFoldCallTo(F))
        return ConstantFoldCall(F, Ops+1, NumOps);
    return 0;
  case Instruction::ICmp:
  case Instruction::FCmp:
    return ConstantExpr::getCompare(cast<CmpInst>(I)->getPredicate(), Ops[0], 
                                    Ops[1]);
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    return ConstantExpr::get(Opc, Ops[0], Ops[1]);
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
    return ConstantExpr::getCast(Opc, Ops[0], DestTy);
  case Instruction::Select:
    return ConstantExpr::getSelect(Ops[0], Ops[1], Ops[2]);
  case Instruction::ExtractElement:
    return ConstantExpr::getExtractElement(Ops[0], Ops[1]);
  case Instruction::InsertElement:
    return ConstantExpr::getInsertElement(Ops[0], Ops[1], Ops[2]);
  case Instruction::ShuffleVector:
    return ConstantExpr::getShuffleVector(Ops[0], Ops[1], Ops[2]);
  case Instruction::GetElementPtr:
    return ConstantExpr::getGetElementPtr(Ops[0],
                                          std::vector<Constant*>(Ops+1, 
                                                                 Ops+NumOps));
  }
}

/// ConstantFoldLoadThroughGEPConstantExpr - Given a constant and a
/// getelementptr constantexpr, return the constant value being addressed by the
/// constant expression, or null if something is funny and we can't decide.
Constant *llvm::ConstantFoldLoadThroughGEPConstantExpr(Constant *C, 
                                                       ConstantExpr *CE) {
  if (CE->getOperand(1) != Constant::getNullValue(CE->getOperand(1)->getType()))
    return 0;  // Do not allow stepping over the value!
  
  // Loop over all of the operands, tracking down which value we are
  // addressing...
  gep_type_iterator I = gep_type_begin(CE), E = gep_type_end(CE);
  for (++I; I != E; ++I)
    if (const StructType *STy = dyn_cast<StructType>(*I)) {
      ConstantInt *CU = cast<ConstantInt>(I.getOperand());
      assert(CU->getZExtValue() < STy->getNumElements() &&
             "Struct index out of range!");
      unsigned El = (unsigned)CU->getZExtValue();
      if (ConstantStruct *CS = dyn_cast<ConstantStruct>(C)) {
        C = CS->getOperand(El);
      } else if (isa<ConstantAggregateZero>(C)) {
        C = Constant::getNullValue(STy->getElementType(El));
      } else if (isa<UndefValue>(C)) {
        C = UndefValue::get(STy->getElementType(El));
      } else {
        return 0;
      }
    } else if (ConstantInt *CI = dyn_cast<ConstantInt>(I.getOperand())) {
      if (const ArrayType *ATy = dyn_cast<ArrayType>(*I)) {
        if (CI->getZExtValue() >= ATy->getNumElements())
         return 0;
        if (ConstantArray *CA = dyn_cast<ConstantArray>(C))
          C = CA->getOperand(CI->getZExtValue());
        else if (isa<ConstantAggregateZero>(C))
          C = Constant::getNullValue(ATy->getElementType());
        else if (isa<UndefValue>(C))
          C = UndefValue::get(ATy->getElementType());
        else
          return 0;
      } else if (const PackedType *PTy = dyn_cast<PackedType>(*I)) {
        if (CI->getZExtValue() >= PTy->getNumElements())
          return 0;
        if (ConstantPacked *CP = dyn_cast<ConstantPacked>(C))
          C = CP->getOperand(CI->getZExtValue());
        else if (isa<ConstantAggregateZero>(C))
          C = Constant::getNullValue(PTy->getElementType());
        else if (isa<UndefValue>(C))
          C = UndefValue::get(PTy->getElementType());
        else
          return 0;
      } else {
        return 0;
      }
    } else {
      return 0;
    }
  return C;
}


//===----------------------------------------------------------------------===//
//  Constant Folding for Calls
//

/// canConstantFoldCallTo - Return true if its even possible to fold a call to
/// the specified function.
bool
llvm::canConstantFoldCallTo(Function *F) {
  const std::string &Name = F->getName();

  switch (F->getIntrinsicID()) {
  case Intrinsic::sqrt_f32:
  case Intrinsic::sqrt_f64:
  case Intrinsic::bswap_i16:
  case Intrinsic::bswap_i32:
  case Intrinsic::bswap_i64:
  case Intrinsic::powi_f32:
  case Intrinsic::powi_f64:
  // FIXME: these should be constant folded as well
  //case Intrinsic::ctpop_i8:
  //case Intrinsic::ctpop_i16:
  //case Intrinsic::ctpop_i32:
  //case Intrinsic::ctpop_i64:
  //case Intrinsic::ctlz_i8:
  //case Intrinsic::ctlz_i16:
  //case Intrinsic::ctlz_i32:
  //case Intrinsic::ctlz_i64:
  //case Intrinsic::cttz_i8:
  //case Intrinsic::cttz_i16:
  //case Intrinsic::cttz_i32:
  //case Intrinsic::cttz_i64:
    return true;
  default: break;
  }

  switch (Name[0])
  {
    case 'a':
      return Name == "acos" || Name == "asin" || Name == "atan" ||
             Name == "atan2";
    case 'c':
      return Name == "ceil" || Name == "cos" || Name == "cosf" ||
             Name == "cosh";
    case 'e':
      return Name == "exp";
    case 'f':
      return Name == "fabs" || Name == "fmod" || Name == "floor";
    case 'l':
      return Name == "log" || Name == "log10";
    case 'p':
      return Name == "pow";
    case 's':
      return Name == "sin" || Name == "sinh" || 
             Name == "sqrt" || Name == "sqrtf";
    case 't':
      return Name == "tan" || Name == "tanh";
    default:
      return false;
  }
}

static Constant *ConstantFoldFP(double (*NativeFP)(double), double V, 
                                const Type *Ty) {
  errno = 0;
  V = NativeFP(V);
  if (errno == 0)
    return ConstantFP::get(Ty, V);
  errno = 0;
  return 0;
}

/// ConstantFoldCall - Attempt to constant fold a call to the specified function
/// with the specified arguments, returning null if unsuccessful.
Constant *
llvm::ConstantFoldCall(Function *F, Constant** Operands, unsigned NumOperands) {
  const std::string &Name = F->getName();
  const Type *Ty = F->getReturnType();

  if (NumOperands == 1) {
    if (ConstantFP *Op = dyn_cast<ConstantFP>(Operands[0])) {
      double V = Op->getValue();
      switch (Name[0])
      {
        case 'a':
          if (Name == "acos")
            return ConstantFoldFP(acos, V, Ty);
          else if (Name == "asin")
            return ConstantFoldFP(asin, V, Ty);
          else if (Name == "atan")
            return ConstantFP::get(Ty, atan(V));
          break;
        case 'c':
          if (Name == "ceil")
            return ConstantFoldFP(ceil, V, Ty);
          else if (Name == "cos")
            return ConstantFP::get(Ty, cos(V));
          else if (Name == "cosh")
            return ConstantFP::get(Ty, cosh(V));
          break;
        case 'e':
          if (Name == "exp")
            return ConstantFP::get(Ty, exp(V));
          break;
        case 'f':
          if (Name == "fabs")
            return ConstantFP::get(Ty, fabs(V));
          else if (Name == "floor")
            return ConstantFoldFP(floor, V, Ty);
          break;
        case 'l':
          if (Name == "log" && V > 0)
            return ConstantFP::get(Ty, log(V));
          else if (Name == "log10" && V > 0)
            return ConstantFoldFP(log10, V, Ty);
          else if (Name == "llvm.sqrt.f32" || Name == "llvm.sqrt.f64") {
            if (V >= -0.0)
              return ConstantFP::get(Ty, sqrt(V));
            else // Undefined
              return ConstantFP::get(Ty, 0.0);
          }
          break;
        case 's':
          if (Name == "sin")
            return ConstantFP::get(Ty, sin(V));
          else if (Name == "sinh")
            return ConstantFP::get(Ty, sinh(V));
          else if (Name == "sqrt" && V >= 0)
            return ConstantFP::get(Ty, sqrt(V));
          else if (Name == "sqrtf" && V >= 0)
            return ConstantFP::get(Ty, sqrt((float)V));
          break;
        case 't':
          if (Name == "tan")
            return ConstantFP::get(Ty, tan(V));
          else if (Name == "tanh")
            return ConstantFP::get(Ty, tanh(V));
          break;
        default:
          break;
      }
    } else if (ConstantInt *Op = dyn_cast<ConstantInt>(Operands[0])) {
      uint64_t V = Op->getZExtValue();
      if (Name == "llvm.bswap.i16")
        return ConstantInt::get(Ty, ByteSwap_16(V));
      else if (Name == "llvm.bswap.i32")
        return ConstantInt::get(Ty, ByteSwap_32(V));
      else if (Name == "llvm.bswap.i64")
        return ConstantInt::get(Ty, ByteSwap_64(V));
    }
  } else if (NumOperands == 2) {
    if (ConstantFP *Op1 = dyn_cast<ConstantFP>(Operands[0])) {
      double Op1V = Op1->getValue();
      if (ConstantFP *Op2 = dyn_cast<ConstantFP>(Operands[1])) {
        double Op2V = Op2->getValue();

        if (Name == "pow") {
          errno = 0;
          double V = pow(Op1V, Op2V);
          if (errno == 0)
            return ConstantFP::get(Ty, V);
        } else if (Name == "fmod") {
          errno = 0;
          double V = fmod(Op1V, Op2V);
          if (errno == 0)
            return ConstantFP::get(Ty, V);
        } else if (Name == "atan2") {
          return ConstantFP::get(Ty, atan2(Op1V,Op2V));
        }
      } else if (ConstantInt *Op2C = dyn_cast<ConstantInt>(Operands[1])) {
        if (Name == "llvm.powi.f32") {
          return ConstantFP::get(Ty, std::pow((float)Op1V,
                                              (int)Op2C->getZExtValue()));
        } else if (Name == "llvm.powi.f64") {
          return ConstantFP::get(Ty, std::pow((double)Op1V,
                                              (int)Op2C->getZExtValue()));
        }
      }
    }
  }
  return 0;
}

