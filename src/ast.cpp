#include "include/ast.hpp"

using std::cout;
using std::endl;
using std::string;

void IRGenContext::PushScope() { scopes.emplace_back(); }

void IRGenContext::PopScope() { scopes.pop_back(); }

void IRGenContext::AddSymbol(const std::string &name, const Symbol &sym) {
  assert(!scopes.empty());
  scopes.back()[name] = sym;
}

Symbol *IRGenContext::FindSymbol(const std::string &name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

std::string IRGenContext::NewTemp() { return "%" + std::to_string(temp_id++); }

std::string IRGenContext::NewLabel(const std::string &prefix) {
  return "%" + prefix + "_" + std::to_string(label_id++);
}

void IRGenContext::Emit(const std::string &line) { *out << "  " << line << "\n"; }

void RiscvContext::PushScope() { scopes.emplace_back(); }

void RiscvContext::PopScope() { scopes.pop_back(); }

void RiscvContext::AddSymbol(const std::string &name, const RiscvSymbol &sym) {
  assert(!scopes.empty());
  scopes.back()[name] = sym;
}

RiscvSymbol *RiscvContext::FindSymbol(const std::string &name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

int RiscvContext::AllocSlot() {
  stack_size += 4;
  return -(stack_size + 8);
}

int RiscvContext::AllocArray(size_t count) {
  int base = -(stack_size + 8 + static_cast<int>(count) * 4);
  stack_size += static_cast<int>(count) * 4;
  return base;
}

void RiscvContext::Emit(const std::string &line) { body.push_back("  " + line); }

void RiscvContext::EmitLabel(const std::string &label) { body.push_back(label + ":"); }

std::string RiscvContext::NewLabel(const std::string &prefix) {
  if (!func_name.empty()) {
    return ".L" + func_name + "_" + prefix + "_" + std::to_string(label_id++);
  }
  return ".L" + prefix + "_" + std::to_string(label_id++);
}

static int Align16(int value) { return (value + 15) / 16 * 16; }

static bool IsImm12(int value) { return value >= -2048 && value <= 2047; }

static void EmitAddImm(RiscvContext &ctx, const std::string &rd,
                       const std::string &rs, int imm) {
  if (IsImm12(imm)) {
    ctx.Emit("addi " + rd + ", " + rs + ", " + std::to_string(imm));
  } else {
    ctx.Emit("li t4, " + std::to_string(imm));
    ctx.Emit("add " + rd + ", " + rs + ", t4");
  }
}

static void EmitAddImmOut(std::ostream &os, const std::string &rd,
                          const std::string &rs, int imm) {
  if (IsImm12(imm)) {
    os << "  addi " << rd << ", " << rs << ", " << imm << "\n";
  } else {
    os << "  li t4, " << imm << "\n";
    os << "  add " << rd << ", " << rs << ", t4\n";
  }
}

static void EmitStoreBaseOut(std::ostream &os, const std::string &reg,
                             const std::string &base, int imm) {
  if (IsImm12(imm)) {
    os << "  sw " << reg << ", " << imm << "(" << base << ")\n";
  } else {
    EmitAddImmOut(os, "t4", base, imm);
    os << "  sw " << reg << ", 0(t4)\n";
  }
}

static void EmitLoadBaseOut(std::ostream &os, const std::string &reg,
                            const std::string &base, int imm) {
  if (IsImm12(imm)) {
    os << "  lw " << reg << ", " << imm << "(" << base << ")\n";
  } else {
    EmitAddImmOut(os, "t4", base, imm);
    os << "  lw " << reg << ", 0(t4)\n";
  }
}

static void EmitStoreBase(RiscvContext &ctx, const std::string &reg,
                          const std::string &base, int imm) {
  if (IsImm12(imm)) {
    ctx.Emit("sw " + reg + ", " + std::to_string(imm) + "(" + base + ")");
  } else {
    EmitAddImm(ctx, "t4", base, imm);
    ctx.Emit("sw " + reg + ", 0(t4)");
  }
}

static void EmitLoadBase(RiscvContext &ctx, const std::string &reg,
                         const std::string &base, int imm) {
  if (IsImm12(imm)) {
    ctx.Emit("lw " + reg + ", " + std::to_string(imm) + "(" + base + ")");
  } else {
    EmitAddImm(ctx, "t4", base, imm);
    ctx.Emit("lw " + reg + ", 0(t4)");
  }
}

static void LoadToReg(RiscvContext &ctx, const RiscvValue &val, const string &reg) {
  if (val.is_imm) {
    ctx.Emit("li " + reg + ", " + std::to_string(val.imm));
  } else if (val.is_ptr) {
    if (val.ptr_is_global) {
      ctx.Emit("la " + reg + ", " + val.label);
    } else if (val.ptr_is_stack_slot) {
      EmitLoadBase(ctx, reg, "s0", val.offset);
    } else {
      EmitAddImm(ctx, reg, "s0", val.offset);
    }
  } else {
    EmitLoadBase(ctx, reg, "s0", val.offset);
  }
}

static RiscvValue StoreFromReg(RiscvContext &ctx, const string &reg) {
  int offset = ctx.AllocSlot();
  EmitStoreBase(ctx, reg, "s0", offset);
  return {false, 0, false, false, false, "", offset};
}

static void EmitLabel(IRGenContext &ctx, const std::string &label) {
  *ctx.out << label << ":\n";
}

static bool IsBuiltinFunc(const std::string &name) {
  return name == "getint" || name == "getch" || name == "getarray" ||
         name == "putint" || name == "putch" || name == "putarray";
}

static int64_t Product(const std::vector<int> &dims, size_t start) {
  int64_t prod = 1;
  for (size_t i = start; i < dims.size(); ++i) {
    prod *= dims[i];
  }
  return prod;
}

static std::string BuildArrayType(const std::vector<int> &dims) {
  std::string type = "i32";
  for (size_t i = dims.size(); i > 0; --i) {
    type = "[" + type + ", " + std::to_string(dims[i - 1]) + "]";
  }
  return type;
}

static std::vector<int> EvalDimsIR(const std::vector<std::unique_ptr<ExprAST>> &dims,
                                   IRGenContext &ctx) {
  std::vector<int> out;
  out.reserve(dims.size());
  for (const auto &expr : dims) {
    out.push_back(expr->Eval(ctx));
  }
  return out;
}

static std::vector<int> EvalDimsRiscv(
    const std::vector<std::unique_ptr<ExprAST>> &dims, RiscvContext &ctx) {
  std::vector<int> out;
  out.reserve(dims.size());
  for (const auto &expr : dims) {
    out.push_back(expr->EvalConst(ctx));
  }
  return out;
}

static void FlattenInitExpr(const InitValAST *init, const std::vector<int> &dims,
                            size_t dim_idx, size_t &pos,
                            std::vector<const ExprAST *> &out) {
  if (dim_idx >= dims.size()) {
    if (init->is_expr) {
      if (pos < out.size()) {
        out[pos] = init->expr.get();
      }
      ++pos;
      return;
    }
    for (const auto &child : init->list) {
      FlattenInitExpr(child.get(), dims, dim_idx, pos, out);
    }
    return;
  }
  if (init->is_expr) {
    if (pos < out.size()) {
      out[pos] = init->expr.get();
    }
    ++pos;
    return;
  }
  int64_t sub = Product(dims, dim_idx + 1);
  for (const auto &child : init->list) {
    if (!child->is_expr) {
      if (sub > 0) {
        size_t aligned = (pos + static_cast<size_t>(sub) - 1) /
                         static_cast<size_t>(sub) *
                         static_cast<size_t>(sub);
        pos = aligned;
      }
      FlattenInitExpr(child.get(), dims, dim_idx + 1, pos, out);
    } else {
      FlattenInitExpr(child.get(), dims, dim_idx + 1, pos, out);
    }
  }
}

static std::string BuildAggregate(const std::vector<int> &dims,
                                  const std::vector<int> &vals, size_t dim_idx,
                                  size_t start) {
  if (dim_idx >= dims.size()) {
    return std::to_string(vals[start]);
  }
  std::string out = "{";
  size_t sub = static_cast<size_t>(Product(dims, dim_idx + 1));
  for (int i = 0; i < dims[dim_idx]; ++i) {
    if (i != 0) {
      out += ", ";
    }
    out += BuildAggregate(dims, vals, dim_idx + 1, start + i * sub);
  }
  out += "}";
  return out;
}

static std::vector<const ExprAST *> BuildInitExprList(
    const InitValAST *init, const std::vector<int> &dims) {
  size_t total = dims.empty() ? 1 : static_cast<size_t>(Product(dims, 0));
  std::vector<const ExprAST *> out(total, nullptr);
  if (init) {
    size_t pos = 0;
    FlattenInitExpr(init, dims, 0, pos, out);
  }
  return out;
}

static std::string GenElemPtr(IRGenContext &ctx, const std::string &base,
                              const std::vector<int> &dims, size_t linear) {
  std::vector<int> indices;
  indices.reserve(dims.size());
  size_t rem = linear;
  for (size_t i = 0; i < dims.size(); ++i) {
    size_t sub = static_cast<size_t>(Product(dims, i + 1));
    int idx = static_cast<int>(rem / sub);
    rem %= sub;
    indices.push_back(idx);
  }
  std::string ptr = base;
  for (int idx : indices) {
    auto next = ctx.NewTemp();
    ctx.Emit(next + " = getelemptr " + ptr + ", " + std::to_string(idx));
    ptr = next;
  }
  return ptr;
}

static std::string GenToBool(IRGenContext &ctx, const std::string &val) {
  auto tmp = ctx.NewTemp();
  ctx.Emit(tmp + " = ne " + val + ", 0");
  return tmp;
}

/* =======================
 * CompUnitAST
 * ======================= */
void CompUnitAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  ctx.PushScope();
  for (const auto &item : items) {
    auto *func = dynamic_cast<FuncDefAST *>(item.get());
    if (func) {
      auto *type = dynamic_cast<FuncTypeAST *>(func->func_type.get());
      bool is_void = type && type->type == "void";
      ctx.func_returns_void[func->ident] = is_void;
    }
  }
  auto ensure_builtin = [&](const std::string &name, bool is_void,
                            const std::string &decl_line) {
    if (ctx.func_returns_void.find(name) == ctx.func_returns_void.end()) {
      ctx.func_returns_void[name] = is_void;
      cout << decl_line << endl;
    }
  };
  ensure_builtin("getint", false, "decl @getint(): i32");
  ensure_builtin("getch", false, "decl @getch(): i32");
  ensure_builtin("getarray", false, "decl @getarray(*i32): i32");
  ensure_builtin("putint", true, "decl @putint(i32)");
  ensure_builtin("putch", true, "decl @putch(i32)");
  ensure_builtin("putarray", true, "decl @putarray(i32, *i32)");
  ctx.in_global = true;
  for (const auto &item : items) {
    if (dynamic_cast<FuncDefAST *>(item.get())) {
      continue;
    }
    item->Dump(ctx);
  }
  ctx.in_global = false;
  for (const auto &item : items) {
    if (!dynamic_cast<FuncDefAST *>(item.get())) {
      continue;
    }
    item->Dump(ctx);
  }
  ctx.PopScope();
}

void CompUnitAST::EmitRiscv(RiscvContext &ctx) const {
  ctx.PushScope();
  for (const auto &item : items) {
    auto *func = dynamic_cast<FuncDefAST *>(item.get());
    if (func) {
      auto *type = dynamic_cast<FuncTypeAST *>(func->func_type.get());
      bool is_void = type && type->type == "void";
      ctx.func_returns_void[func->ident] = is_void;
    }
  }

  ctx.in_global = true;
  for (const auto &item : items) {
    if (dynamic_cast<FuncDefAST *>(item.get())) {
      continue;
    }
    item->EmitRiscv(ctx);
  }
  ctx.in_global = false;

  if (!ctx.data.empty()) {
    cout << "  .data" << endl;
    for (const auto &line : ctx.data) {
      cout << line << endl;
    }
  }

  for (const auto &item : items) {
    auto *func = dynamic_cast<FuncDefAST *>(item.get());
    if (!func) {
      continue;
    }
    RiscvContext fn_ctx;
    fn_ctx.func_returns_void = ctx.func_returns_void;
    fn_ctx.scopes.push_back(ctx.scopes.back());
    func->EmitRiscv(fn_ctx);
  }
  ctx.PopScope();
}

/* =======================
 * FuncDefAST
 * ======================= */
void FuncDefAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  cout << "fun @" << ident << "(";
  for (size_t i = 0; i < params.size(); ++i) {
    if (i != 0) {
      cout << ", ";
    }
    cout << "%" << params[i].ident << ": ";
    if (params[i].is_array) {
      auto dims = EvalDimsIR(params[i].dims, ctx);
      std::string base = dims.empty() ? "i32" : BuildArrayType(dims);
      cout << "*" << base;
    } else {
      cout << "i32";
    }
  }
  cout << "): ";
  auto *type = dynamic_cast<FuncTypeAST *>(func_type.get());
  bool is_void = type && type->type == "void";
  ctx.current_func_is_void = is_void;
  if (is_void && ctx.koopa_void_as_i32) {
    cout << "i32";
  } else {
    func_type->Dump(ctx);
  }
  cout << " {" << endl;
  cout << "%entry:" << endl;
  ctx.PushScope();
  for (const auto &param : params) {
    Symbol sym;
    sym.is_const = false;
    if (param.is_array) {
      sym.is_array = true;
      sym.is_param_ptr = true;
      sym.dims = EvalDimsIR(param.dims, ctx);
      sym.ir_name = "%" + param.ident;
    } else {
      auto alloc = ctx.NewTemp();
      ctx.Emit(alloc + " = alloc i32");
      ctx.Emit("store %" + param.ident + ", " + alloc);
      sym.ir_name = alloc;
    }
    ctx.AddSymbol(param.ident, sym);
  }
  block->Dump(ctx);
  if (is_void && !block->IsTerminator()) {
    if (ctx.koopa_void_as_i32) {
      ctx.Emit("ret 0");
    } else {
      ctx.Emit("ret");
    }
  }
  ctx.PopScope();
  ctx.current_func_is_void = false;
  cout << "}" << endl;
}

void FuncDefAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  ctx.func_name = ident;
  ctx.return_label = ".Lreturn_" + ident;
  ctx.PushScope();
  std::vector<int> param_offsets;
  for (const auto &param : params) {
    int offset = ctx.AllocSlot();
    param_offsets.push_back(offset);
    RiscvSymbol sym;
    sym.is_const = false;
    sym.offset = offset;
    if (param.is_array) {
      sym.is_array = true;
      sym.is_param_ptr = true;
      sym.dims = EvalDimsRiscv(param.dims, ctx);
    }
    ctx.AddSymbol(param.ident, sym);
  }
  block->EmitRiscv(ctx);

  int frame_size = Align16(ctx.stack_size + 8);
  cout << "  .text" << endl;
  cout << "  .globl " << ident << endl;
  cout << ident << ":" << endl;
  EmitAddImmOut(cout, "sp", "sp", -frame_size);
  EmitStoreBaseOut(cout, "ra", "sp", frame_size - 4);
  EmitStoreBaseOut(cout, "s0", "sp", frame_size - 8);
  EmitAddImmOut(cout, "s0", "sp", frame_size);
  for (size_t i = 0; i < param_offsets.size(); ++i) {
    if (i < 8) {
      EmitStoreBaseOut(cout, "a" + std::to_string(i), "s0", param_offsets[i]);
    } else {
      int arg_offset = static_cast<int>((i - 8) * 4);
      EmitLoadBaseOut(cout, "t0", "s0", arg_offset);
      EmitStoreBaseOut(cout, "t0", "s0", param_offsets[i]);
    }
  }
  for (const auto &line : ctx.body) {
    cout << line << endl;
  }
  cout << ctx.return_label << ":" << endl;
  EmitLoadBaseOut(cout, "ra", "sp", frame_size - 4);
  EmitLoadBaseOut(cout, "s0", "sp", frame_size - 8);
  EmitAddImmOut(cout, "sp", "sp", frame_size);
  cout << "  ret" << endl;
  ctx.PopScope();
}

