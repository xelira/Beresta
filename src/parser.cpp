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
                const Token& param_type = expect_type();

                Param param;
                param.line = param_name.line;
                param.col = param_name.col;
                param.name = param_name.value;
                param.type_name = param_type.value;
                param.declared = *type_from_word(param_type.value);
                decl->params.push_back(std::move(param));

                if (!match_op(",")) break;
            }
        }
        expect_op(")");
        expect_op("->");

        const Token& return_type = expect_type();
        decl->return_type_name = return_type.value;
        decl->ret = *type_from_word(return_type.value);
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

            if (auto* member = dynamic_cast<Member*>(expr.get())) {
                auto node = std::make_unique<MemberAssign>();
                node->line = member->line;
                node->col = member->col;
                node->base = member->base;
                node->field = member->field;
                node->value = parse_expr();
                expect_op(";");
                return node;
            }

            fail("слева от «=» должна быть переменная, элемент массива или поле уклада",
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
            const Token& field_type = expect_type();
            expect_op(";");
            decl->fields.push_back(Field{field.value, *type_from_word(field_type.value)});
        }
        expect_op("}");

        return decl;
    }

    StmtPtr parse_var() {
        const Token& keyword = expect_kw("слово");
        const Token& name = expect_ident("имя переменной");
        expect_op(":");

        bool is_array = false;
        int array_count = 0;
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
            array_count = static_cast<int>(size);
            expect_op("]");
            is_array = true;
        }

        const Token& type_token = peek();
        bool is_struct = false;
        TypeKind declared = TypeKind::Void;

        if (type_token.kind == TokKind::Keyword && is_type_word(type_token.value)) {
            declared = *type_from_word(type_token.value);
            advance();
        } else if (type_token.kind == TokKind::Ident) {
            is_struct = true;
            advance();
        } else {
            unexpected(type_token, "тип");
        }

        auto decl = std::make_unique<VarDecl>();
        decl->line = keyword.line;
        decl->col = keyword.col;
        decl->name = name.value;
        decl->type_name = type_token.value;
        decl->declared = declared;
        decl->is_array = is_array;
        decl->array_count = array_count;
        decl->is_struct = is_struct;

        if (match_op("=")) {
            if (is_array) {
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

        if (is_neg || is_not) {
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
            if (check_op("[")) {
                advance();
                auto node = std::make_unique<Index>(token.value);
                node->line = token.line;
                node->col = token.col;
                node->index = parse_expr();
                expect_op("]");
                return node;
            }
            if (check_op(".")) {
                advance();
                const Token& field = expect_ident("имя поля");
                auto node = std::make_unique<Member>(token.value, field.value);
                node->line = token.line;
                node->col = token.col;
                node->field_index = -1;
                return node;
            }
            auto ident = std::make_unique<Ident>(token.value);
            ident->line = token.line;
            ident->col = token.col;
            return ident;
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
