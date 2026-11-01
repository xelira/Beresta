#include "sema.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "diagnostic.hpp"

using TK = TypeKind;

namespace {

const std::unordered_map<std::string, std::vector<Overload>>& builtin_table() {
    static const std::unordered_map<std::string, std::vector<Overload>> table = {
        {"длина", {Overload{{TK::String}, TK::Int, "b_str_len"}}},
        {"из_строки", {Overload{{TK::String}, TK::Int, "b_str_to_int"}}},
        {"внемли", {Overload{{}, TK::String, "b_read_line"}}},
        {"слушает", {Overload{{TK::String}, TK::String, "b_read_file"}}},
        {"патриот", {Overload{{TK::String}, TK::String, "b_read_file"}}},
        {"пишет", {Overload{{TK::String, TK::String}, TK::Bool, "b_write_file"}}},
        {"в_число", {Overload{{TK::Float}, TK::Int, "#to_int"}}},
        {"в_дробь", {Overload{{TK::Int}, TK::Float, "#to_float"}}},
        {"к_строке",
         {
             Overload{{TK::Int}, TK::String, "b_str_from_int"},
             Overload{{TK::Float}, TK::String, "b_str_from_float"},
             Overload{{TK::Bool}, TK::String, "b_str_from_bool"},
             Overload{{TK::String}, TK::String, ""},
         }},
    };
    return table;
}

std::string signature(const std::string& name, const std::vector<TK>& params, TK ret) {
    std::string result = name + "(";
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i > 0) result += ", ";
        result += type_name(params[i]);
    }
    result += ") -> ";
    result += type_name(ret);
    return result;
}

class Scope {
public:
    explicit Scope(Scope* parent = nullptr) : parent_(parent) {}

    Scope* parent() const { return parent_; }

    Symbol* lookup(const std::string& name) const {
        for (const Scope* scope = this; scope != nullptr; scope = scope->parent_) {
            const auto it = scope->names_.find(name);
            if (it != scope->names_.end()) return it->second;
        }
        return nullptr;
    }

    void define(Symbol* symbol) {
        const auto it = names_.find(symbol->name);
        if (it != names_.end()) {
            const Symbol* previous = it->second;
            fail("имя «" + symbol->name + "» уже занято", symbol->line, symbol->col,
                 static_cast<int>(symbol->name.size()),
                 previous->line > 0 ? "впервые объявлено в строке " + std::to_string(previous->line)
                                    : "");
        }
        names_[symbol->name] = symbol;
    }

private:
    Scope* parent_;
    std::unordered_map<std::string, Symbol*> names_;
};

class Checker {
public:
    explicit Checker(Program& program) : program_(program) {}

    void run() {
        push_scope();
        install_builtins();

        // 1. Объявляем функции, чтобы работала рекурсия и вызовы «вперёд».
        for (auto& decl : program_.decls) {
            if (auto* func = dynamic_cast<FuncDecl*>(decl.get())) declare_func(*func);
        }

        // 2. Константы.
        for (auto& decl : program_.decls) {
            if (auto* constant = dynamic_cast<ConstDecl*>(decl.get())) check_const(*constant);
        }

        // 3. Тела функций.
        for (auto& decl : program_.decls) {
            if (auto* func = dynamic_cast<FuncDecl*>(decl.get())) check_func_body(*func);
        }

        // 4. Точка входа.
        auto* entry = dynamic_cast<Block*>(program_.entry.get());
        if (entry == nullptr) fail("повреждено дерево: береста — не блок");

        push_scope();
        check_stmts(entry->body);
        pop_scope();
    }

private:
    // --- служебное ---------------------------------------------------

    Symbol* add(Symbol symbol) {
        program_.symbols.push_back(std::make_unique<Symbol>(std::move(symbol)));
        return program_.symbols.back().get();
    }

    std::string fresh_cname(const std::string& prefix) {
        ++counter_;
        return prefix + "_" + std::to_string(counter_);
    }

