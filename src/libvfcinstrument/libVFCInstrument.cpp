/********************************************************************************
 *                                                                              *
 *  This file is part of Verificarlo.                                           *
 *                                                                              *
 *  Copyright (c) 2015                                                          *
 *     Universite de Versailles St-Quentin-en-Yvelines                          *
 *     CMLA, Ecole Normale Superieure de Cachan                                 *
 *                                                                              *
 *  Verificarlo is free software: you can redistribute it and/or modify         *
 *  it under the terms of the GNU General Public License as published by        *
 *  the Free Software Foundation, either version 3 of the License, or           *
 *  (at your option) any later version.                                         *
 *                                                                              *
 *  Verificarlo is distributed in the hope that it will be useful,              *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
 *  GNU General Public License for more details.                                *
 *                                                                              *
 *  You should have received a copy of the GNU General Public License           *
 *  along with Verificarlo.  If not, see <http://www.gnu.org/licenses/>.        *
 *                                                                              *
 ********************************************************************************/

#include "../../config.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <map>
#include <set>
#include <string>
#include <fstream>

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 6
#define CREATE_CALL(func, op1) (Builder.CreateCall(func, op1, ""))
#define CREATE_CALL2(func, op1, op2) (Builder.CreateCall2(func, op1, op2, ""))
#define CREATE_CALL3(func, op1, op2, op3) (Builder.CreateCall3(func, op1, op2, op3, ""))
#define CREATE_CALL4(func, op1, op2, op3, op4) (Builder.CreateCall3(func, op1, op2, op3, op4, ""))
#define CREATE_STRUCT_GEP(t, i, p) (Builder.CreateStructGEP(i, p))
#else
#define CREATE_CALL(func, op1) (Builder.CreateCall(func, {op1}, ""))
#define CREATE_CALL2(func, op1, op2) (Builder.CreateCall(func, {op1, op2}, ""))
#define CREATE_CALL3(func, op1, op2, op3) (Builder.CreateCall(func, {op1, op2, op3}, ""))
#define CREATE_CALL4(func, op1, op2, op3, op4) (Builder.CreateCall(func, {op1, op2, op3, op4}, ""))
#define CREATE_STRUCT_GEP(t, i, p) (Builder.CreateStructGEP(t, i, p, ""))
#endif

using namespace llvm;
// VfclibInst pass command line arguments
static cl::opt<std::string> VfclibInstFunction("vfclibinst-function",
					       cl::desc("Only instrument given FunctionName"),
					       cl::value_desc("FunctionName"), cl::init(""));

static cl::opt<std::string> VfclibInstFunctionFile("vfclibinst-function-file",
						   cl::desc("Instrument functions in file FunctionNameFile "),
						   cl::value_desc("FunctionsNameFile"), cl::init(""));

static cl::opt<bool> VfclibInstVerbose("vfclibinst-verbose",
				       cl::desc("Activate verbose mode"),
				       cl::value_desc("Verbose"), cl::init(false));

namespace {
    // Define an enum type to classify the floating points operations
    // that are instrumented by verificarlo

    enum Fops {FOP_ADD, FOP_SUB, FOP_MUL, FOP_DIV, FOP_IGNORE};

    // Each instruction can be translated to a string representation

    std::string Fops2str[] = { "add", "sub", "mul", "div", "ignore"};

    struct VfclibInst : public ModulePass {
        static char ID;

        std::map<std::string,int> SelectedFunctionSet;

        VfclibInst() : ModulePass(ID) {
            int funcId = 0;
            if (not VfclibInstFunctionFile.empty()) {
                std::string line;
                std::ifstream loopstream (VfclibInstFunctionFile.c_str());
                if (loopstream.is_open()) {
                    while (std::getline(loopstream, line)) {
                        SelectedFunctionSet.insert(std::pair<std::string ,int>(line,funcId++));
                    }
                    loopstream.close();
                } else {
                    errs() << "Cannot open " << VfclibInstFunctionFile << "\n";
                    assert(0);
                }
            } else if (not VfclibInstFunction.empty()) {
                SelectedFunctionSet.insert(std::pair<std::string ,int>(VfclibInstFunction, funcId++));
            }

            if (SelectedFunctionSet.empty()) {
                errs() << "Please give at least a function for processing\n";
                assert(0);
            }
        }