/* =======================
 * FuncTypeAST
 * ======================= */
void FuncTypeAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  if (type == "int") {
    cout << "i32";
  } else if (type == "void") {
    cout << "unit";
  }
}

void FuncTypeAST::EmitRiscv(RiscvContext &ctx) const { (void)ctx; }

/* =======================
 * BlockAST
 * ======================= */
void BlockAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  ctx.PushScope();
  bool need_label = false;
  for (const auto &item : items) {
    if (need_label) {
      auto label = ctx.NewLabel("bb");
      cout << label << ":" << endl;
      need_label = false;
    }
    item->Dump(ctx);
    if (item->IsTerminator()) {
      need_label = true;
    }
  }
  ctx.PopScope();
}

void BlockAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  ctx.PushScope();
  for (const auto &item : items) {
    item->EmitRiscv(ctx);
  }
  ctx.PopScope();
}

bool BlockAST::IsTerminator() const {
  if (items.empty()) {
    return false;
  }
  return items.back()->IsTerminator();
}

/* =======================
 * ConstDeclAST
 * ======================= */
void ConstDeclAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  for (const auto &def : defs) {
    auto dims = EvalDimsIR(def.dims, ctx);
    if (dims.empty()) {
      auto exprs = BuildInitExprList(def.init.get(), dims);
      int value = exprs[0] ? exprs[0]->Eval(ctx) : 0;
      Symbol sym;
      sym.is_const = true;
      sym.const_value = value;
      ctx.AddSymbol(def.ident, sym);
    } else {
      if (ctx.in_global) {
        size_t total = static_cast<size_t>(Product(dims, 0));
        auto exprs = BuildInitExprList(def.init.get(), dims);
        std::vector<int> vals(total, 0);
        for (size_t i = 0; i < total; ++i) {
          if (exprs[i]) {
            vals[i] = exprs[i]->Eval(ctx);
          }
        }
        std::string type = BuildArrayType(dims);
        std::string agg = BuildAggregate(dims, vals, 0, 0);
        cout << "global @" << def.ident << " = alloc " << type << ", " << agg
             << endl;
        Symbol sym;
        sym.is_const = false;
        sym.is_array = true;
        sym.dims = dims;
        sym.ir_name = "@" + def.ident;
        ctx.AddSymbol(def.ident, sym);
      } else {
        std::string type = BuildArrayType(dims);
        auto alloc = ctx.NewTemp();
        ctx.Emit(alloc + " = alloc " + type);
        Symbol sym;
        sym.is_const = false;
        sym.is_array = true;
        sym.dims = dims;
        sym.ir_name = alloc;
        ctx.AddSymbol(def.ident, sym);
        size_t total = static_cast<size_t>(Product(dims, 0));
        auto exprs = BuildInitExprList(def.init.get(), dims);
        for (size_t i = 0; i < total; ++i) {
          std::string val = "0";
          if (exprs[i]) {
            val = exprs[i]->Gen(ctx);
          }
          auto ptr = GenElemPtr(ctx, alloc, dims, i);
          ctx.Emit("store " + val + ", " + ptr);
        }
      }
    }
  }
}

void ConstDeclAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  for (const auto &def : defs) {
    auto dims = EvalDimsRiscv(def.dims, ctx);
    if (dims.empty()) {
      auto exprs = BuildInitExprList(def.init.get(), dims);
      int value = exprs[0] ? exprs[0]->EvalConst(ctx) : 0;
      RiscvSymbol sym;
      sym.is_const = true;
      sym.const_value = value;
      ctx.AddSymbol(def.ident, sym);
    } else {
      size_t total = static_cast<size_t>(Product(dims, 0));
      auto exprs = BuildInitExprList(def.init.get(), dims);
      std::vector<int> vals(total, 0);
      for (size_t i = 0; i < total; ++i) {
        if (exprs[i]) {
          vals[i] = exprs[i]->EvalConst(ctx);
        }
      }
      if (ctx.in_global) {
        ctx.data.push_back("  .globl " + def.ident);
        ctx.data.push_back(def.ident + ":");
        for (size_t i = 0; i < total; ++i) {
          ctx.data.push_back("  .word " + std::to_string(vals[i]));
        }
        RiscvSymbol sym;
        sym.is_const = false;
        sym.is_array = true;
        sym.is_global = true;
        sym.label = def.ident;
        sym.dims = dims;
        ctx.AddSymbol(def.ident, sym);
      } else {
        int base = ctx.AllocArray(total);
        RiscvSymbol sym;
        sym.is_const = false;
        sym.is_array = true;
        sym.offset = base;
        sym.dims = dims;
        ctx.AddSymbol(def.ident, sym);
      for (size_t i = 0; i < total; ++i) {
          ctx.Emit("li t0, " + std::to_string(vals[i]));
          int offset = base + static_cast<int>(i) * 4;
          EmitStoreBase(ctx, "t0", "s0", offset);
      }
      }
    }
  }
}