    void push_scope() {
        scopes_.push_back(std::make_unique<Scope>(scope_));
        scope_ = scopes_.back().get();
    }

    void pop_scope() { scope_ = scope_->parent(); }

    TK resolve_type(const std::string& word, int line, int col) {
        const auto type = type_from_word(word);
        if (!type) fail("неизвестный тип «" + word + "»", line, col, static_cast<int>(word.size()));
        return *type;
    }

    void install_builtins() {
        for (const auto& [name, overloads] : builtin_table()) {
            Symbol symbol;
            symbol.name = name;
            symbol.kind = Symbol::Kind::Builtin;
            symbol.overloads = &overloads;
            scope_->define(add(std::move(symbol)));
        }
    }

    // --- константы ---------------------------------------------------

    void check_const(ConstDecl& decl) {
        const TK declared = resolve_type(decl.type_name, decl.line, decl.col);
        if (declared == TK::Void) {
            fail("устой не может быть типа ничто", decl.line, decl.col,
                 static_cast<int>(decl.name.size()), "константа должна что-то хранить");
        }

        const TK value_type = check_expr(*decl.value);
        if (!assignable(value_type, declared)) {
            fail("устой «" + decl.name + "» типа " + type_name(declared) +
                     ", а значение типа " + type_name(value_type),
                 decl.value->line, decl.value->col, 1,
                 std::string("приведите значение к ") + type_name(declared));
        }

        if (!is_const_expr(*decl.value)) {
            fail("устой «" + decl.name + "» требует литерального значения", decl.value->line,
                 decl.value->col, 1,
                 "константы кладутся в статическую память и не вычисляются в рантайме");
        }

        Symbol symbol;
        symbol.name = decl.name;
        symbol.type = declared;
        symbol.kind = Symbol::Kind::Const;
        symbol.cname = fresh_cname("c");
        symbol.line = decl.line;
        symbol.col = decl.col;
        symbol.global = true;
        Symbol* symbol_ptr = add(std::move(symbol));
        scope_->define(symbol_ptr);
        decl.symbol = symbol_ptr;
    }

    static bool is_const_expr(const Expr& expr) {
        if (dynamic_cast<const IntLit*>(&expr) || dynamic_cast<const FloatLit*>(&expr) ||
            dynamic_cast<const StringLit*>(&expr) || dynamic_cast<const BoolLit*>(&expr)) {
            return true;
        }
        if (const auto* unary = dynamic_cast<const Unary*>(&expr)) {
            return unary->op == "-" && is_const_expr(*unary->operand);
        }
        return false;
    }

    // --- функции -----------------------------------------------------

    void declare_func(FuncDecl& decl) {
        std::vector<TK> param_types;
        for (Param& param : decl.params) {
            for (const Param& earlier : decl.params) {
                if (&earlier == &param) break;
                if (earlier.name == param.name) {
                    fail("параметр «" + param.name + "» повторяется", param.line, param.col,
                         static_cast<int>(param.name.size()));
                }
            }
            param_types.push_back(resolve_type(param.type_name, param.line, param.col));
        }

        Symbol symbol;
        symbol.name = decl.name;
        symbol.kind = Symbol::Kind::Func;
        symbol.type = resolve_type(decl.return_type_name, decl.line, decl.col);
        symbol.ret = symbol.type;
        symbol.params = param_types;

        int usual_params = 0;
        int float_params = 0;
        for (TK type : param_types) {
            if (type == TK::Float) {
                ++float_params;
            } else {
                ++usual_params;
            }
        }
        if (usual_params > 6) {
            fail("слишком много параметров у «" + decl.name + "»", decl.line, decl.col,
                 static_cast<int>(decl.name.size()),
                 "бэкенд x86-64 передаёт не больше 6 обычных параметров");
        }
        if (float_params > 8) {
            fail("слишком много параметров-дробей у «" + decl.name + "»", decl.line, decl.col,
                 static_cast<int>(decl.name.size()),
                 "бэкенд x86-64 передаёт не больше 8 дробных параметров");
        }
        symbol.cname = fresh_cname("f");
        symbol.line = decl.line;
        symbol.col = decl.col;
        decl.symbol = add(std::move(symbol));
        scope_->define(decl.symbol);
    }

