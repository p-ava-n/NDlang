#include "ndlang/CodeGen.h"

#include <unordered_map>
#include <vector>

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Vectorize/SLPVectorizer.h"

namespace ndlang {

using namespace llvm;

struct CodeGen::Impl {
  LLVMContext ctx;
  std::unique_ptr<Module> mod;
  IRBuilder<> b;
  std::unique_ptr<TargetMachine> tm;
  std::string tripleStr;
  std::string setupErr;

  std::unordered_map<std::string, Function *> funcs;
  std::vector<std::unordered_map<std::string, AllocaInst *>> scopes;
  Function *curFn = nullptr;
  Function *printfFn = nullptr;

  Impl(const std::string &name, const std::string &triple)
      : mod(std::make_unique<Module>(name, ctx)), b(ctx) {
#ifdef NDLANG_ALL_TARGETS
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
#else
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
#endif
    tripleStr = triple.empty() ? sys::getDefaultTargetTriple() : triple;
    std::string cpu = triple.empty() ? sys::getHostCPUName().str() : "generic";

    TargetOptions opts;
    std::string err;
#if LLVM_VERSION_MAJOR >= 21
    Triple tt(tripleStr);
    const Target *target = TargetRegistry::lookupTarget(tt, err);
    if (target)
      tm.reset(target->createTargetMachine(tt, cpu, "", opts, Reloc::PIC_));
    mod->setTargetTriple(tt);
#else
    const Target *target = TargetRegistry::lookupTarget(tripleStr, err);
    if (target)
      tm.reset(target->createTargetMachine(tripleStr, cpu, "", opts, Reloc::PIC_));
    mod->setTargetTriple(tripleStr);
#endif
    if (!tm)
      setupErr = "cannot create target machine for '" + tripleStr + "': " + err;
    else
      mod->setDataLayout(tm->createDataLayout());
  }

  // ------------------------------------------------------------------ types
  llvm::Type *lower(const ndlang::Type &t) {
    switch (t.kind) {
    case ndlang::Type::Int: return b.getInt32Ty();
    case ndlang::Type::Float: return b.getFloatTy();
    case ndlang::Type::Bool: return b.getInt1Ty();
    case ndlang::Type::Vec: return FixedVectorType::get(b.getFloatTy(), t.dim);
    default: return b.getVoidTy();
    }
  }

  // --------------------------------------------------------------- scopes
  void push() { scopes.emplace_back(); }
  void pop() { scopes.pop_back(); }
  AllocaInst *find(const std::string &n) {
    for (size_t i = scopes.size(); i-- > 0;) {
      auto it = scopes[i].find(n);
      if (it != scopes[i].end())
        return it->second;
    }
    return nullptr;
  }

  // Variables start life as allocas in the entry block; mem2reg promotes them
  // to SSA registers later.
  AllocaInst *entryAlloca(llvm::Type *ty, const std::string &name) {
    IRBuilder<> tmp(&curFn->getEntryBlock(), curFn->getEntryBlock().begin());
    return tmp.CreateAlloca(ty, nullptr, name);
  }

  // ---------------------------------------------------------------- program
  bool generate(const Program &prog, std::string &err) {
    if (!setupErr.empty()) {
      err = setupErr;
      return false;
    }
    printfFn = Function::Create(
        FunctionType::get(b.getInt32Ty(), {PointerType::getUnqual(ctx)}, /*vararg=*/true),
        Function::ExternalLinkage, "printf", *mod);

    // Declare everything first so calls can precede definitions.
    for (auto &f : prog.funcs) {
      std::vector<llvm::Type *> ps;
      for (auto &p : f->params)
        ps.push_back(lower(p.type));
      auto *ft = FunctionType::get(lower(f->ret), ps, false);
      auto *fn = Function::Create(ft, Function::ExternalLinkage, f->name, *mod);
      size_t i = 0;
      for (auto &arg : fn->args())
        arg.setName(f->params[i++].name);
      funcs[f->name] = fn;
    }
    for (auto &f : prog.funcs)
      emitFunc(*f);

    std::string verr;
    raw_string_ostream os(verr);
    if (verifyModule(*mod, &os)) {
      err = "internal error: generated invalid IR:\n" + os.str();
      return false;
    }
    return true;
  }

