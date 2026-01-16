#include "include/ast.hpp"
#include <iostream>

using std::cout;
using std::endl;

/* =======================
 * CompUnitAST
 * ======================= */
void CompUnitAST::Dump() const {
  if (mode == "-debug") {
    cout << "CompUnitAST { ";
    func_def->Dump();
    cout << " }";
    return;
  }

  if (mode == "-koopa") {
    func_def->Dump();
    return;
  }
}

/* =======================
 * FuncDefAST
 * ======================= */
void FuncDefAST::Dump() const {
  if (mode == "-debug") {
    cout << "FuncDefAST { ";
    func_type->Dump();
    cout << ", " << ident << ", ";
    block->Dump();
    cout << " }";
    return;
  }

  if (mode == "-koopa") {
    cout << "fun @" << ident << "(): ";
    func_type->Dump();
    cout << " {" << endl;
    block->Dump();
    cout << "}" << endl;
    return;
  }
}

/* =======================
 * FuncTypeAST
 * ======================= */
void FuncTypeAST::Dump() const {
  if (mode == "-debug") {
    cout << "FuncTypeAST { " << type << " }";
    return;
  }

  if (mode == "-koopa") {
    if (type == "int") {
      cout << "i32";
    } else if (type == "void") {
      cout << "void";
    }
    return;
  }
}

/* =======================
 * BlockAST
 * ======================= */
void BlockAST::Dump() const {
  if (mode == "-debug") {
    cout << "BlockAST { ";
    stmt->Dump();
    cout << " }";
    return;
  }

  if (mode == "-koopa") {
    cout << "%entry:" << endl;
    stmt->Dump();
    return;
  }
}

/* =======================
 * StmtAST
 * ======================= */
void StmtAST::Dump() const {
  if (mode == "-debug") {
    cout << "StmtAST { " << number << " }";
    return;
  }

  if (mode == "-koopa") {
    cout << "  ret " << number << endl;
    return;
  }
}
