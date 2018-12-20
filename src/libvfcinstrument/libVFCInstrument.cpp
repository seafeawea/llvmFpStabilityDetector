/********************************************************************************
 *                                                                              *
 *  This file is part of Verificarlo. *
 *                                                                              *
 *  Copyright (c) 2015 * Universite de Versailles St-Quentin-en-Yvelines * CMLA,
 *Ecole Normale Superieure de Cachan                                 *
 *                                                                              *
 *  Verificarlo is free software: you can redistribute it and/or modify * it
 *under the terms of the GNU General Public License as published by        * the
 *Free Software Foundation, either version 3 of the License, or           * (at
 *your option) any later version.                                         *
 *                                                                              *
 *  Verificarlo is distributed in the hope that it will be useful, * but WITHOUT
 *ANY WARRANTY; without even the implied warranty of              *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the * GNU General
 *Public License for more details.                                *
 *                                                                              *
 *  You should have received a copy of the GNU General Public License * along
 *with Verificarlo.  If not, see <http://www.gnu.org/licenses/>.        *
 *                                                                              *
 ********************************************************************************/

#include "../../config.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <fstream>
#include <map>
#include <set>
#include <string>

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 6
#define CREATE_CALL(func, op1) (Builder.CreateCall(func, op1, ""))
#define CREATE_CALL2(func, op1, op2) (Builder.CreateCall2(func, op1, op2, ""))
#define CREATE_CALL3(func, op1, op2, op3) \
  (Builder.CreateCall3(func, op1, op2, op3, ""))
#define CREATE_CALL4(func, op1, op2, op3, op4) \
  (Builder.CreateCall4(func, op1, op2, op3, op4, ""))
#define CREATE_CALL5(func, op1, op2, op3, op4, op5) \
  (Builder.CreateCall5(func, op1, op2, op3, op4, op5, ""))

#define CREATE_STRUCT_GEP(t, i, p) (Builder.CreateStructGEP(i, p))
#else
#define CREATE_CALL(func, op1) (Builder.CreateCall(func, {op1}, ""))
#define CREATE_CALL2(func, op1, op2) (Builder.CreateCall(func, {op1, op2}, ""))
#define CREATE_CALL3(func, op1, op2, op3) \
  (Builder.CreateCall(func, {op1, op2, op3}, ""))
#define CREATE_CALL4(func, op1, op2, op3, op4) \
  (Builder.CreateCall(func, {op1, op2, op3, op4}, ""))
#define CREATE_CALL5(func, op1, op2, op3, op4) \
  (Builder.CreateCall(func, {op1, op2, op3, op4, op5}, ""))
#define CREATE_STRUCT_GEP(t, i, p) (Builder.CreateStructGEP(t, i, p, ""))
#endif

using namespace llvm;
// VfclibInst pass command line arguments
static cl::opt<std::string> VfclibInstFunction(
    "vfclibinst-function", cl::desc("Only instrument given FunctionName"),
    cl::value_desc("FunctionName"), cl::init(""));

static cl::opt<std::string> VfclibInstFunctionFile(
    "vfclibinst-function-file",
    cl::desc("Instrument functions in file FunctionNameFile "),
    cl::value_desc("FunctionsNameFile"), cl::init(""));

static cl::opt<bool> VfclibInstVerbose("vfclibinst-verbose",
                                       cl::desc("Activate verbose mode"),
                                       cl::value_desc("Verbose"),
                                       cl::init(false));

namespace {
// Define an enum type to classify the floating points operations
// that are instrumented by verificarlo

enum Fops { FOP_ADD, FOP_SUB, FOP_MUL, FOP_DIV, FOP_IGNORE };

// Each instruction can be translated to a string representation

std::string Fops2str[] = {"add", "sub", "mul", "div", "ignore"};

struct VfclibInst : public ModulePass {
  static char ID;

  std::map<std::string, int> SelectedFunctionSet;

  VfclibInst() : ModulePass(ID) {
    int funcId = 0;
    if (not VfclibInstFunctionFile.empty()) {
      std::string line;
      std::ifstream loopstream(VfclibInstFunctionFile.c_str());
      if (loopstream.is_open()) {
        while (std::getline(loopstream, line)) {
          SelectedFunctionSet.insert(
              std::pair<std::string, int>(line, funcId++));
        }
        loopstream.close();
      } else {
        errs() << "Cannot open " << VfclibInstFunctionFile << "\n";
        assert(0);
      }
    } else if (not VfclibInstFunction.empty()) {
      SelectedFunctionSet.insert(
          std::pair<std::string, int>(VfclibInstFunction, funcId++));
    }

    if (SelectedFunctionSet.empty()) {
      errs() << "Please give at least a function for processing\n";
      assert(0);
    }
  }