/* =======================
 * VarDeclAST
 * ======================= */
void VarDeclAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  for (const auto &def : defs) {
    auto dims = EvalDimsIR(def.dims, ctx);
    bool is_array = !dims.empty();
    if (ctx.in_global) {
      if (!is_array) {
        int init_val = 0;
        if (def.has_init) {
          auto exprs = BuildInitExprList(def.init.get(), dims);
          init_val = exprs[0] ? exprs[0]->Eval(ctx) : 0;
        }
        cout << "global @" << def.ident << " = alloc i32, " << init_val
             << endl;
        Symbol sym;
        sym.is_const = false;
        sym.ir_name = "@" + def.ident;
        ctx.AddSymbol(def.ident, sym);
      } else {
        std::string type = BuildArrayType(dims);
        if (!def.has_init) {
          cout << "global @" << def.ident << " = alloc " << type
               << ", zeroinit" << endl;
        } else {
          size_t total = static_cast<size_t>(Product(dims, 0));
          auto exprs = BuildInitExprList(def.init.get(), dims);
          std::vector<int> vals(total, 0);
          for (size_t i = 0; i < total; ++i) {
            if (exprs[i]) {
              vals[i] = exprs[i]->Eval(ctx);
            }
          }
          std::string agg = BuildAggregate(dims, vals, 0, 0);
          cout << "global @" << def.ident << " = alloc " << type << ", " << agg
               << endl;
        }
        Symbol sym;
        sym.is_const = false;
        sym.is_array = true;
        sym.dims = dims;
        sym.ir_name = "@" + def.ident;
        ctx.AddSymbol(def.ident, sym);
      }
    } else {
      if (!is_array) {
        auto alloc = ctx.NewTemp();
        ctx.Emit(alloc + " = alloc i32");
        Symbol sym;
        sym.is_const = false;
        sym.ir_name = alloc;
        ctx.AddSymbol(def.ident, sym);
        if (def.has_init) {
          auto exprs = BuildInitExprList(def.init.get(), dims);
          auto val = exprs[0] ? exprs[0]->Gen(ctx) : "0";
          ctx.Emit("store " + val + ", " + alloc);
        }
      } else {
        std::string type = BuildArrayType(dims);
        auto alloc = ctx.NewTemp();
        ctx.Emit(alloc + " = alloc " + type);
        Symbol sym;
        sym.is_const = false;
        sym.is_array = true;
        sym.dims = dims;
        sym.ir_name = alloc;
        ctx.AddSymbol(def.ident, sym);
        if (def.has_init) {
          size_t total = static_cast<size_t>(Product(dims, 0));
          auto exprs = BuildInitExprList(def.init.get(), dims);
          for (size_t i = 0; i < total; ++i) {
            std::string val = "0";
            if (exprs[i]) {
              val = exprs[i]->Gen(ctx);
            }
            auto ptr = GenElemPtr(ctx, alloc, dims, i);
            ctx.Emit("store " + val + ", " + ptr);
          }
        }
      }
    }
  }
}

void VarDeclAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  for (const auto &def : defs) {
    auto dims = EvalDimsRiscv(def.dims, ctx);
    bool is_array = !dims.empty();
    if (ctx.in_global) {
      if (!is_array) {
        int init_val = 0;
        if (def.has_init) {
          auto exprs = BuildInitExprList(def.init.get(), dims);
          init_val = exprs[0] ? exprs[0]->EvalConst(ctx) : 0;
        }
        ctx.data.push_back("  .globl " + def.ident);
        ctx.data.push_back(def.ident + ":");
        ctx.data.push_back("  .word " + std::to_string(init_val));
        RiscvSymbol sym;
        sym.is_const = false;
        sym.is_global = true;
        sym.label = def.ident;
        ctx.AddSymbol(def.ident, sym);
      } else {
        size_t total = static_cast<size_t>(Product(dims, 0));
        std::vector<int> vals(total, 0);
        if (def.has_init) {
          auto exprs = BuildInitExprList(def.init.get(), dims);
          for (size_t i = 0; i < total; ++i) {
            if (exprs[i]) {
              vals[i] = exprs[i]->EvalConst(ctx);
            }
          }
        }
        ctx.data.push_back("  .globl " + def.ident);
        ctx.data.push_back(def.ident + ":");
        for (size_t i = 0; i < total; ++i) {
          ctx.data.push_back("  .word " + std::to_string(vals[i]));
        }
        RiscvSymbol sym;
        sym.is_const = false;
        sym.is_global = true;
        sym.label = def.ident;
        sym.is_array = true;
        sym.dims = dims;
        ctx.AddSymbol(def.ident, sym);
      }
    } else {
      if (!is_array) {
        int offset = ctx.AllocSlot();
        RiscvSymbol sym;
        sym.is_const = false;
        sym.offset = offset;
        ctx.AddSymbol(def.ident, sym);
        if (def.has_init) {
          auto exprs = BuildInitExprList(def.init.get(), dims);
          auto val = exprs[0] ? exprs[0]->GenRiscv(ctx)
                              : RiscvValue{true, 0, false, false, false, "", 0};
          LoadToReg(ctx, val, "t0");
          EmitStoreBase(ctx, "t0", "s0", offset);
        }
      } else {
        size_t total = static_cast<size_t>(Product(dims, 0));
        int base = ctx.AllocArray(total);
        RiscvSymbol sym;
        sym.is_const = false;
        sym.is_array = true;
        sym.offset = base;
        sym.dims = dims;
        ctx.AddSymbol(def.ident, sym);
        if (def.has_init) {
          auto exprs = BuildInitExprList(def.init.get(), dims);
        for (size_t i = 0; i < total; ++i) {
            RiscvValue val = exprs[i] ? exprs[i]->GenRiscv(ctx)
                                      : RiscvValue{true, 0, false, false, false, "", 0};
            LoadToReg(ctx, val, "t0");
            int offset = base + static_cast<int>(i) * 4;
            EmitStoreBase(ctx, "t0", "s0", offset);
        }
        }
      }
    }
  }
}

/* =======================
 * ReturnStmtAST
 * ======================= */
void ReturnStmtAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  if (value) {
    auto val = value->Gen(ctx);
    ctx.Emit("ret " + val);
  } else {
    if (ctx.current_func_is_void && ctx.koopa_void_as_i32) {
      ctx.Emit("ret 0");
    } else {
      ctx.Emit("ret");
    }
  }
}

void ReturnStmtAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  if (value) {
    auto val = value->GenRiscv(ctx);
    LoadToReg(ctx, val, "a0");
  }
  ctx.Emit("j " + ctx.return_label);
}