    void check_func_body(FuncDecl& decl) {
        const std::optional<TK> outer_return = current_return_;
        current_return_ = decl.symbol->ret;

        push_scope();
        for (Param& param : decl.params) {
            Symbol symbol;
            symbol.name = param.name;
            symbol.type = param.declared;
            symbol.kind = Symbol::Kind::Param;
            symbol.cname = fresh_cname("p");
            symbol.line = param.line;
            symbol.col = param.col;
            param.symbol = add(std::move(symbol));
            scope_->define(param.symbol);
        }

        auto* body = dynamic_cast<Block*>(decl.body.get());
        check_stmts(body->body);

        if (decl.symbol->ret != TK::Void && !has_return(*body)) {
            fail("дело «" + decl.name + "» обещает вернуть " + type_name(decl.symbol->ret) +
                     ", но не возвращает",
                 decl.line, decl.col, static_cast<int>(decl.name.size()),
                 "добавьте «вернуть ...;» или сделайте функцию типа ничто");
        }

        pop_scope();
        current_return_ = outer_return;
    }

    bool has_return(const Stmt& stmt) {
        if (dynamic_cast<const Return*>(&stmt)) return true;
        if (const auto* block = dynamic_cast<const Block*>(&stmt)) {
            for (const auto& inner : block->body) {
                if (has_return(*inner)) return true;
            }
            return false;
        }
        if (const auto* node = dynamic_cast<const If*>(&stmt)) {
            if (node->then_branch && has_return(*node->then_branch)) return true;
            return node->otherwise && has_return(*node->otherwise);
        }
        if (const auto* node = dynamic_cast<const While*>(&stmt)) {
            return node->body && has_return(*node->body);
        }
        if (const auto* node = dynamic_cast<const For*>(&stmt)) {
            return node->body && has_return(*node->body);
        }
        return false;
    }

    // --- операторы ---------------------------------------------------

    void check_stmts(const std::vector<StmtPtr>& stmts) {
        for (const auto& stmt : stmts) check_stmt(*stmt);
    }

    void check_stmt(Stmt& stmt) {
        if (auto* node = dynamic_cast<VarDecl*>(&stmt)) return check_var(*node);
        if (auto* node = dynamic_cast<Block*>(&stmt)) {
            push_scope();
            check_stmts(node->body);
            pop_scope();
            return;
        }
        if (auto* node = dynamic_cast<Assign*>(&stmt)) return check_assign(*node);
        if (auto* node = dynamic_cast<Print*>(&stmt)) return check_print(*node);
        if (auto* node = dynamic_cast<ExprStmt*>(&stmt)) {
            check_expr(*node->value);
            return;
        }
        if (auto* node = dynamic_cast<If*>(&stmt)) return check_if(*node);
        if (auto* node = dynamic_cast<While*>(&stmt)) return check_while(*node);
        if (auto* node = dynamic_cast<For*>(&stmt)) return check_for(*node);
        if (auto* node = dynamic_cast<ForEach*>(&stmt)) return check_foreach(*node);
        if (auto* node = dynamic_cast<IndexAssign*>(&stmt)) return check_index_assign(*node);
        if (auto* node = dynamic_cast<Return*>(&stmt)) return check_return(*node);
        if (auto* node = dynamic_cast<Break*>(&stmt)) {
            if (loop_depth_ == 0) {
                fail("«прервать» вне цикла", node->line, node->col, 9,
                     "это слово работает только внутри «доколе» или «повтор»");
            }
            return;
        }
        if (auto* node = dynamic_cast<Continue*>(&stmt)) {
            if (loop_depth_ == 0) {
                fail("«продолжить» вне цикла", node->line, node->col, 10,
                     "это слово работает только внутри «доколе» или «повтор»");
            }
            return;
        }
        fail("неизвестный оператор", stmt.line, stmt.col);
    }