        StructType * getMCAInterfaceType(IRBuilder<> &Builder) {

            // Verificarlo instrumentation calls the mca backend using
            // a vtable implemented as a structure.
            //
            // Here we declare the struct type corresponding to the
            // mca_interface_t defined in ../vfcwrapper/vfcwrapper.h
            //
            // Only the functions instrumented are declared. The last
            // three functions are user called functions and are not
            // needed here.

            SmallVector<Type *, 2> floatArgs, doubleArgs;
            floatArgs.push_back(Builder.getFloatTy());
            floatArgs.push_back(Builder.getFloatTy());
            floatArgs.push_back(Builder.getInt8PtrTy());
            doubleArgs.push_back(Builder.getDoubleTy());
            doubleArgs.push_back(Builder.getDoubleTy());
            doubleArgs.push_back(Builder.getInt8PtrTy());

            PointerType * floatInstFun = PointerType::getUnqual(
                    FunctionType::get(Builder.getFloatTy(), floatArgs, false));
            PointerType * doubleInstFun = PointerType::getUnqual(
                    FunctionType::get(Builder.getDoubleTy(), doubleArgs, false));

            return StructType::get(

                floatInstFun,
                floatInstFun,
                floatInstFun,
                floatInstFun,

                doubleInstFun,
                doubleInstFun,
                doubleInstFun,
                doubleInstFun,

                (void *)0
                );
        }

        bool runOnModule(Module &M) {
            bool modified = false;

            // Find the list of functions to instrument
            // Instrumentation adds stubs to mcalib function which we
            // never want to instrument.  Therefore it is important to
            // first find all the functions of interest before
            // starting instrumentation.

            std::vector<Function*> functions;
            for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
                const bool is_in = SelectedFunctionSet.find(
                        F->getName()) != SelectedFunctionSet.end();
                if (SelectedFunctionSet.empty() || is_in) {
                    functions.push_back(&*F);
                }

                if (F->getName().str() == "main") {
                    runOnMainFunction(M, *F);
                }
            }

            // Do the instrumentation on selected functions
            for(std::vector<Function*>::iterator F = functions.begin(); F != functions.end(); ++F) {
                modified |= runOnFunction(M, **F);
            }
            // runOnModule must return true if the pass modifies the IR
            return modified;
        }

        bool runOnMainFunction(Module &M, Function &F) {
            BasicBlock& firstBlock = F.getEntryBlock();
            LLVMContext &Context = M.getContext();
            IRBuilder<> Builder(Context);
            Instruction & I = firstBlock.front();
            Builder.SetInsertPoint(&I);

            Constant *hookFunc;
            // Function *hook;
            SmallVector<Type *, 2> arg_vector;
            arg_vector.push_back(Builder.getInt8PtrTy());
            arg_vector.push_back(Builder.getInt32Ty());
            hookFunc = M.getOrInsertFunction("initNewFunction", 
                FunctionType::get(Builder.getVoidTy(), arg_vector, false));
            // hook= cast<Function>(hookFunc);

            for (std::map<std::string,int>::iterator it = SelectedFunctionSet.begin();
                it != SelectedFunctionSet.end(); it++) {
                Instruction *newInst = CREATE_CALL2(hookFunc,
                    Builder.CreateGlobalStringPtr(llvm::StringRef(it->first.c_str())), 
                    Builder.getInt32(it->second));
                //if (newInst->getParent() != NULL) newInst->removeFromParent();
                //firstBlock.getFirstInsertionPt()
                
            }

            return true;
        }

