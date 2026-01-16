%code requires {
  #include <memory>
  #include <string>
  #include <utility>
  #include <vector>
  #include "include/ast.hpp"
}

%{

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "include/ast.hpp"

// 声明 lexer 函数和错误处理函数
int yylex();
void yyerror(std::unique_ptr<BaseAST> &ast, const char *s);

using namespace std;

%}

// 定义 parser 函数和错误处理函数的附加参数
// 我们需要返回一个字符串作为 AST, 所以我们把附加参数定义成字符串的智能指针
// 解析完成后, 我们要手动修改这个参数, 把它设置成解析得到的字符串
%parse-param { std::unique_ptr<BaseAST> &ast }

// yylval 的定义, 我们把它定义成了一个联合体 (union)
// 因为 token 的值有的是字符串指针, 有的是整数
// 之前我们在 lexer 中用到的 str_val 和 int_val 就是在这里被定义的
// 至于为什么要用字符串指针而不直接用 string 或者 unique_ptr<string>?
// 请自行 STFW 在 union 里写一个带析构函数的类会出现什么情况
%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val;
  ExprAST *expr_val;
  std::vector<BaseAST *> *ast_list;
  std::vector<VarDef> *var_defs;
  std::vector<ConstDef> *const_defs;
  VarDef *var_def;
  ConstDef *const_def;
  std::vector<FuncDefAST::Param> *func_params;
  FuncDefAST::Param *func_param;
  std::vector<ExprAST *> *expr_list;
  std::vector<InitValAST *> *init_list;
  InitValAST *init_val;
}

// lexer 返回的所有 token 种类的声明
// 注意 IDENT 和 INT_CONST 会返回 token 的值, 分别对应 str_val 和 int_val
%token INT VOID RETURN CONST IF ELSE WHILE BREAK CONTINUE
%token <str_val> IDENT
%token <int_val> INT_CONST
%token AND OR EQ NE LE GE

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

// 非终结符的类型定义
// %type <str_val> FuncDef FuncType Block Stmt Number
%type <ast_val> CompUnit CompUnitItem FuncDef Block BlockItem Decl ConstDecl VarDecl Stmt
%type <expr_val> Exp LOrExp LAndExp EqExp RelExp AddExp MulExp UnaryExp PrimaryExp LVal ConstExp
%type <expr_val> Number
%type <str_val> UnaryOp
%type <ast_list> CompUnitItemList CompUnitItemListOpt BlockItemList BlockItemListOpt
%type <var_defs> VarDefList
%type <const_defs> ConstDefList
%type <var_def> VarDef
%type <const_def> ConstDef
%type <func_params> FuncFParams FuncFParamsOpt
%type <func_param> FuncFParam
%type <expr_list> FuncRParams FuncRParamsOpt ArrayDimList ArrayDimListOpt ArrayIndexList ArrayIndexListOpt
%type <init_list> InitValList InitValListOpt
%type <init_val> InitVal

%%

// 开始符, CompUnit ::= FuncDef, 大括号后声明了解析完成后 parser 要做的事情
// 之前我们定义了 FuncDef 会返回一个 str_val, 也就是字符串指针
// 而 parser 一旦解析完 CompUnit, 就说明所有的 token 都被解析了, 即解析结束了
// 此时我们应该把 FuncDef 返回的结果收集起来, 作为 AST 传给调用 parser 的函数
// $1 指代规则里第一个符号的返回值, 也就是 FuncDef 的返回值
CompUnit
  : CompUnitItemListOpt {
    auto comp_unit = make_unique<CompUnitAST>();
    for (auto *item : *$1) {
      comp_unit->items.emplace_back(item);
    }
    delete $1;
    ast = std::move(comp_unit);
  }
  ;

CompUnitItemListOpt
  : %empty {
    $$ = new std::vector<BaseAST *>();
  }
  | CompUnitItemList {
    $$ = $1;
  }
  ;

CompUnitItemList
  : CompUnitItem {
    auto vec = new std::vector<BaseAST *>();
    vec->push_back($1);
    $$ = vec;
  }
  | CompUnitItemList CompUnitItem {
    $1->push_back($2);
    $$ = $1;
  }
  ;

CompUnitItem
  : Decl { $$ = $1; }
  | FuncDef { $$ = $1; }
  ;

