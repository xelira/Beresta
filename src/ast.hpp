#pragma once

#include <memory>
#include <string>
#include <vector>

#include "symbol.hpp"
#include "types.hpp"

struct Node {
    int line = 0;
    int col = 0;
    virtual ~Node() = default;
};

struct Expr : Node {
    TypeKind type = TypeKind::Void;
    bool has_type = false;
    //: для выражений-массивов
    TypeKind element = TypeKind::Void;
    int count = 0;
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntLit : Expr {
    long long value = 0;
    explicit IntLit(long long v) : value(v) {}
};

struct FloatLit : Expr {
    double value = 0.0;
    explicit FloatLit(double v) : value(v) {}
};

struct StringLit : Expr {
    std::string value;
    explicit StringLit(std::string v) : value(std::move(v)) {}
};

struct BoolLit : Expr {
    bool value = false;
    explicit BoolLit(bool v) : value(v) {}
};

struct Ident : Expr {
    std::string name;
    Symbol* symbol = nullptr;
    explicit Ident(std::string n) : name(std::move(n)) {}
};

struct Unary : Expr {
    std::string op;
    ExprPtr operand;
    Unary(std::string o, ExprPtr e) : op(std::move(o)), operand(std::move(e)) {}
};

struct Binary : Expr {
    std::string op;
    ExprPtr left;
    ExprPtr right;
    bool str_op = false;
    Binary(std::string o, ExprPtr l, ExprPtr r)
        : op(std::move(o)), left(std::move(l)), right(std::move(r)) {}
};

struct Call : Expr {
    std::string name;
    std::vector<ExprPtr> args;
    std::string cname;
    bool is_builtin = false;
    explicit Call(std::string n) : name(std::move(n)) {}
};

//: Обращение к элементу массива: а[i]
struct Index : Expr {
    std::string name;
    ExprPtr index;
    Symbol* symbol = nullptr;
    explicit Index(std::string n) : name(std::move(n)) {}
};

struct Stmt : Node {};
using StmtPtr = std::unique_ptr<Stmt>;

struct VarDecl : Stmt {
    std::string name;
    std::string type_name;
    TypeKind declared = TypeKind::Void;
    ExprPtr value;
    Symbol* symbol = nullptr;
    bool is_array = false;
    int array_count = 0;
};

struct IndexAssign : Stmt {
    std::string name;
    ExprPtr index;
    ExprPtr value;
    Symbol* symbol = nullptr;
};

struct ConstDecl : Stmt {
    std::string name;
    std::string type_name;
    TypeKind declared = TypeKind::Void;
    ExprPtr value;
    Symbol* symbol = nullptr;
};

struct Assign : Stmt {
    std::string name;
    ExprPtr value;
    Symbol* symbol = nullptr;
};

struct Print : Stmt {
    ExprPtr value;
};

struct ExprStmt : Stmt {
    ExprPtr value;
};

struct Block : Stmt {
    std::vector<StmtPtr> body;
};

struct If : Stmt {
    ExprPtr cond;
    StmtPtr then_branch;
    StmtPtr otherwise;
};

struct While : Stmt {
    ExprPtr cond;
    StmtPtr body;
};

struct For : Stmt {
    std::string var;
    ExprPtr start;
    ExprPtr stop;
    ExprPtr step;
    StmtPtr body;
    Symbol* symbol = nullptr;
    //: слоты для границы и шага, которые вычисляются один раз
    int slot_stop = -1;
    int slot_step = -1;
};

struct Return : Stmt {
    ExprPtr value;
};

//: для каждого х в а { ... }
struct ForEach : Stmt {
    std::string var;
    std::string array;
    StmtPtr body;
    Symbol* symbol = nullptr;  // переменная цикла
    Symbol* source = nullptr;  // массив
    int counter_slot = -1;
};

struct Break : Stmt {};
struct Continue : Stmt {};

struct Param : Node {
    std::string name;
    std::string type_name;
    TypeKind declared = TypeKind::Void;
    Symbol* symbol = nullptr;
};

struct FuncDecl : Stmt {
    std::string name;
    std::vector<Param> params;
    std::string return_type_name;
    TypeKind ret = TypeKind::Void;
    StmtPtr body;
    Symbol* symbol = nullptr;
};

struct Program : Node {
    std::vector<StmtPtr> decls;
    StmtPtr entry;  // Block
    //: Все символы программы. Дерево ссылается на них сырыми указателями.
    std::vector<std::unique_ptr<Symbol>> symbols;
};
