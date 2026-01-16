#pragma once

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern std::string mode;

class InitValAST;

struct Symbol {
  bool is_const = false;
  int const_value = 0;
  std::string ir_name;
  bool is_array = false;
  bool is_param_ptr = false;
  std::vector<int> dims;
};

struct IRGenContext {
  int temp_id = 0;
  int label_id = 0;
  std::vector<std::unordered_map<std::string, Symbol>> scopes;
  std::vector<std::string> break_labels;
  std::vector<std::string> continue_labels;
  std::unordered_map<std::string, bool> func_returns_void;
  bool koopa_void_as_i32 = true;
  bool current_func_is_void = false;
  bool in_global = false;
  std::ostream *out = nullptr;

  void PushScope();
  void PopScope();
  void AddSymbol(const std::string &name, const Symbol &sym);
  Symbol *FindSymbol(const std::string &name);
  std::string NewTemp();
  std::string NewLabel(const std::string &prefix);
  void Emit(const std::string &line);
};

struct RiscvSymbol {
  bool is_const = false;
  int const_value = 0;
  bool is_global = false;
  std::string label;
  int offset = 0;
  bool is_array = false;
  bool is_param_ptr = false;
  std::vector<int> dims;
};

struct RiscvValue {
  bool is_imm = false;
  int imm = 0;
  bool is_ptr = false;
  bool ptr_is_global = false;
  bool ptr_is_stack_slot = false;
  std::string label;
  int offset = 0;
};

struct RiscvContext {
  int stack_size = 0;
  int label_id = 0;
  std::string func_name;
  std::string return_label;
  std::vector<std::string> break_labels;
  std::vector<std::string> continue_labels;
  std::unordered_map<std::string, bool> func_returns_void;
  std::vector<std::string> data;
  bool in_global = false;
  std::vector<std::string> body;
  std::vector<std::unordered_map<std::string, RiscvSymbol>> scopes;

  void PushScope();
  void PopScope();
  void AddSymbol(const std::string &name, const RiscvSymbol &sym);
  RiscvSymbol *FindSymbol(const std::string &name);
  int AllocSlot();
  int AllocArray(size_t count);
  void Emit(const std::string &line);
  void EmitLabel(const std::string &label);
  std::string NewLabel(const std::string &prefix);
};

/**
 * AST 基类
 */
class BaseAST {
 public:
  virtual ~BaseAST() = default;
  virtual void Dump(IRGenContext &ctx) const = 0;
  virtual void EmitRiscv(RiscvContext &ctx) const = 0;
  virtual bool IsTerminator() const { return false; }
};

class ExprAST {
 public:
  virtual ~ExprAST() = default;
  virtual std::string Gen(IRGenContext &ctx) const = 0;
  virtual int Eval(IRGenContext &ctx) const = 0;
  virtual RiscvValue GenRiscv(RiscvContext &ctx) const = 0;
  virtual int EvalConst(RiscvContext &ctx) const = 0;
};

/**
 * CompUnit ::= FuncDef
 */
class CompUnitAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> items;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
};

/**
 * FuncDef ::= FuncType IDENT "(" ")" Block
 */
class FuncDefAST : public BaseAST {
 public:
  struct Param {
    std::string ident;
    bool is_array = false;
    std::vector<std::unique_ptr<ExprAST>> dims;
  };

  std::unique_ptr<BaseAST> func_type;
  std::string ident;
  std::vector<Param> params;
  std::unique_ptr<BaseAST> block;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
};

/**
 * FuncType ::= "int"
 */
class FuncTypeAST : public BaseAST {
 public:
  std::string type;  // 目前只会是 "int"

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
};

/**
 * Block ::= "{" Stmt "}"
 */
class BlockAST : public BaseAST {
 public:
  std::vector<std::unique_ptr<BaseAST>> items;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
  bool IsTerminator() const override;
};

struct ConstDef {
  std::string ident;
  std::vector<std::unique_ptr<ExprAST>> dims;
  std::unique_ptr<InitValAST> init;
};

class ConstDeclAST : public BaseAST {
 public:
  std::vector<ConstDef> defs;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
};