  StructType *getMCAInterfaceType(IRBuilder<> &Builder) {
    // Verificarlo instrumentation calls the mca backend using
    // a vtable implemented as a structure.
    //
    // Here we declare the struct type corresponding to the
    // mca_interface_t defined in ../vfcwrapper/vfcwrapper.h
    //
    // Only the functions instrumented are declared. The last
    // three functions are user called functions and are not
    // needed here.

    SmallVector<Type *, 7> floatArgs, doubleArgs;
    floatArgs.push_back(Builder.getInt8PtrTy());
    floatArgs.push_back(Builder.getFloatTy());
    floatArgs.push_back(Builder.getFloatTy());
    floatArgs.push_back(Builder.getInt32Ty());
    floatArgs.push_back(Builder.getInt32Ty());
    floatArgs.push_back(Builder.getInt8PtrTy());
    floatArgs.push_back(Builder.getInt8PtrTy());
    floatArgs.push_back(Builder.getInt8PtrTy());

    doubleArgs.push_back(Builder.getInt8PtrTy());
    doubleArgs.push_back(Builder.getDoubleTy());
    doubleArgs.push_back(Builder.getDoubleTy());
    doubleArgs.push_back(Builder.getInt32Ty());
    doubleArgs.push_back(Builder.getInt32Ty());
    doubleArgs.push_back(Builder.getInt8PtrTy());
    doubleArgs.push_back(Builder.getInt8PtrTy());
    doubleArgs.push_back(Builder.getInt8PtrTy());

    PointerType *floatInstFun = PointerType::getUnqual(
        FunctionType::get(Builder.getFloatTy(), floatArgs, false));
    PointerType *doubleInstFun = PointerType::getUnqual(
        FunctionType::get(Builder.getDoubleTy(), doubleArgs, false));

    return StructType::get(

        floatInstFun, floatInstFun, floatInstFun, floatInstFun,

        doubleInstFun, doubleInstFun, doubleInstFun, doubleInstFun,

        (void *)0);
  }

  bool runOnModule(Module &M) {
    bool modified = false;

    // Find the list of functions to instrument
    // Instrumentation adds stubs to mcalib function which we
    // never want to instrument.  Therefore it is important to
    // first find all the functions of interest before
    // starting instrumentation.

    std::vector<Function *> functions;
    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
      const bool is_in =
          SelectedFunctionSet.find(F->getName()) != SelectedFunctionSet.end();
      if (SelectedFunctionSet.empty() || is_in) {
        functions.push_back(&*F);
      }

      if (F->getName().str() == "main") {
        runOnMainFunction(M, *F);
      }
    }

    // Do the instrumentation on selected functions
    for (std::vector<Function *>::iterator F = functions.begin();
         F != functions.end(); ++F) {
      // errs() << "we are processing function " << (*F)->getName() << '\n';
      if ((*F)->isDeclaration()) continue;
      modified |= runOnFunction(M, **F);
    }
    // runOnModule must return true if the pass modifies the IR
    return modified;
  }

  bool runOnMainFunction(Module &M, Function &F) {
    BasicBlock &firstBlock = F.getEntryBlock();
    LLVMContext &Context = M.getContext();
    IRBuilder<> Builder(Context);
    Instruction &I = firstBlock.front();
    Builder.SetInsertPoint(&I);

    Constant *hookFunc;
    // Function *hook;
    SmallVector<Type *, 2> arg_vector;
    arg_vector.push_back(Builder.getInt8PtrTy());
    arg_vector.push_back(Builder.getInt32Ty());
    hookFunc = M.getOrInsertFunction(
        "initNewFunction",
        FunctionType::get(Builder.getVoidTy(), arg_vector, false));
    // hook= cast<Function>(hookFunc);

    for (std::map<std::string, int>::iterator it = SelectedFunctionSet.begin();
         it != SelectedFunctionSet.end(); it++) {
      if (it->first != "") {
        Instruction *newInst = CREATE_CALL2(
            hookFunc,
            Builder.CreateGlobalStringPtr(llvm::StringRef(it->first.c_str())),
            Builder.getInt32(it->second));
        //    Builder.SetInsertPoint(newInst);
        // if (newInst->getParent() != NULL) newInst->removeFromParent();
        // firstBlock.getFirstInsertionPt()
      }
    }

    return true;
  }