  void emitFunc(const FuncDecl &f) {
    curFn = funcs[f.name];
    b.SetInsertPoint(BasicBlock::Create(ctx, "entry", curFn));
    push();
    size_t i = 0;
    for (auto &arg : curFn->args()) {
      AllocaInst *slot = entryAlloca(arg.getType(), f.params[i].name + ".addr");
      b.CreateStore(&arg, slot);
      scopes.back()[f.params[i].name] = slot;
      ++i;
    }
    emitBlock(*f.body);
    if (!b.GetInsertBlock()->getTerminator()) {
      if (f.ret.isVoid())
        b.CreateRetVoid();
      else
        b.CreateUnreachable(); // sema guarantees this is never reached
    }
    pop();
  }

  // ------------------------------------------------------------- statements
  void emitBlock(const BlockStmt &blk) {
    push();
    for (auto &s : blk.stmts)
      emitStmt(*s);
    pop();
  }

  bool terminated() { return b.GetInsertBlock()->getTerminator() != nullptr; }

  void emitStmt(const Stmt &s) {
    switch (s.kind) {
    case NodeKind::BlockStmt:
      emitBlock(static_cast<const BlockStmt &>(s));
      break;
    case NodeKind::LetStmt: {
      auto &l = static_cast<const LetStmt &>(s);
      Value *init = emitExpr(*l.init);
      AllocaInst *slot = entryAlloca(lower(l.varType), l.name);
      b.CreateStore(init, slot);
      scopes.back()[l.name] = slot;
      break;
    }
    case NodeKind::AssignStmt: {
      auto &a = static_cast<const AssignStmt &>(s);
      Value *v = emitExpr(*a.value);
      b.CreateStore(v, find(a.name));
      break;
    }
    case NodeKind::IfStmt:
      emitIf(static_cast<const IfStmt &>(s));
      break;
    case NodeKind::ForStmt:
      emitFor(static_cast<const ForStmt &>(s));
      break;
    case NodeKind::ReturnStmt: {
      auto &r = static_cast<const ReturnStmt &>(s);
      if (r.value)
        b.CreateRet(emitExpr(*r.value));
      else
        b.CreateRetVoid();
      // Statements after a return land in an unreachable block.
      b.SetInsertPoint(BasicBlock::Create(ctx, "after.ret", curFn));
      break;
    }
    case NodeKind::ExprStmt:
      emitExpr(*static_cast<const ExprStmt &>(s).expr);
      break;
    default:
      break;
    }
  }

  void emitIf(const IfStmt &i) {
    Value *cond = emitExpr(*i.cond);
    auto *thenBB = BasicBlock::Create(ctx, "if.then", curFn);
    auto *elseBB = i.elseBranch ? BasicBlock::Create(ctx, "if.else", curFn) : nullptr;
    auto *mergeBB = BasicBlock::Create(ctx, "if.end", curFn);
    b.CreateCondBr(cond, thenBB, elseBB ? elseBB : mergeBB);

    b.SetInsertPoint(thenBB);
    emitBlock(*i.thenBlock);
    if (!terminated())
      b.CreateBr(mergeBB);

    if (elseBB) {
      b.SetInsertPoint(elseBB);
      emitStmt(*i.elseBranch);
      if (!terminated())
        b.CreateBr(mergeBB);
    }
    b.SetInsertPoint(mergeBB);
  }