    void check_var(VarDecl& decl) {
        const TK declared = resolve_type(decl.type_name, decl.line, decl.col);

        if (decl.is_array) {
            if (declared == TK::Void) {
                fail("массив не может хранить ничто", decl.line, decl.col,
                     static_cast<int>(decl.name.size()));
            }
            if (decl.array_count <= 0) {
                fail("длина массива должна быть положительной", decl.line, decl.col, 1);
            }

            Symbol symbol;
            symbol.name = decl.name;
            symbol.type = TK::Array;
            symbol.element = declared;
            symbol.count = decl.array_count;
            symbol.kind = Symbol::Kind::Var;
            symbol.cname = fresh_cname("b");
            symbol.line = decl.line;
            symbol.col = decl.col;
            decl.symbol = add(std::move(symbol));
            scope_->define(decl.symbol);
            return;
        }

        if (declared == TK::Void) {
            fail("переменная «" + decl.name + "» не может быть типа ничто", decl.line, decl.col,
                 static_cast<int>(decl.name.size()),
                 "ничто — это отсутствие значения, его нельзя хранить");
        }

        if (decl.value) {
            const TK value_type = check_expr(*decl.value);
            if (!assignable(value_type, declared)) {
                fail("«" + decl.name + "» объявлена как " + type_name(declared) +
                         ", а значение типа " + type_name(value_type),
                     decl.value->line, decl.value->col, 1,
                     std::string("положите сюда ") + type_name(declared) +
                         " или смените тип переменной");
            }
        }

        Symbol symbol;
        symbol.name = decl.name;
        symbol.type = declared;
        symbol.kind = Symbol::Kind::Var;
        symbol.cname = fresh_cname("b");
        symbol.line = decl.line;
        symbol.col = decl.col;
        decl.symbol = add(std::move(symbol));
        scope_->define(decl.symbol);
    }

    void check_assign(Assign& stmt) {
        Symbol* symbol = scope_->lookup(stmt.name);
        if (symbol == nullptr) {
            fail("переменная «" + stmt.name + "» не объявлена", stmt.line, stmt.col,
                 static_cast<int>(stmt.name.size()),
                 "сначала «слово имя: тип = значение;»");
        }
        if (symbol->kind == Symbol::Kind::Func || symbol->kind == Symbol::Kind::Builtin) {
            fail("«" + stmt.name + "» — функция, её нельзя переприсвоить", stmt.line, stmt.col,
                 static_cast<int>(stmt.name.size()));
        }
        if (symbol->kind == Symbol::Kind::Const) {
            fail("«" + stmt.name + "» — устой, его нельзя менять", stmt.line, stmt.col,
                 static_cast<int>(stmt.name.size()), "устой — это константа");
        }
        if (symbol->type == TK::Array) {
            fail("массив «" + stmt.name + "» нельзя присвоить целиком", stmt.line, stmt.col,
                 static_cast<int>(stmt.name.size()),
                 "меняйте элементы: " + stmt.name + "[i] = значение;");
        }

        const TK value_type = check_expr(*stmt.value);
        if (!assignable(value_type, symbol->type)) {
            fail("в «" + stmt.name + "» типа " + type_name(symbol->type) + " нельзя положить " +
                     type_name(value_type),
                 stmt.value->line, stmt.value->col, 1,
                 std::string("нужно значение типа ") + type_name(symbol->type));
        }
        stmt.symbol = symbol;
    }