  bool runOnFunction(Module &M, Function &F) {
    created_all_global_str.clear();
    if (VfclibInstVerbose) {
      errs() << "In Function: ";
      errs().write_escaped(F.getName()) << '\n';
    }

    bool modified = false;
    BasicBlock *BBEntry = &F.getEntryBlock();
    runOnEntryBlock(M, *BBEntry);

    int32_t func_id = SelectedFunctionSet[F.getName()];

    IRBuilder<> Builder(M.getContext());
    Builder.SetInsertPoint(&(F.getEntryBlock().front()));
    SmallVector<Type *, 1> arg_vector;
    arg_vector.push_back(Builder.getInt32Ty());
    Constant *hookFunc = M.getOrInsertFunction(
        "checkAndPrintInfo",
        FunctionType::get(Builder.getVoidTy(), arg_vector, false));
    CREATE_CALL(hookFunc, Builder.getInt32(func_id));

    Instruction *create_map_inst = Builder.CreateCall(M.getOrInsertFunction(
        "createFpNodeMap", FunctionType::get(Builder.getInt8PtrTy(), false)));

    for (Function::iterator bi = F.begin(), be = F.end(); bi != be; ++bi) {
      relaceFloatOPName(M, *bi, F, func_id);
    }

    for (Function::iterator bi = F.begin(), be = F.end(); bi != be; ++bi) {
      modified |= runOnBasicBlock(M, *bi, F, func_id, create_map_inst);
    }
    return modified;
  }

  std::map<std::string, Value *> created_all_global_str;
  Value *createGlobalStringPtrIfNotExist(StringRef sf, IRBuilder<> &Builder) {
    std::string s = sf.str();
    if (created_all_global_str.find(s) == created_all_global_str.end()) {
      Value *global_string = Builder.CreateGlobalStringPtr(sf);
      created_all_global_str.insert(
          std::pair<std::string, Value *>(s, global_string));
    }
    return created_all_global_str[s];
  }

  StringRef getFloatArgName(Use &use_arg) {
    StringRef name_arg;
    if (isa<Instruction>(use_arg)) {
      Instruction *I_arg = cast<Instruction>(use_arg);
      switch (I_arg->getOpcode()) {
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        case Instruction::FDiv:
        case Instruction::Call:
          name_arg = I_arg->getName();
          break;
        case Instruction::Load:
          if (I_arg->getMetadata("fp") != NULL) {
            name_arg = cast<MDString>(I_arg->getMetadata("fp")->getOperand(0))
                           ->getString();
          } else {
            assert(0 && "Load Instrution don't have fp info");
          }

          break;
        case Instruction::Store:
        default:
          errs() << "I_arg1.opCode " << I_arg->getOpcodeName() << '\n';
          assert(0 && "Fop Instrution have invald argument");
          break;
      };
    } else if (cast<Value>(use_arg)->getValueID() == Value::ConstantFPVal) {
      name_arg = StringRef("__const__value__");
    } else if (cast<Value>(use_arg)->getValueID() == Value::ArgumentVal) {
      name_arg = use_arg->getName();
    } else {
      assert(0 && "Fop Instrution have invald argument");
    }
    return name_arg;
  }