  void emitFor(const ForStmt &f) {
    Value *lo = emitExpr(*f.start);
    Value *hi = emitExpr(*f.end);
    push();
    AllocaInst *iv = entryAlloca(b.getInt32Ty(), f.var);
    b.CreateStore(lo, iv);
    scopes.back()[f.var] = iv;

    auto *condBB = BasicBlock::Create(ctx, "for.cond", curFn);
    auto *bodyBB = BasicBlock::Create(ctx, "for.body", curFn);
    auto *incBB = BasicBlock::Create(ctx, "for.inc", curFn);
    auto *endBB = BasicBlock::Create(ctx, "for.end", curFn);
    b.CreateBr(condBB);

    b.SetInsertPoint(condBB);
    Value *cur = b.CreateLoad(b.getInt32Ty(), iv, f.var + ".cur");
    b.CreateCondBr(b.CreateICmpSLT(cur, hi, "for.cmp"), bodyBB, endBB);

    b.SetInsertPoint(bodyBB);
    emitBlock(*f.body);
    if (!terminated())
      b.CreateBr(incBB);

    b.SetInsertPoint(incBB);
    Value *c2 = b.CreateLoad(b.getInt32Ty(), iv, f.var + ".old");
    b.CreateStore(b.CreateAdd(c2, b.getInt32(1), f.var + ".next", false, true), iv);
    b.CreateBr(condBB);

    b.SetInsertPoint(endBB);
    pop();
  }

  // ------------------------------------------------------------ expressions
  Value *emitExpr(const Expr &e) {
    switch (e.kind) {
    case NodeKind::NumberLit: {
      auto &n = static_cast<const NumberLit &>(e);
      if (n.isFloat)
        return ConstantFP::get(b.getFloatTy(), n.fval);
      return ConstantInt::get(b.getInt32Ty(), static_cast<uint64_t>(n.ival), true);
    }
    case NodeKind::BoolLit:
      return b.getInt1(static_cast<const BoolLit &>(e).value);
    case NodeKind::VecLit: {
      auto &v = static_cast<const VecLit &>(e);
      Value *vec = PoisonValue::get(lower(e.type));
      for (size_t k = 0; k < v.elems.size(); ++k)
        vec = b.CreateInsertElement(vec, emitExpr(*v.elems[k]), b.getInt32(static_cast<uint32_t>(k)));
      return vec;
    }
    case NodeKind::VarRef: {
      auto &v = static_cast<const VarRef &>(e);
      return b.CreateLoad(lower(e.type), find(v.name), v.name);
    }
    case NodeKind::CastExpr:
      return b.CreateSIToFP(emitExpr(*static_cast<const CastExpr &>(e).operand), b.getFloatTy(), "widen");
    case NodeKind::UnaryOp: {
      auto &u = static_cast<const UnaryOp &>(e);
      Value *x = emitExpr(*u.operand);
      if (u.op == UnOp::Not)
        return b.CreateNot(x, "not");
      return e.type.isInt() ? b.CreateNeg(x, "neg") : b.CreateFNeg(x, "neg");
    }
    case NodeKind::BinaryOp:
      return emitBinary(static_cast<const BinaryOp &>(e));
    case NodeKind::IndexExpr: {
      auto &ix = static_cast<const IndexExpr &>(e);
      return b.CreateExtractElement(emitExpr(*ix.base), emitExpr(*ix.index), "elem");
    }
    case NodeKind::CallExpr:
      return emitCall(static_cast<const CallExpr &>(e));
    default:
      return nullptr;
    }
  }

  Value *emitShortCircuit(const BinaryOp &op) {
    bool isAnd = op.op == BinOp::And;
    Value *lhs = emitExpr(*op.lhs);
    BasicBlock *lhsBB = b.GetInsertBlock();
    auto *rhsBB = BasicBlock::Create(ctx, isAnd ? "and.rhs" : "or.rhs", curFn);
    auto *endBB = BasicBlock::Create(ctx, isAnd ? "and.end" : "or.end", curFn);
    if (isAnd)
      b.CreateCondBr(lhs, rhsBB, endBB);
    else
      b.CreateCondBr(lhs, endBB, rhsBB);

    b.SetInsertPoint(rhsBB);
    Value *rhs = emitExpr(*op.rhs);
    BasicBlock *rhsEnd = b.GetInsertBlock();
    b.CreateBr(endBB);

    b.SetInsertPoint(endBB);
    PHINode *phi = b.CreatePHI(b.getInt1Ty(), 2, isAnd ? "and" : "or");
    phi->addIncoming(b.getInt1(!isAnd), lhsBB);
    phi->addIncoming(rhs, rhsEnd);
    return phi;
  }

