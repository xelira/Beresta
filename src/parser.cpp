#include "parser.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>

#include "diagnostic.hpp"

namespace {

int precedence_of(const std::string& op) {
    static const std::unordered_map<std::string, int> table = {
        {"или", 1},  {"и", 2},   {"==", 3}, {"!=", 3}, {"<", 4},  {"<=", 4},
        {">", 4},    {">=", 4},  {"+", 5},  {"-", 5},  {"*", 6},  {"/", 6},
        {"%", 6},
    };
    const auto it = table.find(op);
    return it == table.end() ? 0 : it->second;
}

struct TypeSpec {
    std::string name;
    TypeKind kind = TypeKind::Void;
    bool is_array = false;
    int count = 0;
    bool is_struct = false;
    bool is_pointer = false;
    TypeKind pointee = TypeKind::Void;
    std::string pointee_name;
    bool pointee_struct = false;
    bool is_slice = false;
    TypeKind slice_element = TypeKind::Void;
    std::string slice_element_name;
    bool is_map = false;
};

[[noreturn]] void unexpected(const Token& token, const std::string& what) {
    fail("ожидалось " + what + ", а встретилось " + token.describe(), token.line, token.col,
         token.length);
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Program run() {
        Program program;

        while (!check(TokKind::Eof)) {
            const Token& token = peek();
            if (token.kind == TokKind::Keyword && token.value == "береста") {
                if (program.entry) {
                    fail("блок «береста» может быть только один", token.line, token.col,
                         token.length);
                }
                advance();
                program.entry = parse_block();
            } else if (token.kind == TokKind::Keyword && token.value == "устой") {
                program.decls.push_back(parse_const());
            } else if (token.kind == TokKind::Keyword && token.value == "дело") {
                program.decls.push_back(parse_func());
            } else if (token.kind == TokKind::Keyword && token.value == "уклад") {
                program.decls.push_back(parse_struct());
            } else {
                fail("ожидалось объявление, а встретилось " + token.describe(), token.line,
                     token.col, token.length,
                     "на верхнем уровне бывают «устой», «дело» и блок «береста»");
            }
        }

        if (!program.entry) {
            fail("в программе нет блока «береста»", 0, 0, 1,
                 "точка входа выглядит так: береста { ... }");
        }

        return program;
    }

private:
    // --- навигация ---------------------------------------------------

    const Token& peek(int offset = 0) const {
        std::size_t index = pos_ + static_cast<std::size_t>(offset);
        if (index >= tokens_.size()) index = tokens_.size() - 1;
        return tokens_[index];
    }

    const Token& advance() {
        const Token& token = peek();
        if (token.kind != TokKind::Eof) ++pos_;
        return token;
    }

    bool check(TokKind kind, const std::string& value = "") const {
        const Token& token = peek();
        return token.kind == kind && (value.empty() || token.value == value);
    }

    bool check_kw(const std::string& word) const { return check(TokKind::Keyword, word); }
    bool check_op(const std::string& op) const { return check(TokKind::Op, op); }

    bool match_kw(const std::string& word) {
        if (!check_kw(word)) return false;
        advance();
        return true;
    }

    bool match_op(const std::string& op) {
        if (!check_op(op)) return false;
        advance();
        return true;
    }

    const Token& expect_op(const std::string& op) {
        if (!check_op(op)) unexpected(peek(), "«" + op + "»");
        return advance();
    }

    const Token& expect_kw(const std::string& word) {
        if (!check_kw(word)) unexpected(peek(), "слово «" + word + "»");
        return advance();
    }

    const Token& expect_ident(const std::string& what) {
        if (!check(TokKind::Ident)) unexpected(peek(), what);
        return advance();
    }

    const Token& expect_type() {
        const Token& token = peek();
        if (token.kind != TokKind::Keyword || !is_type_word(token.value)) {
            fail("ожидался тип, а встретилось " + token.describe(), token.line, token.col,
                 token.length, "доступные типы: число, дробь, строка, флаг, ничто");
        }
        return advance();
    }

    // --- объявления --------------------------------------------------

    StmtPtr parse_const() {
        const Token& keyword = expect_kw("устой");
        const Token& name = expect_ident("имя константы");
        expect_op(":");
        const Token& type_token = expect_type();
        expect_op("=");

        auto decl = std::make_unique<ConstDecl>();
        decl->line = keyword.line;
        decl->col = keyword.col;
        decl->name = name.value;
        decl->type_name = type_token.value;
        decl->declared = *type_from_word(type_token.value);
        decl->value = parse_expr();
        expect_op(";");
        return decl;
    }

    StmtPtr parse_func() {
        const Token& keyword = expect_kw("дело");
        const Token& name = expect_ident("имя функции");
        expect_op("(");

        auto decl = std::make_unique<FuncDecl>();
        decl->line = keyword.line;
        decl->col = keyword.col;
        decl->name = name.value;

        if (!check_op(")")) {
            while (true) {
                const Token& param_name = expect_ident("имя параметра");
                expect_op(":");
                const TypeSpec spec = parse_type_spec();

                Param param;
                param.line = param_name.line;
                param.col = param_name.col;
                param.name = param_name.value;
                param.type_name = spec.name;
                param.declared = spec.kind;
                param.is_array = spec.is_array;
                param.array_count = spec.count;
                param.is_struct = spec.is_struct;
                param.is_pointer = spec.is_pointer;
                param.pointee = spec.pointee;
                param.pointee_name = spec.pointee_name;
                param.pointee_struct = spec.pointee_struct;
                param.is_slice = spec.is_slice;
                param.slice_element = spec.slice_element;
                param.is_map = spec.is_map;
                decl->params.push_back(std::move(param));

                if (!match_op(",")) break;
            }
        }
        expect_op(")");
        expect_op("->");

        const TypeSpec return_spec = parse_type_spec();
        if (return_spec.is_array || return_spec.is_slice || return_spec.is_pointer ||
            return_spec.is_struct) {
            fail("возврат такого типа пока не поддерживается", decl->line, decl->col,
                 static_cast<int>(decl->name.size()),
                 "функция может вернуть простой тип или словарь");
        }
        decl->return_type_name = return_spec.name;
        decl->ret = return_spec.kind;
        decl->body = parse_block();
        return decl;
    }

    // --- операторы ---------------------------------------------------

    std::unique_ptr<Block> parse_block() {
        const Token& open = expect_op("{");

        auto block = std::make_unique<Block>();
        block->line = open.line;
        block->col = open.col;

        while (!check_op("}")) {
            if (check(TokKind::Eof)) {
                fail("незакрытый блок", open.line, open.col, 1, "поставьте «}»");
            }
            block->body.push_back(parse_stmt());
        }
        expect_op("}");
        return block;
    }

    StmtPtr parse_stmt() {
        const Token& token = peek();

        if (token.kind == TokKind::Keyword) {
            const std::string& word = token.value;
            if (word == "слово") return parse_var();
            if (word == "молви") return parse_print();
            if (word == "ежели") return parse_if();
            if (word == "доколе") return parse_while();
            if (word == "повтор") return parse_for();
            if (word == "для") return parse_foreach();
            if (word == "вернуть") return parse_return();
            if (word == "прервать") {
                advance();
                expect_op(";");
                auto node = std::make_unique<Break>();
                node->line = token.line;
                node->col = token.col;
                return node;
            }
            if (word == "продолжить") {
                advance();
                expect_op(";");
                auto node = std::make_unique<Continue>();
                node->line = token.line;
                node->col = token.col;
                return node;
            }
        }

        if (check_op("{")) return parse_block();

        // составное присваивание: имя += выражение ;
        if (check(TokKind::Ident) && peek(1).kind == TokKind::Op &&
            (peek(1).value == "+" || peek(1).value == "-" || peek(1).value == "*" ||
             peek(1).value == "/" || peek(1).value == "%") &&
            peek(2).kind == TokKind::Op && peek(2).value == "=") {
            const Token name = advance();
            const Token op = advance();
            advance();  // «=»

            auto left = std::make_unique<Ident>(name.value);
            left->line = name.line;
            left->col = name.col;
            auto right = parse_expr();
            auto binary = std::make_unique<Binary>(op.value, std::move(left), std::move(right));
            binary->line = op.line;
            binary->col = op.col;

            auto node = std::make_unique<Assign>();
            node->line = name.line;
            node->col = name.col;
            node->name = name.value;
            node->value = std::move(binary);
            expect_op(";");
            return node;
        }

        // присваивание: имя = выражение ;
        if (check(TokKind::Ident) && peek(1).kind == TokKind::Op && peek(1).value == "=") {
            const Token& name = advance();
            advance();  // «=»
            auto node = std::make_unique<Assign>();
            node->line = name.line;
            node->col = name.col;
            node->name = name.value;
            node->value = parse_expr();
            expect_op(";");
            return node;
        }

        auto expr = parse_expr();

        // присваивание элементу массива или полю уклада: а[i] = ..., т.х = ...
        if (check_op("=")) {
            advance();

            if (auto* index = dynamic_cast<Index*>(expr.get())) {
                auto node = std::make_unique<IndexAssign>();
                node->line = index->line;
                node->col = index->col;
                node->name = index->name;
                node->index = std::move(index->index);
                node->value = parse_expr();
                expect_op(";");
                return node;
            }

            if (dynamic_cast<Member*>(expr.get()) != nullptr) {
                auto node = std::make_unique<MemberAssign>();
                node->line = expr->line;
                node->col = expr->col;
                node->target = std::move(expr);
                node->value = parse_expr();
                expect_op(";");
                return node;
            }

            if (auto* deref = dynamic_cast<Unary*>(expr.get());
                deref != nullptr && deref->op == "по") {
                auto node = std::make_unique<DerefAssign>();
                node->line = deref->line;
                node->col = deref->col;
                node->pointer = std::move(deref->operand);
                node->value = parse_expr();
                expect_op(";");
                return node;
            }

            fail("слева от «=» должна быть переменная, элемент массива, поле уклада или «по»",
                 expr->line, expr->col);
        }

        auto node = std::make_unique<ExprStmt>();
        node->line = expr->line;
        node->col = expr->col;
        node->value = std::move(expr);
        expect_op(";");
        return node;
    }

    StmtPtr parse_struct() {
        const Token& keyword = expect_kw("уклад");
        const Token& name = expect_ident("имя уклада");
        expect_op("{");

        auto decl = std::make_unique<StructDecl>();
        decl->line = keyword.line;
        decl->col = keyword.col;
        decl->name = name.value;

        while (!check_op("}")) {
            if (check(TokKind::Eof)) {
                fail("незакрытый уклад", keyword.line, keyword.col, 1, "поставьте «}»");
            }
            const Token& field = expect_ident("имя поля");
            expect_op(":");
            const TypeSpec spec = parse_type_spec();
            if (spec.is_array || spec.is_pointer) {
                fail("поля-массивы и поля-указатели пока не поддерживаются", field.line,
                     field.col, static_cast<int>(field.value.size()),
                     "поле может быть простым типом или другим укладом");
            }
            expect_op(";");

            Field info;
            info.name = field.value;
            info.type_name = spec.name;
            info.type = spec.is_struct ? TypeKind::Struct : spec.kind;
            decl->fields.push_back(std::move(info));
        }
        expect_op("}");

        return decl;
    }

    TypeSpec parse_type_spec() {
        TypeSpec spec;

        if (check_op("[")) {
            advance();
            const Token& count_token = peek();
            if (count_token.kind != TokKind::Int) {
                unexpected(count_token, "размер массива (целое число)");
            }
            advance();
            const long long size = std::stoll(count_token.value);
            if (size <= 0 || size > 100000) {
                fail("размер массива должен быть от 1 до 100000", count_token.line,
                     count_token.col, count_token.length);
            }
            spec.is_array = true;
            spec.count = static_cast<int>(size);
            expect_op("]");
        }

        if (check_kw("словарь")) {
            advance();
            spec.is_map = true;
            spec.kind = TypeKind::Map;
            spec.name = "словарь";
            return spec;
        }

        if (check_kw("срез")) {
            advance();
            spec.is_slice = true;
            spec.kind = TypeKind::Slice;

            const Token& element = peek();
            if (element.kind == TokKind::Keyword && is_type_word(element.value)) {
                spec.slice_element = *type_from_word(element.value);
                spec.slice_element_name = element.value;
                advance();
            } else {
                unexpected(element, "тип элемента среза");
            }

            spec.name = "срез " + spec.slice_element_name;
            return spec;
        }

        if (check_kw("указатель")) {
            advance();
            spec.is_pointer = true;
            spec.kind = TypeKind::Pointer;

            const Token& pointee = peek();
            if (pointee.kind == TokKind::Keyword && is_type_word(pointee.value)) {
                spec.pointee = *type_from_word(pointee.value);
                spec.pointee_name = pointee.value;
                advance();
            } else if (pointee.kind == TokKind::Ident) {
                spec.pointee_struct = true;
                spec.pointee_name = pointee.value;
                advance();
            } else {
                unexpected(pointee, "тип, на который указывает указатель");
            }

            spec.name = "указатель " + spec.pointee_name;
            return spec;
        }

        const Token& token = peek();
        if (token.kind == TokKind::Keyword && is_type_word(token.value)) {
            spec.kind = *type_from_word(token.value);
            advance();
        } else if (token.kind == TokKind::Ident) {
            spec.is_struct = true;
            advance();
        } else {
            unexpected(token, "тип");
        }

        spec.name = token.value;
        return spec;
    }

    StmtPtr parse_var() {
        const Token& keyword = expect_kw("слово");
        const Token& name = expect_ident("имя переменной");
        expect_op(":");

        const TypeSpec spec = parse_type_spec();

        auto decl = std::make_unique<VarDecl>();
        decl->line = keyword.line;
        decl->col = keyword.col;
        decl->name = name.value;
        decl->type_name = spec.name;
        decl->declared = spec.kind;
        decl->is_array = spec.is_array;
        decl->array_count = spec.count;
        decl->is_struct = spec.is_struct;
        decl->is_pointer = spec.is_pointer;
        decl->pointee = spec.pointee;
        decl->pointee_name = spec.pointee_name;
        decl->pointee_struct = spec.pointee_struct;
        decl->is_slice = spec.is_slice;
        decl->slice_element = spec.slice_element;
        decl->is_map = spec.is_map;

        if (match_op("=")) {
            if (spec.is_array) {
                fail("массив пока нельзя инициализировать списком", keyword.line, keyword.col,
                     static_cast<int>(name.value.size()),
                     "объявите массив и заполните элементы по одному");
            }
            decl->value = parse_expr();
        }
        expect_op(";");
        return decl;
    }

    StmtPtr parse_print() {
        const Token& keyword = expect_kw("молви");
        auto node = std::make_unique<Print>();
        node->line = keyword.line;
        node->col = keyword.col;
        node->value = parse_expr();
        expect_op(";");
        return node;
    }

    StmtPtr parse_if() {
        const Token& keyword = expect_kw("ежели");

        auto node = std::make_unique<If>();
        node->line = keyword.line;
        node->col = keyword.col;
        node->cond = parse_expr();
        node->then_branch = parse_block();

        if (check_kw("инако")) {
            advance();
            if (check_kw("ежели")) {
                node->otherwise = parse_if();
            } else {
                node->otherwise = parse_block();
            }
        }

        return node;
    }

    StmtPtr parse_while() {
        const Token& keyword = expect_kw("доколе");
        auto node = std::make_unique<While>();
        node->line = keyword.line;
        node->col = keyword.col;
        node->cond = parse_expr();
        node->body = parse_block();
        return node;
    }

    StmtPtr parse_for() {
        const Token& keyword = expect_kw("повтор");
        const Token& var = expect_ident("имя счётчика");
        expect_kw("от");

        auto node = std::make_unique<For>();
        node->line = keyword.line;
        node->col = keyword.col;
        node->var = var.value;
        node->start = parse_expr();
        expect_kw("до");
        node->stop = parse_expr();

        if (match_kw("шаг")) node->step = parse_expr();

        node->body = parse_block();
        return node;
    }

    StmtPtr parse_foreach() {
        const Token& keyword = expect_kw("для");
        expect_kw("каждого");
        const Token& var = expect_ident("имя переменной цикла");
        expect_kw("из");
        const Token& source = expect_ident("имя массива");

        auto node = std::make_unique<ForEach>();
        node->line = keyword.line;
        node->col = keyword.col;
        node->var = var.value;
        node->array = source.value;
        node->body = parse_block();
        return node;
    }

    StmtPtr parse_return() {
        const Token& keyword = expect_kw("вернуть");
        auto node = std::make_unique<Return>();
        node->line = keyword.line;
        node->col = keyword.col;
        if (!check_op(";")) node->value = parse_expr();
        expect_op(";");
        return node;
    }

    // --- выражения ---------------------------------------------------

    ExprPtr parse_expr(int min_precedence = 1) {
        ExprPtr left = parse_unary();

        while (true) {
            const Token& token = peek();
            if (token.kind != TokKind::Op && token.kind != TokKind::Keyword) break;

            const int precedence = precedence_of(token.value);
            if (precedence == 0 || precedence < min_precedence) break;

            const Token op = token;
            advance();
            ExprPtr right = parse_expr(precedence + 1);

            auto binary = std::make_unique<Binary>(op.value, std::move(left), std::move(right));
            binary->line = op.line;
            binary->col = op.col;
            left = std::move(binary);
        }

        return left;
    }

    ExprPtr parse_unary() {
        const Token& token = peek();
        const bool is_neg = token.kind == TokKind::Op && token.value == "-";
        const bool is_not = (token.kind == TokKind::Op && token.value == "!") ||
                            (token.kind == TokKind::Keyword && token.value == "не");
        const bool is_address = token.kind == TokKind::Keyword && token.value == "адрес";
        const bool is_deref = token.kind == TokKind::Keyword && token.value == "по";

        if (is_neg || is_not || is_address || is_deref) {
            const Token op = token;
            advance();
            auto node = std::make_unique<Unary>(op.value, parse_unary());
            node->line = op.line;
            node->col = op.col;
            return node;
        }

        return parse_primary();
    }

    ExprPtr parse_primary() {
        const Token token = peek();

        if (token.kind == TokKind::Int) {
            advance();
            try {
                return make_lit<IntLit>(token, std::stoll(token.value));
            } catch (const std::exception&) {
                fail("слишком большое число", token.line, token.col, token.length);
            }
        }

        if (token.kind == TokKind::Float) {
            advance();
            try {
                return make_lit<FloatLit>(token, std::stod(token.value));
            } catch (const std::exception&) {
                fail("некорректная дробь", token.line, token.col, token.length);
            }
        }

        if (token.kind == TokKind::Str) {
            advance();
            return make_lit<StringLit>(token, token.value);
        }

        if (token.kind == TokKind::Keyword && (token.value == "правда" || token.value == "кривда")) {
            advance();
            return make_lit<BoolLit>(token, token.value == "правда");
        }

        if (token.kind == TokKind::Ident) {
            advance();
            if (check_op("(")) {
                advance();
                auto call = std::make_unique<Call>(token.value);
                call->line = token.line;
                call->col = token.col;

                if (!check_op(")")) {
                    while (true) {
                        call->args.push_back(parse_expr());
                        if (!match_op(",")) break;
                    }
                }
                expect_op(")");
                return call;
            }
            ExprPtr base;
            if (check_op("[")) {
                advance();
                auto index = std::make_unique<Index>(token.value);
                index->line = token.line;
                index->col = token.col;
                index->index = parse_expr();
                expect_op("]");
                base = std::move(index);
            } else {
                auto ident = std::make_unique<Ident>(token.value);
                ident->line = token.line;
                ident->col = token.col;
                base = std::move(ident);
            }

            while (check_op(".")) {
                advance();
                const Token& field = expect_ident("имя поля");
                auto member = std::make_unique<Member>(std::move(base), field.value);
                member->line = field.line;
                member->col = field.col;
                base = std::move(member);
            }
            return base;
        }

        if (check_op("(")) {
            advance();
            ExprPtr inner = parse_expr();
            expect_op(")");
            return inner;
        }

        if (check_op("{")) {
            fail("блок не может быть выражением", token.line, token.col, token.length,
                 "блоки бывают только операторами");
        }

        unexpected(token, "выражение");
    }

    template <typename NodeT, typename ValueT>
    ExprPtr make_lit(const Token& token, ValueT&& value) {
        auto node = std::make_unique<NodeT>(std::forward<ValueT>(value));
        node->line = token.line;
        node->col = token.col;
        return node;
    }

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

}  // namespace

Program parse(std::vector<Token> tokens) { return Parser(std::move(tokens)).run(); }