  Instruction *replaceWithMCACall(Module &M, BasicBlock &B, Instruction *I,
                                  Fops opCode, int32_t func_id, int32_t line,
                                  Instruction *create_map_inst) {
    LLVMContext &Context = M.getContext();
    IRBuilder<> Builder(Context);
    StructType *mca_interface_type = getMCAInterfaceType(Builder);

    Type *retType = I->getType();
    Type *opType = I->getOperand(0)->getType();
    std::string opName = Fops2str[opCode];

    std::string baseTypeName = "";
    std::string vectorName = "";
    Type *baseType = opType;

    // Check for vector types
    if (opType->isVectorTy()) {
      VectorType *t = static_cast<VectorType *>(opType);
      baseType = t->getElementType();
      unsigned size = t->getNumElements();

      if (size == 2) {
        vectorName = "2x";
      } else if (size == 4) {
        vectorName = "4x";
      } else {
        errs() << "Unsuported vector size: " << size << "\n";
        assert(0);
      }
    }

    // Check the type of the operation
    if (baseType->isDoubleTy()) {
      baseTypeName = "double";
    } else if (baseType->isFloatTy()) {
      baseTypeName = "float";
    } else {
      I->print(errs());
      errs() << "Unsupported operand type: " << *opType << "\n";
      // assert(0);
      return NULL;
    }

    // For vector types, helper functions in vfcwrapper are called
    if (vectorName != "") {
      std::string mcaFunctionName = "_" + vectorName + baseTypeName + opName;

      Constant *hookFunc = M.getOrInsertFunction(mcaFunctionName, retType,
                                                 opType, opType, (Type *)0);

      // For vector types we call directly a hardcoded helper function
      // no need to go through the vtable at this stage.
      Instruction *newInst =
          CREATE_CALL4(hookFunc, I->getOperand(0), I->getOperand(1),
                       Builder.getInt32(func_id), Builder.getInt32(line));
      /*
Instruction *newInst = CREATE_CALL5(hookFunc,
      I->getOperand(0),
      I->getOperand(1),
      Builder.getInt32((func_id<<24)+line),
      Builder.CreateGlobalStringPtr(I->getOperand(0)->getName()),
      Builder.CreateGlobalStringPtr(I->getOperand(1)->getName()));
      */

      return newInst;
    }
    // For scalar types, we go directly through the struct of pointer function
    else {
      // We use a builder adding instructions before the
      // instruction to replace
      Builder.SetInsertPoint(I);

      // Get a pointer to the global vtable
      // The vtable is accessed through the global structure
      // _vfc_current_mca_interface of type mca_interface_t which is
      // declared in ../vfcwrapper/vfcwrapper.c

      Constant *current_mca_interface =
          M.getOrInsertGlobal("_vfc_current_mca_interface", mca_interface_type);

      // current_mca_interface->print(errs());

      // Compute the position of the required member fct pointer
      // opCodes are ordered in the same order than the struct members :-)
      // There are 4 float members followed by 4 double members.
      int fct_position = opCode;
      if (baseTypeName == "double") fct_position += 4;
      // Dereference the member at fct_position
      Value *arg_ptr = CREATE_STRUCT_GEP(mca_interface_type,
                                         current_mca_interface, fct_position);
      Value *fct_ptr = Builder.CreateLoad(arg_ptr, "");

      // Use &use_arg1 = I->getOperandUse(0);
      // StringRef name_arg1 = getFloatArgName(use_arg1);
      Value *value_arg1 = I->getOperand(0);
      StringRef name_arg1 = value_arg1->getName();
      // Use &use_arg2 = I->getOperandUse(1);
      // StringRef name_arg2 = getFloatArgName(use_arg2);
      Value *value_arg2 = I->getOperand(1);
      StringRef name_arg2 = value_arg2->getName();

      Value *value_name_arg1 =
          createGlobalStringPtrIfNotExist(name_arg1, Builder);
      Value *value_name_arg2 =
          createGlobalStringPtrIfNotExist(name_arg2, Builder);

      Value *result_name_arg =
          createGlobalStringPtrIfNotExist(I->getName(), Builder);

      SmallVector<Value *, 8> all_arg;
      all_arg.push_back(create_map_inst);
      all_arg.push_back(I->getOperand(0));
      all_arg.push_back(I->getOperand(1));
      all_arg.push_back(Builder.getInt32(func_id));
      all_arg.push_back(Builder.getInt32(line));
      all_arg.push_back(value_name_arg1);
      all_arg.push_back(value_name_arg2);
      all_arg.push_back(result_name_arg);

      Instruction *newInst =
          Builder.CreateCall(fct_ptr, makeArrayRef<Value *>(all_arg));

      return newInst;
    }
  }

  Fops mustReplace(Instruction &I) {
    switch (I.getOpcode()) {
      case Instruction::FAdd:
        return FOP_ADD;
      case Instruction::FSub:
        // In LLVM IR the FSub instruction is used to represent FNeg
        return FOP_SUB;
      case Instruction::FMul:
        return FOP_MUL;
      case Instruction::FDiv:
        return FOP_DIV;
      default:
        return FOP_IGNORE;
    }
  }

  void runOnEntryBlock(Module &M, BasicBlock &BBEntry) {
    BasicBlock::iterator I = BBEntry.begin();
    while (isa<AllocaInst>(I)) ++I;
    // now I is the fisrt non-alloca instruction
    for (BasicBlock::iterator ie = BBEntry.end(); I != ie; ++I) {
      if (isa<StoreInst>(I)) {
        errs() << "a entry store instruction" << '\n';
        Type *ty0 = I->getOperand(0)->getType();

        // IR: store [2 x double]* %2, [2 x double]** %.reg2mem26
        // if is [n x double]*
        if (ty0->isPointerTy() && ty0->getPointerElementType()->isArrayTy() &&
            ty0->getPointerElementType()->getArrayElementType()->isDoubleTy() &&
            I->getOperand(1)->getName().startswith(".reg2mem")) {
          Value *arg0 = I->getOperandUse(0);
          // IR: %2 = load [2 x double]** %roots_addr, align 8
          if (isa<LoadInst>(arg0)) {
            // errs() << "process entry store double vector instruction" <<
            // '\n';
            assert(
                !(cast<Instruction>(arg0)->getOperand(0)->getName().empty()) &&
                "function arg should has a name");
            Value *arg1 = I->getOperand(1);
            arg1->setName("xzFP_" +
                          cast<Instruction>(arg0)->getOperand(0)->getName());
            continue;
          }
        } else if (ty0->isPointerTy() &&
                   ty0->getPointerElementType()->isDoubleTy() &&
                   I->getOperand(1)->getName().startswith(".reg2mem")) {
          Value *arg0 = I->getOperandUse(0);
          // IR: %2 = load double* %roots_addr, align 8
          if (isa<LoadInst>(arg0)) {
            assert(
                !(cast<Instruction>(arg0)->getOperand(0)->getName().empty()) &&
                "function arg should has a name");
            Value *arg1 = I->getOperand(1);
            arg1->setName("xzFP_" +
                          cast<Instruction>(arg0)->getOperand(0)->getName());
            continue;
          }
        }
      }
    }
  }