    void check_print(Print& stmt) {
        const TK value_type = check_expr(*stmt.value);
        if (value_type == TK::Void) {
            fail("нечего молвить: выражение ничего не возвращает", stmt.value->line,
                 stmt.value->col);
        }
        if (value_type == TK::Array) {
            fail("массив нельзя молвить целиком", stmt.value->line, stmt.value->col, 1,
                 "напечатайте элементы по одному");
        }
    }

    void check_if(If& stmt) {
        const TK cond_type = check_expr(*stmt.cond);
        if (cond_type != TK::Bool) {
            fail(std::string("условие ежели должно быть флагом, а оно ") + type_name(cond_type),
                 stmt.cond->line, stmt.cond->col, 1,
                 "сравните значения: «ежели х > 0 { ... }»");
        }
        check_stmt(*stmt.then_branch);
        if (stmt.otherwise) check_stmt(*stmt.otherwise);
    }

    void check_while(While& stmt) {
        const TK cond_type = check_expr(*stmt.cond);
        if (cond_type != TK::Bool) {
            fail(std::string("условие доколе должно быть флагом, а оно ") + type_name(cond_type),
                 stmt.cond->line, stmt.cond->col);
        }
        ++loop_depth_;
        check_stmt(*stmt.body);
        --loop_depth_;
    }

    void check_for(For& stmt) {
        const TK start_type = check_expr(*stmt.start);
        if (start_type != TK::Int) {
            fail(std::string("начало повтора должно быть числом, а оно ") +
                     type_name(start_type),
                 stmt.start->line, stmt.start->col);
        }
        const TK stop_type = check_expr(*stmt.stop);
        if (stop_type != TK::Int) {
            fail(std::string("конец повтора должно быть числом, а оно ") + type_name(stop_type),
                 stmt.stop->line, stmt.stop->col);
        }
        if (stmt.step) {
            const TK step_type = check_expr(*stmt.step);
            if (step_type != TK::Int) {
                fail(std::string("шаг повтора должен быть числом, а он ") + type_name(step_type),
                     stmt.step->line, stmt.step->col);
            }
            const auto* literal = dynamic_cast<const IntLit*>(stmt.step.get());
            if (literal != nullptr && literal->value == 0) {
                fail("шаг повтора не может быть нулём", stmt.step->line, stmt.step->col, 1,
                     "иначе цикл никогда не закончится");
            }
        }

        push_scope();
        Symbol symbol;
        symbol.name = stmt.var;
        symbol.type = TK::Int;
        symbol.kind = Symbol::Kind::Loop;
        symbol.cname = fresh_cname("i");
        symbol.line = stmt.line;
        symbol.col = stmt.col;
        stmt.symbol = add(std::move(symbol));
        scope_->define(stmt.symbol);

        ++loop_depth_;
        check_stmt(*stmt.body);
        --loop_depth_;
        pop_scope();
    }

    void check_foreach(ForEach& stmt) {
        Symbol* source = scope_->lookup(stmt.array);
        if (source == nullptr) {
            fail("неизвестный массив «" + stmt.array + "»", stmt.line, stmt.col,
                 static_cast<int>(stmt.array.size()),
                 "сначала объявите его: слово имя: [N]число;");
        }
        if (source->type != TK::Array) {
            fail("«" + stmt.array + "» — не массив", stmt.line, stmt.col,
                 static_cast<int>(stmt.array.size()),
                 "для каждого работает только с массивами");
        }
        stmt.source = source;

        push_scope();
        Symbol symbol;
        symbol.name = stmt.var;
        symbol.type = source->element;
        symbol.kind = Symbol::Kind::Loop;
        symbol.cname = fresh_cname("e");
        symbol.line = stmt.line;
        symbol.col = stmt.col;
        stmt.symbol = add(std::move(symbol));
        scope_->define(stmt.symbol);

        ++loop_depth_;
        check_stmt(*stmt.body);
        --loop_depth_;
        pop_scope();
    }