struct VarDef {
  std::string ident;
  std::vector<std::unique_ptr<ExprAST>> dims;
  std::unique_ptr<InitValAST> init;
  bool has_init = false;
};

class VarDeclAST : public BaseAST {
 public:
  std::vector<VarDef> defs;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
};

class ReturnStmtAST : public BaseAST {
 public:
  std::unique_ptr<ExprAST> value;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
  bool IsTerminator() const override { return true; }
};

class AssignStmtAST : public BaseAST {
 public:
  std::unique_ptr<ExprAST> lval;
  std::unique_ptr<ExprAST> value;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
};

class ExprStmtAST : public BaseAST {
 public:
  std::unique_ptr<ExprAST> expr;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
};

class EmptyStmtAST : public BaseAST {
 public:
  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
};

class IfStmtAST : public BaseAST {
 public:
  std::unique_ptr<ExprAST> cond;
  std::unique_ptr<BaseAST> then_stmt;
  std::unique_ptr<BaseAST> else_stmt;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
  bool IsTerminator() const override;
};

class WhileStmtAST : public BaseAST {
 public:
  std::unique_ptr<ExprAST> cond;
  std::unique_ptr<BaseAST> body;

  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
};

class BreakStmtAST : public BaseAST {
 public:
  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
  bool IsTerminator() const override { return true; }
};

class ContinueStmtAST : public BaseAST {
 public:
  void Dump(IRGenContext &ctx) const override;
  void EmitRiscv(RiscvContext &ctx) const override;
  bool IsTerminator() const override { return true; }
};

class NumberAST : public ExprAST {
 public:
  int value = 0;

  std::string Gen(IRGenContext &ctx) const override;
  int Eval(IRGenContext &ctx) const override;
  RiscvValue GenRiscv(RiscvContext &ctx) const override;
  int EvalConst(RiscvContext &ctx) const override;
};

class LValAST : public ExprAST {
 public:
  std::string ident;
  std::vector<std::unique_ptr<ExprAST>> indices;

  std::string Gen(IRGenContext &ctx) const override;
  int Eval(IRGenContext &ctx) const override;
  std::string GetPtr(IRGenContext &ctx) const;
  std::string GetPtrWithIndices(IRGenContext &ctx) const;
  RiscvValue GenRiscv(RiscvContext &ctx) const override;
  int EvalConst(RiscvContext &ctx) const override;
  int GetOffset(RiscvContext &ctx) const;
  bool IsGlobal(RiscvContext &ctx) const;
  std::string GetLabel(RiscvContext &ctx) const;
  void EmitAddrRiscv(RiscvContext &ctx, const std::vector<int> &dims,
                     const std::vector<RiscvValue> &idx_vals,
                     const RiscvSymbol &sym) const;
};

class UnaryExpAST : public ExprAST {
 public:
  std::string op;
  std::unique_ptr<ExprAST> rhs;

  std::string Gen(IRGenContext &ctx) const override;
  int Eval(IRGenContext &ctx) const override;
  RiscvValue GenRiscv(RiscvContext &ctx) const override;
  int EvalConst(RiscvContext &ctx) const override;
};

class BinaryExpAST : public ExprAST {
 public:
  std::string op;
  std::unique_ptr<ExprAST> lhs;
  std::unique_ptr<ExprAST> rhs;

  std::string Gen(IRGenContext &ctx) const override;
  int Eval(IRGenContext &ctx) const override;
  RiscvValue GenRiscv(RiscvContext &ctx) const override;
  int EvalConst(RiscvContext &ctx) const override;
};

class CallExpAST : public ExprAST {
 public:
  std::string ident;
  std::vector<std::unique_ptr<ExprAST>> args;

  std::string Gen(IRGenContext &ctx) const override;
  int Eval(IRGenContext &ctx) const override;
  RiscvValue GenRiscv(RiscvContext &ctx) const override;
  int EvalConst(RiscvContext &ctx) const override;
};

class InitValAST {
 public:
  bool is_expr = false;
  std::unique_ptr<ExprAST> expr;
  std::vector<std::unique_ptr<InitValAST>> list;
};