  void relaceFloatOPName(Module &M, BasicBlock &B, Function &F,
                         int32_t func_id) {
    for (BasicBlock::iterator ii = B.begin(), ie = B.end(); ii != ie; ++ii) {
      Instruction &I = *ii;

      Fops opCode = mustReplace(I);
      if (opCode == FOP_IGNORE) continue;

      switch (I.getOpcode()) {
        case Instruction::FAdd:
          I.setName("xz_fadd");
          break;
        case Instruction::FSub:
          I.setName("xz_fsub");
          break;
        case Instruction::FMul:
          I.setName("xz_fmul");
          break;
        case Instruction::FDiv:
          I.setName("xz_fdiv");
          break;
        default:
          break;
      }
    }
  }

  bool runOnBasicBlock(Module &M, BasicBlock &B, Function &F, int32_t func_id,
                       Instruction *create_map_inst) {
    bool modified = false;

    LLVMContext &Context = M.getContext();
    IRBuilder<> Builder(Context);
    SmallVector<Type *, 7> arg_func_signalture;
    arg_func_signalture.push_back(Builder.getInt8PtrTy());
    arg_func_signalture.push_back(Builder.getInt8PtrTy());
    arg_func_signalture.push_back(Builder.getInt8PtrTy());
    arg_func_signalture.push_back(Builder.getInt32Ty());
    arg_func_signalture.push_back(Builder.getInt32Ty());
    arg_func_signalture.push_back(Builder.getDoubleTy());
    Constant *hookStoreFunc;
    hookStoreFunc = M.getOrInsertFunction(
        "__storeDouble",
        FunctionType::get(Builder.getVoidTy(), arg_func_signalture, false));

    arg_func_signalture.push_back(Builder.getInt64Ty());
    Constant *hookDoubleStoreToDoubleArray;
    hookDoubleStoreToDoubleArray = M.getOrInsertFunction(
        "__doubleStoreToDoubleArrayElement",
        FunctionType::get(Builder.getVoidTy(), arg_func_signalture, false));

    Constant *hookDoubleArrayStoreToDouble;
    hookDoubleArrayStoreToDouble = M.getOrInsertFunction(
        "__doubleArrayElementStoreToDouble",
        FunctionType::get(Builder.getVoidTy(), arg_func_signalture, false));

    Constant *hookClearMap;
    hookClearMap = M.getOrInsertFunction(
        "deleteFunctionFloatErrorInfo",
        FunctionType::get(Builder.getVoidTy(), Builder.getInt8PtrTy(), false));

    for (BasicBlock::iterator ii = B.begin(), ie = B.end(); ii != ie; ++ii) {
      Instruction &I = *ii;

      if (I.getOpcode() == Instruction::Ret) {
        Builder.SetInsertPoint(&I);
        CREATE_CALL(hookClearMap, create_map_inst);
        continue;
      }

      if (I.getOpcode() == Instruction::FPTrunc) {
        errs() << "Find a fptrunc inst\n"; 
        continue;
      }
      if (I.getOpcode() == Instruction::FPExt) {
        errs() << "Find a fpext inst\n"; 
        continue;
      }

      Fops opCode = mustReplace(I);
      if (opCode == FOP_IGNORE && I.getOpcode() != Instruction::Load &&
          I.getOpcode() != Instruction::Store &&
          I.getOpcode() != Instruction::BitCast) {
        continue;
      }
      /*
            errs() << "----------------------------" << '\n';
            errs() << "I.opCode " << I.getOpcodeName();
            errs() << '\n';
            for (Instruction::op_iterator it = I.op_begin(); it != I.op_end();
         it++) { errs() << "op "; it->get()->print(errs()); errs() << '\n';
            }
            errs() << "----------------------------" << '\n';
      */
      std::string dbgInfo;
      llvm::raw_string_ostream rso(dbgInfo);
      DebugLoc loc = I.getDebugLoc();  //.print(rso)

      if (I.getOpcode() == Instruction::Load) {
        // Type *opType = I.getOperand(0)->getType();
        /* if (opType->isPointerTy() &&
            opType->getPointerElementType()->isDoubleTy()) {
          MDNode *N = MDNode::get(
              M.getContext(),
              MDString::get(M.getContext(), I.getOperand(0)->getName()));
          I.setMetadata("fp", N);
          continue;
        } */

        // %.reload30 = load [2 x double]** %.reg2mem26
        // %50 = bitcast [2 x double]* %.reload30 to double*, !dbg !64
        // %51 = getelementptr double* %50, i64 0, !dbg !64
        // load/store double %49, double* %51, align 8, !dbg !64
        // IF type is [2 x double]**
        if (I.getOperand(0)->getType()->isPointerTy() &&
            I.getOperand(0)
                ->getType()
                ->getPointerElementType()
                ->isPointerTy() &&
            I.getOperand(0)
                ->getType()
                ->getPointerElementType()
                ->getPointerElementType()
                ->isArrayTy() &&
            I.getOperand(0)
                ->getType()
                ->getPointerElementType()
                ->getPointerElementType()
                ->getArrayElementType()
                ->isDoubleTy()) {
          // errs() << "Find load [n x double]** instruction\n";
          if (isa<BitCastInst>(I.getNextNode()) &&
              isa<GetElementPtrInst>(I.getNextNode()->getNextNode())) {
            // errs() << "Find store double vector instruction\n";
            Instruction *get_element_inst = I.getNextNode()->getNextNode();
            Value *array_index = get_element_inst->getOperand(1);
            assert(array_index->getType()->isIntegerTy());
            if (isa<StoreInst>(I.getNextNode()->getNextNode()->getNextNode())) {
              Instruction *store_inst =
                  I.getNextNode()->getNextNode()->getNextNode();
              if (!store_inst->getOperand(0)->getType()->isDoubleTy()) continue;
              errs() << "Find store double vector instruction\n";
              StringRef double_array_name;
              if (I.getOperand(0)->getName().startswith("xzFP_")) {
                double_array_name =
                    I.getOperand(0)->getName().split("_").second;
              } else {
                double_array_name = I.getOperand(0)->getName();
              }

              assert(!double_array_name.empty());
              Builder.SetInsertPoint(store_inst->getNextNode());
              Value *value_double_array_name =
                  createGlobalStringPtrIfNotExist(double_array_name, Builder);

              SmallVector<Value *, 7> all_arg;
              Value *value_from_name_arg = createGlobalStringPtrIfNotExist(
                  store_inst->getOperand(0)->getName(), Builder);

              all_arg.push_back(create_map_inst);
              all_arg.push_back(value_from_name_arg);
              all_arg.push_back(value_double_array_name);
              all_arg.push_back(Builder.getInt32(func_id));
              all_arg.push_back(
                  Builder.getInt32(store_inst->getDebugLoc().getLine()));
              all_arg.push_back(store_inst->getOperand(0));
              all_arg.push_back(array_index);

              // Instruction *newInst =
              Builder.CreateCall(hookDoubleStoreToDoubleArray,
                                 makeArrayRef<Value *>(all_arg));
              int forward_step = 4;
              while (forward_step--) ii++;

              //%tmp94 = load double* %tmp92
            } else if (isa<LoadInst>(
                           I.getNextNode()->getNextNode()->getNextNode())) {
              Instruction *load_inst =
                  I.getNextNode()->getNextNode()->getNextNode();
              if (!load_inst->getType()->isDoubleTy()) continue;
              errs() << "Find load double vector instruction\n";
              // load_inst->print(errs());
              StringRef double_array_name;
              if (I.getOperand(0)->getName().startswith("xzFP_")) {
                double_array_name =
                    I.getOperand(0)->getName().split("_").second;
              } else {
                double_array_name = I.getOperand(0)->getName();
              }

              Builder.SetInsertPoint(load_inst->getNextNode());
              Value *value_double_array_name =
                  createGlobalStringPtrIfNotExist(double_array_name, Builder);

              SmallVector<Value *, 7> all_arg;
              Value *value_to_name_arg = createGlobalStringPtrIfNotExist(
                  load_inst->getName(), Builder);
              all_arg.push_back(create_map_inst);
              all_arg.push_back(value_double_array_name);
              all_arg.push_back(value_to_name_arg);
              all_arg.push_back(Builder.getInt32(func_id));
              all_arg.push_back(
                  Builder.getInt32(load_inst->getDebugLoc().getLine()));
              all_arg.push_back(load_inst);
              all_arg.push_back(array_index);

              // Instruction *newInst =
              Builder.CreateCall(hookDoubleArrayStoreToDouble,
                                 makeArrayRef<Value *>(all_arg));
              int forward_step = 4;
              while (forward_step--) ii++;
            }
          }

          // simple %aaa = load double* bbb
        } else if (I.getOperand(0)->getType()->isPointerTy() &&
                   I.getOperand(0)
                       ->getType()
                       ->getPointerElementType()
                       ->isDoubleTy()) {
          Builder.SetInsertPoint(I.getNextNode());

          Value *value_from_name_arg = createGlobalStringPtrIfNotExist(
              I.getOperand(0)->getName(), Builder);
          Value *value_to_name_arg =
              createGlobalStringPtrIfNotExist(I.getName(), Builder);

          SmallVector<Value *, 6> arg_store_double;
          arg_store_double.push_back(create_map_inst);
          arg_store_double.push_back(value_from_name_arg);
          arg_store_double.push_back(value_to_name_arg);
          arg_store_double.push_back(Builder.getInt32(func_id));
          arg_store_double.push_back(Builder.getInt32(loc.getLine()));
          arg_store_double.push_back(&I);

          /* Builder.CreateCall5(hookStoreFunc, value_from_name_arg,
                              value_to_name_arg, Builder.getInt32(func_id),
                              Builder.getInt32(I.getDebugLoc().getLine()), &I,
                              ""); */
          Builder.CreateCall(hookStoreFunc,
                             makeArrayRef<Value *>(arg_store_double));
          ii++;
        }
        continue;

      } else if (I.getOpcode() == Instruction::Store) {
        if (I.getNumOperands() == 2) {
          Type *opType1 = I.getOperand(0)->getType();
          Type *opType2 = I.getOperand(1)->getType();
          // errs() << "I.name " << I.getName() << '\n';
          if (opType1->isDoubleTy() && opType2->isPointerTy() &&
              opType2->getPointerElementType()->isDoubleTy()) {
            // if (I.getName())
            // I.op_begin()->get()->print(errs());
            // errs() << '\n';
            // I.getOperand(0)->getType()->print(errs());
            /*MDNode* N = MDNode::get(M.getContext(),
            MDString::get(M.getContext(), I.getOperand(1)->getName())); if
            (isa<Instruction>(I.getOperand(0))) { Instruction& fromI =
            cast<Instruction>(*(I.getOperand(0))); if(mustReplace(fromI) !=
            FOP_IGNORE) fromI.setMetadata("fp", N);
            }*/

            LLVMContext &Context = M.getContext();
            IRBuilder<> Builder(Context);
            Builder.SetInsertPoint(I.getNextNode());

            Value *value_arg1 = I.getOperand(0);
            StringRef name_arg1;
            /* if (isa<Instruction>(use_arg1)) {
              Instruction *I_arg = cast<Instruction>(use_arg1);
              switch (I_arg->getOpcode()) {
                case Instruction::Load:
                  if (I_arg->getMetadata("fp") != NULL) {
                    name_arg1 =
                        cast<MDString>(I_arg->getMetadata("fp")->getOperand(0))
                            ->getString();
                  } else {
                    assert(0 && "Load Instrution don't have fp info");
                  }
                  break;

                default:
                  name_arg1 = I.getOperand(0)->getName();
                  break;
              }
            } else if (cast<Value>(use_arg1)->getValueID() ==
                       Value::ConstantFPVal) { */
            if (value_arg1->getValueID() == Value::ConstantFPVal) {
              name_arg1 = StringRef("__const__value__");
            } else {
              name_arg1 = I.getOperand(0)->getName();
            }

            Value *value_name_arg1 =
                createGlobalStringPtrIfNotExist(name_arg1, Builder);
            Value *value_name_arg2 = createGlobalStringPtrIfNotExist(
                I.getOperand(1)->getName(), Builder);
            // Value *value_arg1 = createGlobalStringPtrIfNotExist(
            //    I.getOperand(1)->getName(), Builder);
            SmallVector<Value *, 6> arg_store_double;
            arg_store_double.push_back(create_map_inst);
            arg_store_double.push_back(value_name_arg1);
            arg_store_double.push_back(value_name_arg2);
            arg_store_double.push_back(Builder.getInt32(func_id));
            arg_store_double.push_back(Builder.getInt32(loc.getLine()));
            arg_store_double.push_back(I.getOperand(0));

            Builder.CreateCall(hookStoreFunc,
                               makeArrayRef<Value *>(arg_store_double));
          }
        }
        continue;

        // %tmp120 = bitcast [3 x double]* %b to double*
      } else if (I.getOpcode() == Instruction::BitCast) {
        if (I.getType()->isPointerTy() &&
            I.getType()->getPointerElementType()->isDoubleTy() &&
            I.getOperand(0)->getType()->isPointerTy() &&
            I.getOperand(0)->getType()->getPointerElementType()->isArrayTy() &&
            I.getOperand(0)
                ->getType()
                ->getPointerElementType()
                ->getArrayElementType()
                ->isDoubleTy()) {
          // %tmp121 = getelementptr double* %tmp120, i64 1, !dbg !68
          // %tmp122 = load double* %tmp121, align 8, !dbg !68
          if (isa<GetElementPtrInst>(I.getNextNode())) {
            Instruction *get_element_inst = I.getNextNode();
            Value *array_index = get_element_inst->getOperand(1);
            if (isa<LoadInst>(get_element_inst->getNextNode())) {
              Instruction *load_inst = get_element_inst->getNextNode();
              if (&I == get_element_inst->getOperand(0) &&
                  get_element_inst == load_inst->getOperand(0)) {
                Builder.SetInsertPoint(load_inst->getNextNode());
                StringRef double_array_name = I.getOperand(0)->getName();
                Value *value_double_array_name =
                    createGlobalStringPtrIfNotExist(double_array_name, Builder);
                Value *value_to_name_arg = createGlobalStringPtrIfNotExist(
                    load_inst->getName(), Builder);

                SmallVector<Value *, 7> all_arg;
                all_arg.push_back(create_map_inst);
                all_arg.push_back(value_double_array_name);
                all_arg.push_back(value_to_name_arg);
                all_arg.push_back(Builder.getInt32(func_id));
                all_arg.push_back(
                    Builder.getInt32(load_inst->getDebugLoc().getLine()));
                all_arg.push_back(load_inst);
                all_arg.push_back(array_index);
                Builder.CreateCall(hookDoubleArrayStoreToDouble,
                                   makeArrayRef<Value *>(all_arg));
                int forward_step = 3;
                while (forward_step--) ii++;
              }
            } else if (isa<StoreInst>(get_element_inst->getNextNode())) {
              Instruction *store_inst = get_element_inst->getNextNode();
              if (&I == get_element_inst->getOperand(0) &&
                  get_element_inst == store_inst->getOperand(1)) {
                I.print(errs());
                Builder.SetInsertPoint(store_inst->getNextNode());
                StringRef double_array_name = I.getOperand(0)->getName();
                Value *value_double_array_name =
                    createGlobalStringPtrIfNotExist(double_array_name, Builder);
                Value *value_from_name_arg = createGlobalStringPtrIfNotExist(
                    store_inst->getOperand(0)->getName(), Builder);

                SmallVector<Value *, 7> all_arg;
                all_arg.push_back(create_map_inst);
                all_arg.push_back(value_from_name_arg);
                all_arg.push_back(value_double_array_name);
                all_arg.push_back(Builder.getInt32(func_id));
                all_arg.push_back(
                    Builder.getInt32(store_inst->getDebugLoc().getLine()));
                all_arg.push_back(store_inst->getOperand(0));
                all_arg.push_back(array_index);
                Builder.CreateCall(hookDoubleStoreToDoubleArray,
                                   makeArrayRef<Value *>(all_arg));
                int forward_step = 3;
                while (forward_step--) ii++;
              }
            }
          }
        }
        continue;
      }

      // if (VfclibInstVerbose) errs() << "Instrumenting" << I << '\n';
      /* switch (opCode) {
        case Instruction::FAdd:
          I.setName("fadd");
          break;
        case Instruction::FSub:
          I.setName("fsub");
          break;
        case Instruction::FMul:
          I.setName("fmul");
          break;
        case Instruction::FDiv:
          I.setName("fdiv");
          break;
        default:
          break;
      } */

      Instruction *newInst = replaceWithMCACall(M, B, &I, opCode, func_id,
                                                loc.getLine(), create_map_inst);

      if (newInst == NULL) continue;
      // Remove instruction from parent so it can be
      // inserted in a new context

      if (newInst->getParent() != NULL) newInst->removeFromParent();
      newInst->takeName(&I);
      ReplaceInstWithInst(B.getInstList(), ii, newInst);
      // I.replaceAllUsesWith(newInst);
      // I.eraseFromParent();

      // if (newInst->getParent() != NULL) newInst->removeFromParent();
      // ReplaceInstWithInst(B.getInstList(), ii, newInst);

      modified = true;
    }

    return modified;
  }
};
}  // namespace

char VfclibInst::ID = 0;
static RegisterPass<VfclibInst> X("vfclibinst", "verificarlo instrument pass",
                                  false, false);