    TK check_index(Index& expr) {
        Symbol* symbol = scope_->lookup(expr.name);
        if (symbol == nullptr) {
            fail("неизвестный массив «" + expr.name + "»", expr.line, expr.col,
                 static_cast<int>(expr.name.size()));
        }
        if (symbol->type != TK::Array) {
            fail("«" + expr.name + "» — не массив, индексация невозможна", expr.line, expr.col,
                 static_cast<int>(expr.name.size()));
        }

        const TK index_type = check_expr(*expr.index);
        if (index_type != TK::Int) {
            fail(std::string("индекс массива должен быть числом, а он ") + type_name(index_type),
                 expr.index->line, expr.index->col);
        }

        expr.symbol = symbol;
        expr.element = symbol->element;
        expr.count = symbol->count;
        return record(expr, symbol->element);
    }

    void check_index_assign(IndexAssign& stmt) {
        Symbol* symbol = scope_->lookup(stmt.name);
        if (symbol == nullptr) {
            fail("неизвестный массив «" + stmt.name + "»", stmt.line, stmt.col,
                 static_cast<int>(stmt.name.size()));
        }
        if (symbol->type != TK::Array) {
            fail("«" + stmt.name + "» — не массив", stmt.line, stmt.col,
                 static_cast<int>(stmt.name.size()));
        }

        const TK index_type = check_expr(*stmt.index);
        if (index_type != TK::Int) {
            fail(std::string("индекс массива должен быть числом, а он ") + type_name(index_type),
                 stmt.index->line, stmt.index->col);
        }

        const TK value_type = check_expr(*stmt.value);
        if (!assignable(value_type, symbol->element)) {
            fail(std::string("в элемент массива «") + stmt.name + "» типа " +
                     type_name(symbol->element) + " нельзя положить " + type_name(value_type),
                 stmt.value->line, stmt.value->col);
        }

        stmt.symbol = symbol;
    }

    void check_return(Return& stmt) {
        if (!current_return_) {
            fail("«вернуть» вне дела", stmt.line, stmt.col, 1,
                 "возврат работает только внутри функции");
        }

        if (stmt.value) {
            const TK value_type = check_expr(*stmt.value);
            if (*current_return_ == TK::Void) {
                fail("функция типа ничто не возвращает значение", stmt.value->line,
                     stmt.value->col, 1, "напишите просто «вернуть;»");
            }
            if (!assignable(value_type, *current_return_)) {
                fail(std::string("функция обещает вернуть ") + type_name(*current_return_) +
                         ", а возвращает " + type_name(value_type),
                     stmt.value->line, stmt.value->col);
            }
        } else if (*current_return_ != TK::Void) {
            fail(std::string("функция должна вернуть ") + type_name(*current_return_) +
                     ", а «вернуть» пустой",
                 stmt.line, stmt.col, 1, "добавьте значение после «вернуть»");
        }
    }

    // --- выражения ---------------------------------------------------

    TK record(Expr& expr, TK type) {
        expr.type = type;
        expr.has_type = true;
        return type;
    }

    //: Тип уже проверенного выражения (нужен для сообщений без повторной проверки).
    TK check_expr_cached(Expr& expr) { return expr.has_type ? expr.type : check_expr(expr); }

    TK check_expr(Expr& expr) {
        if (auto* node = dynamic_cast<IntLit*>(&expr)) return record(*node, TK::Int);
        if (auto* node = dynamic_cast<FloatLit*>(&expr)) return record(*node, TK::Float);
        if (auto* node = dynamic_cast<StringLit*>(&expr)) return record(*node, TK::String);
        if (auto* node = dynamic_cast<BoolLit*>(&expr)) return record(*node, TK::Bool);
        if (auto* node = dynamic_cast<Ident*>(&expr)) return check_ident(*node);
        if (auto* node = dynamic_cast<Unary*>(&expr)) return check_unary(*node);
        if (auto* node = dynamic_cast<Binary*>(&expr)) return check_binary(*node);
        if (auto* node = dynamic_cast<Call*>(&expr)) return check_call(*node);
        if (auto* node = dynamic_cast<Index*>(&expr)) return check_index(*node);
        fail("неизвестное выражение", expr.line, expr.col);
    }