  Value *emitBinary(const BinaryOp &op) {
    if (op.op == BinOp::And || op.op == BinOp::Or)
      return emitShortCircuit(op);

    Value *l = emitExpr(*op.lhs);
    Value *r = emitExpr(*op.rhs);
    const ndlang::Type &lt = op.lhs->type, &rt = op.rhs->type;

    // vec <op> scalar: broadcast the scalar so one vector instruction suffices.
    if (op.type.isVec()) {
      int n = op.type.dim;
      if (!lt.isVec()) l = b.CreateVectorSplat(n, l, "splat");
      if (!rt.isVec()) r = b.CreateVectorSplat(n, r, "splat");
    }

    bool isInt = lt.isInt() && rt.isInt();
    switch (op.op) {
    case BinOp::Add: return isInt ? b.CreateAdd(l, r, "add") : b.CreateFAdd(l, r, "add");
    case BinOp::Sub: return isInt ? b.CreateSub(l, r, "sub") : b.CreateFSub(l, r, "sub");
    case BinOp::Mul: return isInt ? b.CreateMul(l, r, "mul") : b.CreateFMul(l, r, "mul");
    case BinOp::Div: return isInt ? b.CreateSDiv(l, r, "div") : b.CreateFDiv(l, r, "div");
    case BinOp::Lt: return isInt ? b.CreateICmpSLT(l, r, "lt") : b.CreateFCmpOLT(l, r, "lt");
    case BinOp::Gt: return isInt ? b.CreateICmpSGT(l, r, "gt") : b.CreateFCmpOGT(l, r, "gt");
    case BinOp::Le: return isInt ? b.CreateICmpSLE(l, r, "le") : b.CreateFCmpOLE(l, r, "le");
    case BinOp::Ge: return isInt ? b.CreateICmpSGE(l, r, "ge") : b.CreateFCmpOGE(l, r, "ge");
    case BinOp::Eq:
      return (isInt || lt.isBool()) ? b.CreateICmpEQ(l, r, "eq") : b.CreateFCmpOEQ(l, r, "eq");
    case BinOp::Ne:
      return (isInt || lt.isBool()) ? b.CreateICmpNE(l, r, "ne") : b.CreateFCmpUNE(l, r, "ne");
    default:
      return nullptr;
    }
  }

  // sum(v) -> llvm.vector.reduce.fadd. `reassoc` lets the backend pick a
  // tree/horizontal reduction instead of a strictly ordered one.
  Value *reduceAdd(Value *vec) {
    Value *r = b.CreateFAddReduce(ConstantFP::get(b.getFloatTy(), 0.0), vec);
    FastMathFlags fmf;
    fmf.setAllowReassoc();
    cast<CallInst>(r)->setFastMathFlags(fmf);
    return r;
  }

  Value *emitCall(const CallExpr &c) {
    switch (c.builtin) {
    case Builtin::Sum:
      return reduceAdd(emitExpr(*c.args[0]));
    case Builtin::Dot: {
      Value *m = b.CreateFMul(emitExpr(*c.args[0]), emitExpr(*c.args[1]), "m");
      return reduceAdd(m);
    }
    case Builtin::Print:
      emitPrint(*c.args[0]);
      return nullptr;
    case Builtin::None:
      break;
    }
    std::vector<Value *> args;
    for (auto &a : c.args)
      args.push_back(emitExpr(*a));
    CallInst *call = b.CreateCall(funcs[c.callee], args);
    if (!c.type.isVoid())
      call->setName("call");
    return call;
  }