// FuncDef ::= FuncType IDENT '(' ')' Block;
// 我们这里可以直接写 '(' 和 ')', 因为之前在 lexer 里已经处理了单个字符的情况
// 解析完成后, 把这些符号的结果收集起来, 然后拼成一个新的字符串, 作为结果返回
// $$ 表示非终结符的返回值, 我们可以通过给这个符号赋值的方法来返回结果
// 你可能会问, FuncType, IDENT 之类的结果已经是字符串指针了
// 为什么还要用 unique_ptr 接住它们, 然后再解引用, 把它们拼成另一个字符串指针呢
// 因为所有的字符串指针都是我们 new 出来的, new 出来的内存一定要 delete
// 否则会发生内存泄漏, 而 unique_ptr 这种智能指针可以自动帮我们 delete
// 虽然此处你看不出用 unique_ptr 和手动 delete 的区别, 但当我们定义了 AST 之后
// 这种写法会省下很多内存管理的负担
FuncDef
  : INT IDENT '(' FuncFParamsOpt ')' Block {
    auto node = new FuncDefAST();
    auto type = new FuncTypeAST();
    type->type = "int";
    node->func_type = unique_ptr<BaseAST>(type);
    node->ident = *unique_ptr<string>($2);
    node->params = std::move(*$4);
    delete $4;
    node->block = unique_ptr<BaseAST>($6);
    $$ = node;
  }
  | VOID IDENT '(' FuncFParamsOpt ')' Block {
    auto node = new FuncDefAST();
    auto type = new FuncTypeAST();
    type->type = "void";
    node->func_type = unique_ptr<BaseAST>(type);
    node->ident = *unique_ptr<string>($2);
    node->params = std::move(*$4);
    delete $4;
    node->block = unique_ptr<BaseAST>($6);
    $$ = node;
  }
  ;

FuncFParamsOpt
  : %empty {
    $$ = new std::vector<FuncDefAST::Param>();
  }
  | FuncFParams {
    $$ = $1;
  }
  ;

FuncFParams
  : FuncFParam {
    auto vec = new std::vector<FuncDefAST::Param>();
    vec->push_back(std::move(*$1));
    delete $1;
    $$ = vec;
  }
  | FuncFParams ',' FuncFParam {
    $1->push_back(std::move(*$3));
    delete $3;
    $$ = $1;
  }
  ;

FuncFParam
  : INT IDENT {
    auto param = new FuncDefAST::Param();
    param->ident = *unique_ptr<string>($2);
    $$ = param;
  }
  | INT IDENT '[' ']' ArrayDimListOpt {
    auto param = new FuncDefAST::Param();
    param->ident = *unique_ptr<string>($2);
    param->is_array = true;
    for (auto *dim : *$5) {
      param->dims.emplace_back(dim);
    }
    delete $5;
    $$ = param;
  }
  ;

ArrayDimListOpt
  : %empty {
    $$ = new std::vector<ExprAST *>();
  }
  | ArrayDimList {
    $$ = $1;
  }
  ;

ArrayDimList
  : ArrayDimList '[' ConstExp ']' {
    $1->push_back($3);
    $$ = $1;
  }
  | '[' ConstExp ']' {
    auto vec = new std::vector<ExprAST *>();
    vec->push_back($2);
    $$ = vec;
  }
  ;

Block
  : '{' BlockItemListOpt '}' {
    auto node = new BlockAST();
    for (auto *item : *$2) {
      node->items.emplace_back(item);
    }
    delete $2;
    $$ = node;
  }
  ;

BlockItemListOpt
  : %empty {
    $$ = new std::vector<BaseAST *>();
  }
  | BlockItemList {
    $$ = $1;
  }
  ;

BlockItemList
  : BlockItem {
    auto vec = new std::vector<BaseAST *>();
    vec->push_back($1);
    $$ = vec;
  }
  | BlockItemList BlockItem {
    $1->push_back($2);
    $$ = $1;
  }
  ;

BlockItem
  : Decl { $$ = $1; }
  | Stmt { $$ = $1; }
  ;

Decl
  : ConstDecl { $$ = $1; }
  | VarDecl { $$ = $1; }
  ;

ConstDecl
  : CONST INT ConstDefList ';' {
    auto node = new ConstDeclAST();
    node->defs = std::move(*$3);
    delete $3;
    $$ = node;
  }
  ;