    TK check_ident(Ident& expr) {
        Symbol* symbol = scope_->lookup(expr.name);
        if (symbol == nullptr) {
            fail("неизвестное имя «" + expr.name + "»", expr.line, expr.col,
                 static_cast<int>(expr.name.size()), "объявите переменную через «слово»");
        }
        if (symbol->kind == Symbol::Kind::Func || symbol->kind == Symbol::Kind::Builtin) {
            fail("«" + expr.name + "» — функция, её нужно вызвать", expr.line, expr.col,
                 static_cast<int>(expr.name.size()),
                 "например: " + expr.name + "(...)");
        }
        expr.symbol = symbol;
        if (symbol->type == TK::Array) {
            expr.element = symbol->element;
            expr.count = symbol->count;
        }
        return record(expr, symbol->type);
    }

    TK check_unary(Unary& expr) {
        const TK operand = check_expr(*expr.operand);

        if (expr.op == "-") {
            if (!is_numeric(operand)) {
                fail(std::string("минус не работает с типом ") + type_name(operand), expr.line,
                     expr.col);
            }
            return record(expr, operand);
        }

        if (expr.op == "!" || expr.op == "не") {
            if (operand != TK::Bool) {
                fail("«" + expr.op + "» работает только с флагом, а тут " + type_name(operand),
                     expr.line, expr.col);
            }
            return record(expr, TK::Bool);
        }

        fail("неизвестный унарный оператор «" + expr.op + "»", expr.line, expr.col);
    }

    TK check_binary(Binary& expr) {
        const TK left = check_expr(*expr.left);
        const TK right = check_expr(*expr.right);
        const std::string& op = expr.op;

        if (op == "+") {
            if (left == TK::String || right == TK::String) {
                if (left != TK::String || right != TK::String) {
                    fail(std::string("строку можно склеивать только со строкой, а тут ") +
                             type_name(left) + " + " + type_name(right),
                         expr.line, expr.col, 1, "оберните число в к_строке(число)");
                }
                expr.str_op = true;
                return record(expr, TK::String);
            }
            const auto numeric = common_numeric(left, right);
            if (!numeric) {
                fail(std::string("«+» не работает с ") + type_name(left) + " и " +
                         type_name(right),
                     expr.line, expr.col);
            }
            return record(expr, *numeric);
        }

        if (op == "-" || op == "*" || op == "/") {
            const auto numeric = common_numeric(left, right);
            if (!numeric) {
                fail("«" + op + "» не работает с " + type_name(left) + " и " + type_name(right),
                     expr.line, expr.col);
            }
            return record(expr, *numeric);
        }

        if (op == "%") {
            if (left != TK::Int || right != TK::Int) {
                fail(std::string("«%» работает только с числами, а тут ") + type_name(left) +
                         " и " + type_name(right),
                     expr.line, expr.col, 1, "для дроби используйте деление и вычитание");
            }
            return record(expr, TK::Int);
        }

        if (op == "==" || op == "!=") {
            if (left == TK::Void || right == TK::Void) {
                fail("нельзя сравнивать ничто", expr.line, expr.col);
            }
            if (is_numeric(left) && is_numeric(right)) return record(expr, TK::Bool);
            if (left != right) {
                fail("нельзя сравнивать " + std::string(type_name(left)) + " и " +
                         type_name(right),
                     expr.line, expr.col);
            }
            if (left == TK::String) expr.str_op = true;
            return record(expr, TK::Bool);
        }

        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (is_numeric(left) && is_numeric(right)) return record(expr, TK::Bool);
            if (left == TK::String && right == TK::String) {
                expr.str_op = true;
                return record(expr, TK::Bool);
            }
            fail("«" + op + "» не сравнивает " + type_name(left) + " и " + type_name(right),
                 expr.line, expr.col);
        }