/* =======================
 * AssignStmtAST
 * ======================= */
void AssignStmtAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  auto *lval_node = dynamic_cast<LValAST *>(lval.get());
  assert(lval_node);
  auto *sym = ctx.FindSymbol(lval_node->ident);
  assert(sym);
  if (sym->is_array) {
    size_t full = sym->is_param_ptr ? sym->dims.size() + 1 : sym->dims.size();
    assert(lval_node->indices.size() == full);
  }
  auto ptr = lval_node->GetPtr(ctx);
  auto val = value->Gen(ctx);
  ctx.Emit("store " + val + ", " + ptr);
}

void AssignStmtAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  auto *lval_node = dynamic_cast<LValAST *>(lval.get());
  assert(lval_node);
  auto val = value->GenRiscv(ctx);
  auto *sym = ctx.FindSymbol(lval_node->ident);
  assert(sym);
  if (sym->is_array) {
    size_t full = sym->is_param_ptr ? sym->dims.size() + 1 : sym->dims.size();
    assert(lval_node->indices.size() == full);
    std::vector<RiscvValue> idx_vals;
    idx_vals.reserve(lval_node->indices.size());
    for (const auto &idx : lval_node->indices) {
      idx_vals.push_back(idx->GenRiscv(ctx));
    }
    LoadToReg(ctx, val, "t5");
    lval_node->EmitAddrRiscv(ctx, sym->dims, idx_vals, *sym);
    ctx.Emit("sw t5, 0(t0)");
  } else if (lval_node->IsGlobal(ctx)) {
    LoadToReg(ctx, val, "t0");
    ctx.Emit("la t2, " + lval_node->GetLabel(ctx));
    ctx.Emit("sw t0, 0(t2)");
  } else {
    LoadToReg(ctx, val, "t0");
    int offset = lval_node->GetOffset(ctx);
    EmitStoreBase(ctx, "t0", "s0", offset);
  }
}

/* =======================
 * ExprStmtAST
 * ======================= */
void ExprStmtAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  expr->Gen(ctx);
}

void ExprStmtAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  expr->GenRiscv(ctx);
}

/* =======================
 * EmptyStmtAST
 * ======================= */
void EmptyStmtAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
}

void EmptyStmtAST::EmitRiscv(RiscvContext &ctx) const { (void)ctx; }

/* =======================
 * IfStmtAST
 * ======================= */
void IfStmtAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  auto then_label = ctx.NewLabel("then");
  auto end_label = ctx.NewLabel("end");
  bool then_term = then_stmt->IsTerminator();
  if (else_stmt) {
    bool else_term = else_stmt->IsTerminator();
    auto else_label = ctx.NewLabel("else");
    auto cond_val = GenToBool(ctx, cond->Gen(ctx));
    ctx.Emit("br " + cond_val + ", " + then_label + ", " + else_label);
    cout << then_label << ":" << endl;
    then_stmt->Dump(ctx);
    if (!then_term) {
      ctx.Emit("jump " + end_label);
    }
    cout << else_label << ":" << endl;
    else_stmt->Dump(ctx);
    if (!else_term) {
      ctx.Emit("jump " + end_label);
    }
    if (!then_term || !else_term) {
      cout << end_label << ":" << endl;
    }
  } else {
    auto cond_val = GenToBool(ctx, cond->Gen(ctx));
    ctx.Emit("br " + cond_val + ", " + then_label + ", " + end_label);
    cout << then_label << ":" << endl;
    then_stmt->Dump(ctx);
    if (!then_term) {
      ctx.Emit("jump " + end_label);
    }
    cout << end_label << ":" << endl;
  }
}

void IfStmtAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  auto then_label = ctx.NewLabel("then");
  auto end_label = ctx.NewLabel("end");
  if (else_stmt) {
    auto else_label = ctx.NewLabel("else");
    auto cond_val = cond->GenRiscv(ctx);
    LoadToReg(ctx, cond_val, "t0");
    ctx.Emit("beqz t0, " + else_label);
    ctx.EmitLabel(then_label);
    then_stmt->EmitRiscv(ctx);
    ctx.Emit("j " + end_label);
    ctx.EmitLabel(else_label);
    else_stmt->EmitRiscv(ctx);
    ctx.Emit("j " + end_label);
    ctx.EmitLabel(end_label);
  } else {
    auto cond_val = cond->GenRiscv(ctx);
    LoadToReg(ctx, cond_val, "t0");
    ctx.Emit("beqz t0, " + end_label);
    ctx.EmitLabel(then_label);
    then_stmt->EmitRiscv(ctx);
    ctx.EmitLabel(end_label);
  }
}

bool IfStmtAST::IsTerminator() const {
  if (!else_stmt) {
    return false;
  }
  return then_stmt->IsTerminator() && else_stmt->IsTerminator();
}

/* =======================
 * WhileStmtAST
 * ======================= */
void WhileStmtAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  auto cond_label = ctx.NewLabel("while_cond");
  auto body_label = ctx.NewLabel("while_body");
  auto end_label = ctx.NewLabel("while_end");
  ctx.Emit("jump " + cond_label);
  cout << cond_label << ":" << endl;
  auto cond_val = GenToBool(ctx, cond->Gen(ctx));
  ctx.Emit("br " + cond_val + ", " + body_label + ", " + end_label);
  cout << body_label << ":" << endl;
  ctx.break_labels.push_back(end_label);
  ctx.continue_labels.push_back(cond_label);
  body->Dump(ctx);
  ctx.break_labels.pop_back();
  ctx.continue_labels.pop_back();
  if (!body->IsTerminator()) {
    ctx.Emit("jump " + cond_label);
  }
  cout << end_label << ":" << endl;
}

void WhileStmtAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  auto cond_label = ctx.NewLabel("while_cond");
  auto body_label = ctx.NewLabel("while_body");
  auto end_label = ctx.NewLabel("while_end");
  ctx.Emit("j " + cond_label);
  ctx.EmitLabel(cond_label);
  auto cond_val = cond->GenRiscv(ctx);
  LoadToReg(ctx, cond_val, "t0");
  ctx.Emit("beqz t0, " + end_label);
  ctx.EmitLabel(body_label);
  ctx.break_labels.push_back(end_label);
  ctx.continue_labels.push_back(cond_label);
  body->EmitRiscv(ctx);
  ctx.break_labels.pop_back();
  ctx.continue_labels.pop_back();
  if (!body->IsTerminator()) {
    ctx.Emit("j " + cond_label);
  }
  ctx.EmitLabel(end_label);
}

/* =======================
 * BreakStmtAST
 * ======================= */
void BreakStmtAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  assert(!ctx.break_labels.empty());
  ctx.Emit("jump " + ctx.break_labels.back());
}

void BreakStmtAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  assert(!ctx.break_labels.empty());
  ctx.Emit("j " + ctx.break_labels.back());
}

/* =======================
 * ContinueStmtAST
 * ======================= */