ConstDefList
  : ConstDefList ',' ConstDef {
    $1->push_back(std::move(*$3));
    delete $3;
    $$ = $1;
  }
  | ConstDef {
    auto vec = new std::vector<ConstDef>();
    vec->push_back(std::move(*$1));
    delete $1;
    $$ = vec;
  }
  ;

ConstDef
  : IDENT ArrayDimListOpt '=' InitVal {
    auto def = new ConstDef();
    def->ident = *unique_ptr<string>($1);
    for (auto *dim : *$2) {
      def->dims.emplace_back(dim);
    }
    delete $2;
    def->init = unique_ptr<InitValAST>($4);
    $$ = def;
  }
  ;

ConstExp
  : Exp { $$ = $1; }
  ;

VarDecl
  : INT VarDefList ';' {
    auto node = new VarDeclAST();
    node->defs = std::move(*$2);
    delete $2;
    $$ = node;
  }
  ;

VarDefList
  : VarDefList ',' VarDef {
    $1->push_back(std::move(*$3));
    delete $3;
    $$ = $1;
  }
  | VarDef {
    auto vec = new std::vector<VarDef>();
    vec->push_back(std::move(*$1));
    delete $1;
    $$ = vec;
  }
  ;

VarDef
  : IDENT ArrayDimListOpt {
    auto def = new VarDef();
    def->ident = *unique_ptr<string>($1);
    for (auto *dim : *$2) {
      def->dims.emplace_back(dim);
    }
    delete $2;
    def->has_init = false;
    $$ = def;
  }
  | IDENT ArrayDimListOpt '=' InitVal {
    auto def = new VarDef();
    def->ident = *unique_ptr<string>($1);
    for (auto *dim : *$2) {
      def->dims.emplace_back(dim);
    }
    delete $2;
    def->init = unique_ptr<InitValAST>($4);
    def->has_init = true;
    $$ = def;
  }
  ;

InitVal
  : Exp {
    auto node = new InitValAST();
    node->is_expr = true;
    node->expr = unique_ptr<ExprAST>($1);
    $$ = node;
  }
  | '{' InitValListOpt '}' {
    auto node = new InitValAST();
    node->is_expr = false;
    for (auto *it : *$2) {
      node->list.emplace_back(it);
    }
    delete $2;
    $$ = node;
  }
  ;

InitValListOpt
  : %empty {
    $$ = new std::vector<InitValAST *>();
  }
  | InitValList {
    $$ = $1;
  }
  ;

InitValList
  : InitVal {
    auto vec = new std::vector<InitValAST *>();
    vec->push_back($1);
    $$ = vec;
  }
  | InitValList ',' InitVal {
    $1->push_back($3);
    $$ = $1;
  }
  ;