        if (op == "и" || op == "или") {
            if (left != TK::Bool || right != TK::Bool) {
                fail("«" + op + "» работает только с флагами, а тут " + type_name(left) + " и " +
                         type_name(right),
                     expr.line, expr.col);
            }
            return record(expr, TK::Bool);
        }

        fail("неизвестный оператор «" + op + "»", expr.line, expr.col);
    }

    TK check_call(Call& expr) {
        Symbol* symbol = scope_->lookup(expr.name);
        if (symbol == nullptr) {
            fail("функция «" + expr.name + "» не объявлена", expr.line, expr.col,
                 static_cast<int>(expr.name.size()), "объявите её через «дело»");
        }
        if (symbol->kind != Symbol::Kind::Func && symbol->kind != Symbol::Kind::Builtin) {
            fail("«" + expr.name + "» — не функция, её нельзя вызвать", expr.line, expr.col,
                 static_cast<int>(expr.name.size()));
        }

        std::vector<TK> arg_types;
        arg_types.reserve(expr.args.size());
        for (auto& arg : expr.args) arg_types.push_back(check_expr(*arg));

        // длина массива известна на этапе компиляции
        if (expr.name == "длина" && arg_types.size() == 1 && arg_types[0] == TK::Array) {
            expr.cname = "#len";
            expr.is_builtin = true;
            expr.count = expr.args[0]->count;
            return record(expr, TK::Int);
        }

        if (symbol->kind == Symbol::Kind::Builtin) {
            for (const Overload& overload : *symbol->overloads) {
                if (overload.params.size() != arg_types.size()) continue;

                bool matches = true;
                for (std::size_t i = 0; i < arg_types.size(); ++i) {
                    if (!assignable(arg_types[i], overload.params[i])) {
                        matches = false;
                        break;
                    }
                }
                if (!matches) continue;

                expr.cname = overload.cname;
                expr.is_builtin = true;
                return record(expr, overload.ret);
            }

            std::string variants;
            for (const Overload& overload : *symbol->overloads) {
                if (!variants.empty()) variants += ", ";
                variants += signature(expr.name, overload.params, overload.ret);
            }

            std::string actual;
            for (std::size_t i = 0; i < arg_types.size(); ++i) {
                if (i > 0) actual += ", ";
                actual += type_name(arg_types[i]);
            }

            fail("нет подходящего варианта «" + expr.name + "» для (" + actual + ")",
                 expr.line, expr.col, static_cast<int>(expr.name.size()),
                 "доступно: " + variants);
        }

        if (arg_types.size() != symbol->params.size()) {
            fail("«" + expr.name + "» ждёт " + std::to_string(symbol->params.size()) +
                     " арг., а получила " + std::to_string(arg_types.size()),
                 expr.line, expr.col, static_cast<int>(expr.name.size()),
                 signature(symbol->name, symbol->params, symbol->ret));
        }

        for (std::size_t i = 0; i < arg_types.size(); ++i) {
            if (!assignable(arg_types[i], symbol->params[i])) {
                fail("аргумент " + std::to_string(i + 1) + " для «" + expr.name +
                         "» должен быть " + type_name(symbol->params[i]) + ", а он " +
                         type_name(arg_types[i]),
                     expr.args[i]->line, expr.args[i]->col, 1,
                     signature(symbol->name, symbol->params, symbol->ret));
            }
        }

        expr.cname = symbol->cname;
        return record(expr, symbol->ret);
    }

    Program& program_;
    std::vector<std::unique_ptr<Scope>> scopes_;
    Scope* scope_ = nullptr;
    int counter_ = 0;
    std::optional<TK> current_return_;
    int loop_depth_ = 0;
};

}  // namespace

void analyze(Program& program) { Checker(program).run(); }