  // print() is a thin wrapper over libc printf.
  void emitPrint(const Expr &arg) {
    Value *v = emitExpr(arg);
    const ndlang::Type &t = arg.type;
    std::vector<Value *> args;
    std::string fmt;
    switch (t.kind) {
    case ndlang::Type::Int:
      fmt = "%d\n";
      args.push_back(v);
      break;
    case ndlang::Type::Float:
      fmt = "%f\n";
      args.push_back(b.CreateFPExt(v, b.getDoubleTy())); // varargs promote to double
      break;
    case ndlang::Type::Bool:
      fmt = "%s\n";
      args.push_back(b.CreateSelect(v, b.CreateGlobalStringPtr("true", ".true"),
                                    b.CreateGlobalStringPtr("false", ".false")));
      break;
    case ndlang::Type::Vec:
      fmt = "[";
      for (int k = 0; k < t.dim; ++k) {
        fmt += k ? ", %f" : "%f";
        Value *el = b.CreateExtractElement(v, b.getInt32(static_cast<uint32_t>(k)));
        args.push_back(b.CreateFPExt(el, b.getDoubleTy()));
      }
      fmt += "]\n";
      break;
    default:
      return;
    }
    args.insert(args.begin(), b.CreateGlobalStringPtr(fmt, ".fmt"));
    b.CreateCall(printfFn, args);
  }

  // ----------------------------------------------------------- optimisation
  bool optimize(int level, std::string &err) {
    if (level <= 0)
      return true;
    if (!tm) {
      err = setupErr;
      return false;
    }

    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    PassBuilder PB(tm.get());
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager MPM;
    if (level >= 2) {
      MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);
    } else {
      // The pipeline from the project slides, spelled out.
      FunctionPassManager FPM;
      FPM.addPass(PromotePass());        // mem2reg: alloca/load/store -> SSA
      FPM.addPass(InstCombinePass());    // simplify + merge instructions
      FPM.addPass(SimplifyCFGPass());    // drop empty/unreachable blocks
      FPM.addPass(SLPVectorizerPass());  // pack scalar ops into SIMD
      FPM.addPass(InstCombinePass());
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    }
    MPM.run(*mod, MAM);

    std::string verr;
    raw_string_ostream os(verr);
    if (verifyModule(*mod, &os)) {
      err = "internal error: optimizer produced invalid IR:\n" + os.str();
      return false;
    }
    return true;
  }

  bool emitObject(const std::string &path, std::string &err) {
    if (!tm) {
      err = setupErr;
      return false;
    }
    std::error_code ec;
    raw_fd_ostream dest(path, ec, sys::fs::OF_None);
    if (ec) {
      err = "cannot open '" + path + "': " + ec.message();
      return false;
    }
    legacy::PassManager pm;
#if LLVM_VERSION_MAJOR >= 18
    auto ft = CodeGenFileType::ObjectFile;
#else
    auto ft = CGFT_ObjectFile;
#endif
    if (tm->addPassesToEmitFile(pm, dest, nullptr, ft)) {
      err = "target '" + tripleStr + "' cannot emit object files";
      return false;
    }
    pm.run(*mod);
    dest.flush();
    return true;
  }
};

CodeGen::CodeGen(const std::string &moduleName, const std::string &triple)
    : impl(new Impl(moduleName, triple)) {}
CodeGen::~CodeGen() = default;

bool CodeGen::generate(const Program &prog, std::string &err) { return impl->generate(prog, err); }
bool CodeGen::optimize(int level, std::string &err) { return impl->optimize(level, err); }
bool CodeGen::emitObject(const std::string &path, std::string &err) { return impl->emitObject(path, err); }

std::string CodeGen::ir() const {
  std::string s;
  raw_string_ostream os(s);
  impl->mod->print(os, nullptr);
  return os.str();
}

} // namespace ndlang