Stmt
  : LVal '=' Exp ';' {
    auto node = new AssignStmtAST();
    node->lval = unique_ptr<ExprAST>($1);
    node->value = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | IF '(' Exp ')' Stmt %prec LOWER_THAN_ELSE {
    auto node = new IfStmtAST();
    node->cond = unique_ptr<ExprAST>($3);
    node->then_stmt = unique_ptr<BaseAST>($5);
    $$ = node;
  }
  | IF '(' Exp ')' Stmt ELSE Stmt {
    auto node = new IfStmtAST();
    node->cond = unique_ptr<ExprAST>($3);
    node->then_stmt = unique_ptr<BaseAST>($5);
    node->else_stmt = unique_ptr<BaseAST>($7);
    $$ = node;
  }
  | WHILE '(' Exp ')' Stmt {
    auto node = new WhileStmtAST();
    node->cond = unique_ptr<ExprAST>($3);
    node->body = unique_ptr<BaseAST>($5);
    $$ = node;
  }
  | BREAK ';' {
    $$ = new BreakStmtAST();
  }
  | CONTINUE ';' {
    $$ = new ContinueStmtAST();
  }
  | Exp ';' {
    auto node = new ExprStmtAST();
    node->expr = unique_ptr<ExprAST>($1);
    $$ = node;
  }
  | ';' {
    $$ = new EmptyStmtAST();
  }
  | Block { $$ = $1; }
  | RETURN Exp ';' {
    auto node = new ReturnStmtAST();
    node->value = unique_ptr<ExprAST>($2);
    $$ = node;
  }
  | RETURN ';' {
    auto node = new ReturnStmtAST();
    $$ = node;
  }
  ;

Exp
  : LOrExp { $$ = $1; }
  ;

LOrExp
  : LOrExp OR LAndExp {
    auto node = new BinaryExpAST();
    node->op = "||";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | LAndExp { $$ = $1; }
  ;

LAndExp
  : LAndExp AND EqExp {
    auto node = new BinaryExpAST();
    node->op = "&&";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | EqExp { $$ = $1; }
  ;

EqExp
  : EqExp EQ RelExp {
    auto node = new BinaryExpAST();
    node->op = "==";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | EqExp NE RelExp {
    auto node = new BinaryExpAST();
    node->op = "!=";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | RelExp { $$ = $1; }
  ;

RelExp
  : RelExp '<' AddExp {
    auto node = new BinaryExpAST();
    node->op = "<";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | RelExp '>' AddExp {
    auto node = new BinaryExpAST();
    node->op = ">";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | RelExp LE AddExp {
    auto node = new BinaryExpAST();
    node->op = "<=";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | RelExp GE AddExp {
    auto node = new BinaryExpAST();
    node->op = ">=";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | AddExp { $$ = $1; }
  ;

AddExp
  : AddExp '+' MulExp {
    auto node = new BinaryExpAST();
    node->op = "+";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | AddExp '-' MulExp {
    auto node = new BinaryExpAST();
    node->op = "-";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | MulExp { $$ = $1; }
  ;

MulExp
  : MulExp '*' UnaryExp {
    auto node = new BinaryExpAST();
    node->op = "*";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | MulExp '/' UnaryExp {
    auto node = new BinaryExpAST();
    node->op = "/";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | MulExp '%' UnaryExp {
    auto node = new BinaryExpAST();
    node->op = "%";
    node->lhs = unique_ptr<ExprAST>($1);
    node->rhs = unique_ptr<ExprAST>($3);
    $$ = node;
  }
  | UnaryExp { $$ = $1; }
  ;

UnaryExp
  : PrimaryExp { $$ = $1; }
  | UnaryOp UnaryExp {
    auto node = new UnaryExpAST();
    node->op = *unique_ptr<string>($1);
    node->rhs = unique_ptr<ExprAST>($2);
    $$ = node;
  }
  | IDENT '(' FuncRParamsOpt ')' {
    auto node = new CallExpAST();
    node->ident = *unique_ptr<string>($1);
    for (auto *arg : *$3) {
      node->args.emplace_back(arg);
    }
    delete $3;
    $$ = node;
  }
  ;

UnaryOp
  : '+' { $$ = new string("+"); }
  | '-' { $$ = new string("-"); }
  | '!' { $$ = new string("!"); }
  ;

PrimaryExp
  : '(' Exp ')' { $$ = $2; }
  | LVal { $$ = $1; }
  | Number { $$ = $1; }
  ;

LVal
  : IDENT ArrayIndexListOpt {
    auto node = new LValAST();
    node->ident = *unique_ptr<string>($1);
    for (auto *idx : *$2) {
      node->indices.emplace_back(idx);
    }
    delete $2;
    $$ = node;
  }
  ;

ArrayIndexListOpt
  : %empty {
    $$ = new std::vector<ExprAST *>();
  }
  | ArrayIndexList {
    $$ = $1;
  }
  ;

ArrayIndexList
  : ArrayIndexList '[' Exp ']' {
    $1->push_back($3);
    $$ = $1;
  }
  | '[' Exp ']' {
    auto vec = new std::vector<ExprAST *>();
    vec->push_back($2);
    $$ = vec;
  }
  ;

Number
  : INT_CONST {
    auto node = new NumberAST();
    node->value = $1;
    $$ = node;
  }
  ;

FuncRParamsOpt
  : %empty {
    $$ = new std::vector<ExprAST *>();
  }
  | FuncRParams {
    $$ = $1;
  }
  ;

FuncRParams
  : Exp {
    auto vec = new std::vector<ExprAST *>();
    vec->push_back($1);
    $$ = vec;
  }
  | FuncRParams ',' Exp {
    $1->push_back($3);
    $$ = $1;
  }
  ;

%%

// 定义错误处理函数, 其中第二个参数是错误信息
// parser 如果发生错误 (例如输入的程序出现了语法错误), 就会调用这个函数
void yyerror(unique_ptr<BaseAST> &ast, const char *s) {
  cerr << "error: " << s << endl;
}
