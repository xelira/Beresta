#include "dump.hpp"

#include <sstream>
#include <string>

namespace {

std::string indent(int depth) { return std::string(static_cast<size_t>(depth) * 2, ' '); }

void dump_expr(const Expr& expr, int depth, std::ostringstream& out);
void dump_stmt(const Stmt& stmt, int depth, std::ostringstream& out);

void dump_expr(const Expr& expr, int depth, std::ostringstream& out) {
    const std::string pad = indent(depth);

    if (const auto* node = dynamic_cast<const IntLit*>(&expr)) {
        out << pad << "Число " << node->value << "\n";
    } else if (const auto* node = dynamic_cast<const FloatLit*>(&expr)) {
        out << pad << "Дробь " << node->value << "\n";
    } else if (const auto* node = dynamic_cast<const StringLit*>(&expr)) {
        out << pad << "Строка \"" << node->value << "\"\n";
    } else if (const auto* node = dynamic_cast<const BoolLit*>(&expr)) {
        out << pad << "Флаг " << (node->value ? "правда" : "кривда") << "\n";
    } else if (const auto* node = dynamic_cast<const Ident*>(&expr)) {
        out << pad << "Имя " << node->name << "\n";
    } else if (const auto* node = dynamic_cast<const Unary*>(&expr)) {
        out << pad << "Унарное " << node->op << "\n";
        dump_expr(*node->operand, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const Binary*>(&expr)) {
        out << pad << "Бинарное " << node->op << "\n";
        dump_expr(*node->left, depth + 1, out);
        dump_expr(*node->right, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const Call*>(&expr)) {
        out << pad << "Вызов " << node->name << "\n";
        for (const auto& arg : node->args) dump_expr(*arg, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const Index*>(&expr)) {
        out << pad << "Элемент " << node->name << "\n";
        dump_expr(*node->index, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const Member*>(&expr)) {
        out << pad << "Поле " << node->base << "." << node->field << "\n";
    } else {
        out << pad << "?\n";
    }
}

void dump_stmt(const Stmt& stmt, int depth, std::ostringstream& out) {
    const std::string pad = indent(depth);

    if (const auto* node = dynamic_cast<const VarDecl*>(&stmt)) {
        out << pad << "Слово " << node->name << ": ";
        if (node->is_array) out << "[" << node->array_count << "]";
        out << node->type_name << "\n";
        if (node->value) dump_expr(*node->value, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const IndexAssign*>(&stmt)) {
        out << pad << "Присвоить элементу " << node->name << "\n";
        dump_expr(*node->index, depth + 1, out);
        dump_expr(*node->value, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const MemberAssign*>(&stmt)) {
        out << pad << "Присвоить полю " << node->base << "." << node->field << "\n";
        dump_expr(*node->value, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const StructDecl*>(&stmt)) {
        out << pad << "Уклад " << node->name << "\n";
        for (const auto& field : node->fields) {
            out << pad << "  Поле " << field.name << ": " << type_name(field.type) << "\n";
        }
    } else if (const auto* node = dynamic_cast<const ConstDecl*>(&stmt)) {
        out << pad << "Устой " << node->name << ": " << node->type_name << "\n";
        if (node->value) dump_expr(*node->value, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const Assign*>(&stmt)) {
        out << pad << "Присвоить " << node->name << "\n";
        dump_expr(*node->value, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const Print*>(&stmt)) {
        out << pad << "Молви\n";
        dump_expr(*node->value, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const ExprStmt*>(&stmt)) {
        out << pad << "Выражение\n";
        dump_expr(*node->value, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const Block*>(&stmt)) {
        out << pad << "Блок\n";
        for (const auto& inner : node->body) dump_stmt(*inner, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const If*>(&stmt)) {
        out << pad << "Ежели\n";
        dump_expr(*node->cond, depth + 1, out);
        dump_stmt(*node->then_branch, depth + 1, out);
        if (node->otherwise) dump_stmt(*node->otherwise, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const While*>(&stmt)) {
        out << pad << "Доколе\n";
        dump_expr(*node->cond, depth + 1, out);
        dump_stmt(*node->body, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const For*>(&stmt)) {
        out << pad << "Повтор " << node->var << "\n";
        dump_expr(*node->start, depth + 1, out);
        dump_expr(*node->stop, depth + 1, out);
        if (node->step) dump_expr(*node->step, depth + 1, out);
        dump_stmt(*node->body, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const Return*>(&stmt)) {
        out << pad << "Вернуть\n";
        if (node->value) dump_expr(*node->value, depth + 1, out);
    } else if (const auto* node = dynamic_cast<const ForEach*>(&stmt)) {
        out << pad << "Для каждого " << node->var << " в " << node->array << "\n";
        dump_stmt(*node->body, depth + 1, out);
    } else if (dynamic_cast<const Break*>(&stmt)) {
        out << pad << "Прервать\n";
    } else if (dynamic_cast<const Continue*>(&stmt)) {
        out << pad << "Продолжить\n";
    } else if (const auto* node = dynamic_cast<const FuncDecl*>(&stmt)) {
        out << pad << "Дело " << node->name << " -> " << node->return_type_name << "\n";
        for (const auto& param : node->params) {
            out << pad << "  Параметр " << param.name << ": " << param.type_name << "\n";
        }
        dump_stmt(*node->body, depth + 1, out);
    } else {
        out << pad << "?\n";
    }
}

}  // namespace

std::string dump_ast(const Program& program) {
    std::ostringstream out;
    out << "Программа\n";

    for (const auto& decl : program.decls) dump_stmt(*decl, 1, out);

    if (program.entry) {
        out << "  Береста\n";
        const auto* entry = dynamic_cast<const Block*>(program.entry.get());
        if (entry != nullptr) {
            for (const auto& stmt : entry->body) dump_stmt(*stmt, 2, out);
        }
    }

    return out.str();
}