void ContinueStmtAST::Dump(IRGenContext &ctx) const {
  if (mode != "-koopa") {
    return;
  }
  assert(!ctx.continue_labels.empty());
  ctx.Emit("jump " + ctx.continue_labels.back());
}

void ContinueStmtAST::EmitRiscv(RiscvContext &ctx) const {
  if (mode != "-riscv") {
    return;
  }
  assert(!ctx.continue_labels.empty());
  ctx.Emit("j " + ctx.continue_labels.back());
}

/* =======================
 * NumberAST
 * ======================= */
std::string NumberAST::Gen(IRGenContext &ctx) const {
  (void)ctx;
  return std::to_string(value);
}

int NumberAST::Eval(IRGenContext &ctx) const {
  (void)ctx;
  return value;
}

RiscvValue NumberAST::GenRiscv(RiscvContext &ctx) const {
  (void)ctx;
  return {true, value, false, false, false, "", 0};
}

int NumberAST::EvalConst(RiscvContext &ctx) const {
  (void)ctx;
  return value;
}

/* =======================
 * LValAST
 * ======================= */
std::string LValAST::Gen(IRGenContext &ctx) const {
  auto *sym = ctx.FindSymbol(ident);
  assert(sym);
  if (sym->is_const && !sym->is_array) {
    return std::to_string(sym->const_value);
  }
  if (sym->is_array) {
    size_t full = sym->is_param_ptr ? sym->dims.size() + 1 : sym->dims.size();
    auto ptr = GetPtrWithIndices(ctx);
    if (indices.size() == full) {
      auto tmp = ctx.NewTemp();
      ctx.Emit(tmp + " = load " + ptr);
      return tmp;
    }
    if (indices.size() > 0 && indices.size() < full) {
      auto tmp = ctx.NewTemp();
      ctx.Emit(tmp + " = getelemptr " + ptr + ", 0");
      return tmp;
    }
    return ptr;
  }
  auto tmp = ctx.NewTemp();
  ctx.Emit(tmp + " = load " + sym->ir_name);
  return tmp;
}

int LValAST::Eval(IRGenContext &ctx) const {
  auto *sym = ctx.FindSymbol(ident);
  assert(sym);
  assert(sym->is_const);
  assert(!sym->is_array);
  return sym->const_value;
}

std::string LValAST::GetPtr(IRGenContext &ctx) const {
  auto *sym = ctx.FindSymbol(ident);
  assert(sym);
  assert(!sym->is_const);
  if (!sym->is_array) {
    return sym->ir_name;
  }
  return GetPtrWithIndices(ctx);
}

std::string LValAST::GetPtrWithIndices(IRGenContext &ctx) const {
  auto *sym = ctx.FindSymbol(ident);
  assert(sym);
  std::vector<std::string> idx_vals;
  idx_vals.reserve(indices.size());
  for (const auto &idx : indices) {
    idx_vals.push_back(idx->Gen(ctx));
  }
  std::string ptr = sym->ir_name;
  if (sym->is_param_ptr) {
    if (idx_vals.empty()) {
      return ptr;
    }
    auto tmp = ctx.NewTemp();
    ctx.Emit(tmp + " = getptr " + ptr + ", " + idx_vals[0]);
    ptr = tmp;
    for (size_t i = 1; i < idx_vals.size(); ++i) {
      auto next = ctx.NewTemp();
      ctx.Emit(next + " = getelemptr " + ptr + ", " + idx_vals[i]);
      ptr = next;
    }
    return ptr;
  }
  if (idx_vals.empty()) {
    auto next = ctx.NewTemp();
    ctx.Emit(next + " = getelemptr " + ptr + ", 0");
    return next;
  }
  for (const auto &idx : idx_vals) {
    auto next = ctx.NewTemp();
    ctx.Emit(next + " = getelemptr " + ptr + ", " + idx);
    ptr = next;
  }
  return ptr;
}

RiscvValue LValAST::GenRiscv(RiscvContext &ctx) const {
  auto *sym = ctx.FindSymbol(ident);
  assert(sym);
  if (sym->is_const && !sym->is_array) {
    return {true, sym->const_value, false, false, false, "", 0};
  }
  if (sym->is_array) {
    size_t full = sym->is_param_ptr ? sym->dims.size() + 1 : sym->dims.size();
    std::vector<RiscvValue> idx_vals;
    idx_vals.reserve(indices.size());
    for (const auto &idx : indices) {
      idx_vals.push_back(idx->GenRiscv(ctx));
    }
    if (indices.size() < full) {
      if (indices.empty()) {
        if (sym->is_global) {
          return {false, 0, true, true, false, sym->label, 0};
        }
        if (sym->is_param_ptr) {
          return {false, 0, true, false, true, "", sym->offset};
        }
        return {false, 0, true, false, false, "", sym->offset};
      }
      EmitAddrRiscv(ctx, sym->dims, idx_vals, *sym);
      return StoreFromReg(ctx, "t0");
    }
    EmitAddrRiscv(ctx, sym->dims, idx_vals, *sym);
    ctx.Emit("lw t1, 0(t0)");
    return StoreFromReg(ctx, "t1");
  }
  if (sym->is_global) {
    ctx.Emit("la t2, " + sym->label);
    ctx.Emit("lw t0, 0(t2)");
    return StoreFromReg(ctx, "t0");
  }
  return {false, 0, false, false, false, "", sym->offset};
}

int LValAST::EvalConst(RiscvContext &ctx) const {
  auto *sym = ctx.FindSymbol(ident);
  assert(sym);
  assert(sym->is_const);
  assert(!sym->is_array);
  return sym->const_value;
}

int LValAST::GetOffset(RiscvContext &ctx) const {
  auto *sym = ctx.FindSymbol(ident);
  assert(sym);
  assert(!sym->is_const);
  assert(!sym->is_global);
  return sym->offset;
}

bool LValAST::IsGlobal(RiscvContext &ctx) const {
  auto *sym = ctx.FindSymbol(ident);
  assert(sym);
  return sym->is_global;
}

std::string LValAST::GetLabel(RiscvContext &ctx) const {
  auto *sym = ctx.FindSymbol(ident);
  assert(sym);
  assert(sym->is_global);
  return sym->label;
}

void LValAST::EmitAddrRiscv(RiscvContext &ctx, const std::vector<int> &dims,
                            const std::vector<RiscvValue> &idx_vals,
                            const RiscvSymbol &sym) const {
  if (sym.is_global) {
    ctx.Emit("la t0, " + sym.label);
  } else if (sym.is_param_ptr) {
    EmitLoadBase(ctx, "t0", "s0", sym.offset);
  } else {
    EmitAddImm(ctx, "t0", "s0", sym.offset);
  }
  if (idx_vals.empty()) {
    return;
  }
  int64_t stride0 = sym.is_param_ptr ? Product(dims, 0) : 0;
  ctx.Emit("li t1, 0");
  for (size_t i = 0; i < idx_vals.size(); ++i) {
    LoadToReg(ctx, idx_vals[i], "t2");
    int64_t stride = 1;
    if (sym.is_param_ptr) {
      if (i == 0) {
        stride = stride0;
      } else {
        stride = Product(dims, i);
      }
    } else {
      stride = Product(dims, i + 1);
    }
    if (stride != 1) {
      ctx.Emit("li t3, " + std::to_string(stride));
      ctx.Emit("mul t2, t2, t3");
    }
    ctx.Emit("add t1, t1, t2");
  }
  ctx.Emit("slli t1, t1, 2");
  ctx.Emit("add t0, t0, t1");
}