        bool runOnFunction(Module &M, Function &F) {
            if (VfclibInstVerbose) {
                errs() << "In Function: ";
                errs().write_escaped(F.getName()) << '\n';
            }

            bool modified = false;

            for (Function::iterator bi = F.begin(), be = F.end(); bi != be; ++bi) {
                modified |= runOnBasicBlock(M, *bi, F);
            }
            return modified;
        }

        Instruction *replaceWithMCACall(Module &M, BasicBlock &B,
                Instruction * I, Fops opCode, char* dbg) {

            LLVMContext &Context = M.getContext();
            IRBuilder<> Builder(Context);
            StructType * mca_interface_type = getMCAInterfaceType(Builder);

            Type * retType = I->getType();
            Type * opType = I->getOperand(0)->getType();
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
                errs() << "Unsupported operand type: " << *opType << "\n";
                assert(0);
            }

            // For vector types, helper functions in vfcwrapper are called
            if (vectorName != "") {
                std::string mcaFunctionName = "_" + vectorName + baseTypeName + opName;

                Constant *hookFunc = M.getOrInsertFunction(mcaFunctionName,
                                                           retType,
                                                           opType,
                                                           opType,
                                                           (Type *) 0);

                // For vector types we call directly a hardcoded helper function
                // no need to go through the vtable at this stage.
                Instruction *newInst = CREATE_CALL3(hookFunc,
                                                    I->getOperand(0), I->getOperand(1), Builder.CreateGlobalStringPtr(llvm::StringRef(dbg)));

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

                //current_mca_interface->print(errs());

                // Compute the position of the required member fct pointer
                // opCodes are ordered in the same order than the struct members :-)
                // There are 4 float members followed by 4 double members.
                int fct_position = opCode;
                if (baseTypeName == "double") fct_position += 4;
                // Dereference the member at fct_position
                Value *arg_ptr = CREATE_STRUCT_GEP(
                    mca_interface_type, current_mca_interface, fct_position);
                Value *fct_ptr = Builder.CreateLoad(arg_ptr, "");
                //errs() << '\n';
                //arg_ptr->print(errs());
                //errs() << '\n';
                //fct_ptr->print(errs());
                //errs() << '\n';

                // Create a call instruction. It
                // will _replace_ I after it is returned.
                Instruction *newInst = CREATE_CALL3(
                    fct_ptr,
                    I->getOperand(0), I->getOperand(1), Builder.CreateGlobalStringPtr(dbg));

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

        bool runOnBasicBlock(Module &M, BasicBlock &B, Function &F) {

            bool modified = false;
            for (BasicBlock::iterator ii = B.begin(), ie = B.end(); ii != ie; ++ii) {
                Instruction &I = *ii;
                Fops opCode = mustReplace(I);
                if (opCode == FOP_IGNORE) continue;
                if (VfclibInstVerbose) errs() << "Instrumenting" << I << '\n';
                //errs() << "hello\n";
                //I.getDebugLoc().print(errs());
                std::string dbgInfo;
                llvm::raw_string_ostream rso(dbgInfo);
                DebugLoc loc = I.getDebugLoc(); //.print(rso)
               
                rso << " function " <<  F.getName() 
                    <<"line:" << loc.getLine() << " Column:" << loc.getCol();
                std::string dbgStr = rso.str();
                char* dbg = strdup(dbgStr.c_str());

                Instruction *newInst = replaceWithMCACall(M, B, &I, opCode, dbg);
                // Remove instruction from parent so it can be
                // inserted in a new context
                if (newInst->getParent() != NULL) newInst->removeFromParent();
                ReplaceInstWithInst(B.getInstList(), ii, newInst);
                modified = true;
            }

            return modified;
        }
    };
}

char VfclibInst::ID = 0;
static RegisterPass<VfclibInst> X("vfclibinst", "verificarlo instrument pass", false, false);

