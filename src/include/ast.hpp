#pragma once

#include <iostream>
#include <memory>
#include <string>

extern std::string mode;

/**
 * AST 基类
 */
class BaseAST {
 public:
  virtual ~BaseAST() = default;
  virtual void Dump() const = 0;
};

/**
 * CompUnit ::= FuncDef
 */
class CompUnitAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> func_def;

  void Dump() const override;
};

/**
 * FuncDef ::= FuncType IDENT "(" ")" Block
 */
class FuncDefAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> func_type;
  std::string ident;
  std::unique_ptr<BaseAST> block;

  void Dump() const override;
};

/**
 * FuncType ::= "int"
 */
class FuncTypeAST : public BaseAST {
 public:
  std::string type;  // 目前只会是 "int"

  void Dump() const override;
};

/**
 * Block ::= "{" Stmt "}"
 */
class BlockAST : public BaseAST {
 public:
  std::unique_ptr<BaseAST> stmt;

  void Dump() const override;
};

/**
 * Stmt ::= "return" Number ";"
 */
class StmtAST : public BaseAST {
 public:
  int number;

  void Dump() const override;
};