/* =======================
 * UnaryExpAST
 * ======================= */
std::string UnaryExpAST::Gen(IRGenContext &ctx) const {
  auto rhs_val = rhs->Gen(ctx);
  if (op == "+") {
    return rhs_val;
  }
  if (op == "-") {
    auto tmp = ctx.NewTemp();
    ctx.Emit(tmp + " = sub 0, " + rhs_val);
    return tmp;
  }
  if (op == "!") {
    auto tmp = ctx.NewTemp();
    ctx.Emit(tmp + " = eq " + rhs_val + ", 0");
    return tmp;
  }
  assert(false);
  return "";
}

int UnaryExpAST::Eval(IRGenContext &ctx) const {
  int rhs_val = rhs->Eval(ctx);
  if (op == "+") {
    return rhs_val;
  }
  if (op == "-") {
    return -rhs_val;
  }
  if (op == "!") {
    return rhs_val == 0 ? 1 : 0;
  }
  assert(false);
  return 0;
}

RiscvValue UnaryExpAST::GenRiscv(RiscvContext &ctx) const {
  auto rhs_val = rhs->GenRiscv(ctx);
  if (op == "+") {
    return rhs_val;
  }
  LoadToReg(ctx, rhs_val, "t0");
  if (op == "-") {
    ctx.Emit("neg t0, t0");
  } else if (op == "!") {
    ctx.Emit("seqz t0, t0");
  } else {
    assert(false);
  }
  return StoreFromReg(ctx, "t0");
}

int UnaryExpAST::EvalConst(RiscvContext &ctx) const {
  int rhs_val = rhs->EvalConst(ctx);
  if (op == "+") {
    return rhs_val;
  }
  if (op == "-") {
    return -rhs_val;
  }
  if (op == "!") {
    return rhs_val == 0 ? 1 : 0;
  }
  assert(false);
  return 0;
}

/* =======================
 * BinaryExpAST
 * ======================= */
std::string BinaryExpAST::Gen(IRGenContext &ctx) const {
  if (op == "&&" || op == "||") {
    auto res_alloc = ctx.NewTemp();
    ctx.Emit(res_alloc + " = alloc i32");
    auto lhs_val = GenToBool(ctx, lhs->Gen(ctx));
    auto rhs_label = ctx.NewLabel("sc_rhs");
    auto set_label = ctx.NewLabel("sc_set");
    auto end_label = ctx.NewLabel("sc_end");
    if (op == "&&") {
      ctx.Emit("br " + lhs_val + ", " + rhs_label + ", " + set_label);
      EmitLabel(ctx, rhs_label);
      auto rhs_val = GenToBool(ctx, rhs->Gen(ctx));
      ctx.Emit("store " + rhs_val + ", " + res_alloc);
      ctx.Emit("jump " + end_label);
      EmitLabel(ctx, set_label);
      ctx.Emit("store 0, " + res_alloc);
      ctx.Emit("jump " + end_label);
    } else {
      ctx.Emit("br " + lhs_val + ", " + set_label + ", " + rhs_label);
      EmitLabel(ctx, rhs_label);
      auto rhs_val = GenToBool(ctx, rhs->Gen(ctx));
      ctx.Emit("store " + rhs_val + ", " + res_alloc);
      ctx.Emit("jump " + end_label);
      EmitLabel(ctx, set_label);
      ctx.Emit("store 1, " + res_alloc);
      ctx.Emit("jump " + end_label);
    }
    EmitLabel(ctx, end_label);
    auto tmp = ctx.NewTemp();
    ctx.Emit(tmp + " = load " + res_alloc);
    return tmp;
  }
  auto lhs_val = lhs->Gen(ctx);
  auto rhs_val = rhs->Gen(ctx);
  auto tmp = ctx.NewTemp();
  string inst;
  if (op == "+") {
    inst = "add";
  } else if (op == "-") {
    inst = "sub";
  } else if (op == "*") {
    inst = "mul";
  } else if (op == "/") {
    inst = "div";
  } else if (op == "%") {
    inst = "mod";
  } else if (op == "<") {
    inst = "lt";
  } else if (op == ">") {
    inst = "gt";
  } else if (op == "<=") {
    inst = "le";
  } else if (op == ">=") {
    inst = "ge";
  } else if (op == "==") {
    inst = "eq";
  } else if (op == "!=") {
    inst = "ne";
  } else {
    assert(false);
  }
  ctx.Emit(tmp + " = " + inst + " " + lhs_val + ", " + rhs_val);
  return tmp;
}

int BinaryExpAST::Eval(IRGenContext &ctx) const {
  int lhs_val = lhs->Eval(ctx);
  int rhs_val = rhs->Eval(ctx);
  if (op == "+") {
    return lhs_val + rhs_val;
  }
  if (op == "-") {
    return lhs_val - rhs_val;
  }
  if (op == "*") {
    return lhs_val * rhs_val;
  }
  if (op == "/") {
    return lhs_val / rhs_val;
  }
  if (op == "%") {
    return lhs_val % rhs_val;
  }
  if (op == "<") {
    return lhs_val < rhs_val ? 1 : 0;
  }
  if (op == ">") {
    return lhs_val > rhs_val ? 1 : 0;
  }
  if (op == "<=") {
    return lhs_val <= rhs_val ? 1 : 0;
  }
  if (op == ">=") {
    return lhs_val >= rhs_val ? 1 : 0;
  }
  if (op == "==") {
    return lhs_val == rhs_val ? 1 : 0;
  }
  if (op == "!=") {
    return lhs_val != rhs_val ? 1 : 0;
  }
  if (op == "&&") {
    return (lhs_val != 0 && rhs_val != 0) ? 1 : 0;
  }
  if (op == "||") {
    return (lhs_val != 0 || rhs_val != 0) ? 1 : 0;
  }
  assert(false);
  return 0;
}

RiscvValue BinaryExpAST::GenRiscv(RiscvContext &ctx) const {
  if (op == "&&" || op == "||") {
    auto res_offset = ctx.AllocSlot();
    auto rhs_label = ctx.NewLabel("sc_rhs");
    auto set_label = ctx.NewLabel("sc_set");
    auto end_label = ctx.NewLabel("sc_end");

    auto lhs_val = lhs->GenRiscv(ctx);
    LoadToReg(ctx, lhs_val, "t0");
    if (op == "&&") {
      ctx.Emit("beqz t0, " + set_label);
      ctx.EmitLabel(rhs_label);
      auto rhs_val = rhs->GenRiscv(ctx);
      LoadToReg(ctx, rhs_val, "t1");
      ctx.Emit("snez t1, t1");
      EmitStoreBase(ctx, "t1", "s0", res_offset);
      ctx.Emit("j " + end_label);
      ctx.EmitLabel(set_label);
      EmitStoreBase(ctx, "x0", "s0", res_offset);
      ctx.Emit("j " + end_label);
    } else {
      ctx.Emit("bnez t0, " + set_label);
      ctx.EmitLabel(rhs_label);
      auto rhs_val = rhs->GenRiscv(ctx);
      LoadToReg(ctx, rhs_val, "t1");
      ctx.Emit("snez t1, t1");
      EmitStoreBase(ctx, "t1", "s0", res_offset);
      ctx.Emit("j " + end_label);
      ctx.EmitLabel(set_label);
      ctx.Emit("li t1, 1");
      EmitStoreBase(ctx, "t1", "s0", res_offset);
      ctx.Emit("j " + end_label);
    }
    ctx.EmitLabel(end_label);
    return {false, 0, false, false, false, "", res_offset};
  }
  auto lhs_val = lhs->GenRiscv(ctx);
  auto rhs_val = rhs->GenRiscv(ctx);
  LoadToReg(ctx, lhs_val, "t0");
  LoadToReg(ctx, rhs_val, "t1");

  if (op == "+") {
    ctx.Emit("add t0, t0, t1");
  } else if (op == "-") {
    ctx.Emit("sub t0, t0, t1");
  } else if (op == "*") {
    ctx.Emit("mul t0, t0, t1");
  } else if (op == "/") {
    ctx.Emit("div t0, t0, t1");
  } else if (op == "%") {
    ctx.Emit("rem t0, t0, t1");
  } else if (op == "<") {
    ctx.Emit("slt t0, t0, t1");
  } else if (op == ">") {
    ctx.Emit("slt t0, t1, t0");
  } else if (op == "<=") {
    ctx.Emit("slt t0, t1, t0");
    ctx.Emit("seqz t0, t0");
  } else if (op == ">=") {
    ctx.Emit("slt t0, t0, t1");
    ctx.Emit("seqz t0, t0");
  } else if (op == "==") {
    ctx.Emit("xor t0, t0, t1");
    ctx.Emit("seqz t0, t0");
  } else if (op == "!=") {
    ctx.Emit("xor t0, t0, t1");
    ctx.Emit("snez t0, t0");
  } else if (op == "&&") {
    ctx.Emit("snez t0, t0");
    ctx.Emit("snez t1, t1");
    ctx.Emit("and t0, t0, t1");
  } else if (op == "||") {
    ctx.Emit("snez t0, t0");
    ctx.Emit("snez t1, t1");
    ctx.Emit("or t0, t0, t1");
  } else {
    assert(false);
  }

  return StoreFromReg(ctx, "t0");
}

int BinaryExpAST::EvalConst(RiscvContext &ctx) const {
  int lhs_val = lhs->EvalConst(ctx);
  int rhs_val = rhs->EvalConst(ctx);
  if (op == "+") {
    return lhs_val + rhs_val;
  }
  if (op == "-") {
    return lhs_val - rhs_val;
  }
  if (op == "*") {
    return lhs_val * rhs_val;
  }
  if (op == "/") {
    return lhs_val / rhs_val;
  }
  if (op == "%") {
    return lhs_val % rhs_val;
  }
  if (op == "<") {
    return lhs_val < rhs_val ? 1 : 0;
  }
  if (op == ">") {
    return lhs_val > rhs_val ? 1 : 0;
  }
  if (op == "<=") {
    return lhs_val <= rhs_val ? 1 : 0;
  }
  if (op == ">=") {
    return lhs_val >= rhs_val ? 1 : 0;
  }
  if (op == "==") {
    return lhs_val == rhs_val ? 1 : 0;
  }
  if (op == "!=") {
    return lhs_val != rhs_val ? 1 : 0;
  }
  if (op == "&&") {
    return (lhs_val != 0 && rhs_val != 0) ? 1 : 0;
  }
  if (op == "||") {
    return (lhs_val != 0 || rhs_val != 0) ? 1 : 0;
  }
  assert(false);
  return 0;
}

/* =======================
 * CallExpAST
 * ======================= */
std::string CallExpAST::Gen(IRGenContext &ctx) const {
  std::vector<std::string> arg_vals;
  arg_vals.reserve(args.size());
  for (const auto &arg : args) {
    arg_vals.push_back(arg->Gen(ctx));
  }
  string args_str;
  for (size_t i = 0; i < arg_vals.size(); ++i) {
    if (i != 0) {
      args_str += ", ";
    }
    args_str += arg_vals[i];
  }
  bool is_void = false;
  auto it = ctx.func_returns_void.find(ident);
  if (it != ctx.func_returns_void.end()) {
    is_void = it->second;
  }
  if (is_void && (!ctx.koopa_void_as_i32 || IsBuiltinFunc(ident))) {
    ctx.Emit("call @" + ident + "(" + args_str + ")");
    return "0";
  }
  auto tmp = ctx.NewTemp();
  ctx.Emit(tmp + " = call @" + ident + "(" + args_str + ")");
  return tmp;
}

int CallExpAST::Eval(IRGenContext &ctx) const {
  (void)ctx;
  return 0;
}

RiscvValue CallExpAST::GenRiscv(RiscvContext &ctx) const {
  std::vector<RiscvValue> arg_vals;
  arg_vals.reserve(args.size());
  for (const auto &arg : args) {
    arg_vals.push_back(arg->GenRiscv(ctx));
  }
  int extra = 0;
  int aligned = 0;
  if (arg_vals.size() > 8) {
    extra = static_cast<int>((arg_vals.size() - 8) * 4);
    aligned = Align16(extra);
    EmitAddImm(ctx, "sp", "sp", -aligned);
    for (size_t i = 8; i < arg_vals.size(); ++i) {
      LoadToReg(ctx, arg_vals[i], "t0");
      int offset = static_cast<int>((i - 8) * 4);
      EmitStoreBase(ctx, "t0", "sp", offset);
    }
  }
  for (size_t i = 0; i < arg_vals.size() && i < 8; ++i) {
    LoadToReg(ctx, arg_vals[i], "t0");
    ctx.Emit("mv a" + std::to_string(i) + ", t0");
  }
  ctx.Emit("call " + ident);
  if (aligned > 0) {
    EmitAddImm(ctx, "sp", "sp", aligned);
  }
  bool is_void = false;
  auto it = ctx.func_returns_void.find(ident);
  if (it != ctx.func_returns_void.end()) {
    is_void = it->second;
  }
  if (is_void) {
    return {true, 0, false, false, false, "", 0};
  }
  return StoreFromReg(ctx, "a0");
}

int CallExpAST::EvalConst(RiscvContext &ctx) const {
  (void)ctx;
  return 0;
}
