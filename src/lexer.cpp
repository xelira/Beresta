#include "lexer.hpp"

#include <initializer_list>
#include <string>
#include <unordered_set>

#include "diagnostic.hpp"

namespace {

const std::unordered_set<std::string>& keyword_set() {
    static const std::unordered_set<std::string> words = {
        // объявления
        "слово", "устой", "дело", "береста", "уклад",
        // управление
        "ежели", "инако", "доколе", "повтор", "от", "до", "шаг",
        "для", "каждого", "из",
        "вернуть", "прервать", "продолжить",
        // указатели
        "указатель", "адрес", "по",
        // ввод-вывод
        "молви",
        // литералы и логика
        "правда", "кривда", "и", "или", "не",
        // типы
        "число", "дробь", "строка", "флаг", "ничто",
    };
    return words;
}

bool is_ident_start(unsigned char ch) {
    // Только кириллица (UTF-8 >= 0x80) и подчёркивание. Латынь запрещена.
    return ch == '_' || ch >= 0x80;
}

bool is_latin_letter(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool is_ident_continue(unsigned char ch) {
    return is_ident_start(ch) || (ch >= '0' && ch <= '9');
}

bool is_continuation(unsigned char ch) { return (ch & 0xC0) == 0x80; }

class Lexer {
public:
    explicit Lexer(const std::string& source) : source_(source) {}

    std::vector<Token> run() {
        while (pos_ < source_.size()) {
            const unsigned char ch = static_cast<unsigned char>(source_[pos_]);

            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                advance();
                continue;
            }

            if (ch == '/' && peek(1) == '/') {
                while (pos_ < source_.size() && peek(0) != '\n') advance();
                continue;
            }

            if (ch == '/' && peek(1) == '*') {
                skip_block_comment();
                continue;
            }

            if (ch >= '0' && ch <= '9') {
                number();
                continue;
            }

            if (ch == '"') {
                string_literal();
                continue;
            }

            if (is_latin_letter(ch)) {
                fail("пиши на русском", line_, col_, 1,
                     "имена и слова бересты — только кириллицей");
            }

            if (is_ident_start(ch)) {
                word();
                continue;
            }

            if (operators()) continue;

            fail(std::string("непонятный знак «") + static_cast<char>(ch) + "»", line_, col_, 1,
                 "в бересте нет такого символа");
        }

        tokens_.push_back(Token{TokKind::Eof, "", line_, col_, 0});
        return tokens_;
    }

private:
    char peek(std::size_t offset) const {
        const std::size_t index = pos_ + offset;
        return index < source_.size() ? source_[index] : '\0';
    }

    void advance(std::size_t count = 1) {
        for (std::size_t i = 0; i < count && pos_ < source_.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(source_[pos_]);
            if (ch == '\n') {
                ++line_;
                col_ = 1;
            } else if (!is_continuation(ch)) {
                ++col_;
            }
            ++pos_;
        }
    }

    void add(TokKind kind, std::string value, int line, int col, int length) {
        tokens_.push_back(Token{kind, std::move(value), line, col, length});
    }

    void skip_block_comment() {
        const int line = line_;
        const int col = col_;
        advance(2);
        while (pos_ < source_.size()) {
            if (peek(0) == '*' && peek(1) == '/') {
                advance(2);
                return;
            }
            advance();
        }
        fail("незакрытый комментарий /*", line, col, 2, "закройте его «*/»");
    }

    void number() {
        const int line = line_;
        const int col = col_;
        const std::size_t start = pos_;

        while ((peek(0) >= '0' && peek(0) <= '9') || peek(0) == '_') advance();

        bool is_float = false;
        if (peek(0) == '.' && peek(1) >= '0' && peek(1) <= '9') {
            is_float = true;
            advance();
            while ((peek(0) >= '0' && peek(0) <= '9') || peek(0) == '_') advance();
        }

        std::string raw;
        for (std::size_t i = start; i < pos_; ++i) {
            if (source_[i] != '_') raw.push_back(source_[i]);
        }

        add(is_float ? TokKind::Float : TokKind::Int, std::move(raw), line, col,
            static_cast<int>(pos_ - start));
    }

    void string_literal() {
        const int line = line_;
        const int col = col_;
        const std::size_t start = pos_;
        advance();  // открывающая кавычка

        std::string value;
        while (true) {
            if (pos_ >= source_.size()) {
                fail("незакрытая строка", line, col, 1, "закройте строку кавычкой");
            }

            const char ch = peek(0);
            if (ch == '"') {
                advance();
                break;
            }

            if (ch == '\n') {
                fail("перевод строки внутри строки", line_, col_, 1);
            }

            if (ch == '\\') {
                const char esc = peek(1);
                switch (esc) {
                    case 'n': value.push_back('\n'); break;
                    case 't': value.push_back('\t'); break;
                    case 'r': value.push_back('\r'); break;
                    case '0': value.push_back('\0'); break;
                    case '\\': value.push_back('\\'); break;
                    case '"': value.push_back('"'); break;
                    default:
                        fail(std::string("неизвестная escape-последовательность «\\") + esc + "»",
                             line_, col_, 2);
                }
                advance(2);
                continue;
            }

            value.push_back(ch);
            advance();
        }

        add(TokKind::Str, std::move(value), line, col, static_cast<int>(pos_ - start));
    }

    void word() {
        const int line = line_;
        const int col = col_;
        const std::size_t start = pos_;

        while (pos_ < source_.size() &&
               is_ident_continue(static_cast<unsigned char>(source_[pos_]))) {
            advance();
        }

        std::string text = source_.substr(start, pos_ - start);
        const TokKind kind = is_keyword(text) ? TokKind::Keyword : TokKind::Ident;
        add(kind, std::move(text), line, col, static_cast<int>(pos_ - start));
    }

    bool operators() {
        static const char* two_char[] = {"->", "==", "!=", "<=", ">="};
        for (const char* op : two_char) {
            if (peek(0) == op[0] && peek(1) == op[1]) {
                add(TokKind::Op, std::string(op), line_, col_, 2);
                advance(2);
                return true;
            }
        }

        const char ch = peek(0);
        if (std::string("+-*/%=<>(){}[],:;!.").find(ch) != std::string::npos) {
            add(TokKind::Op, std::string(1, ch), line_, col_, 1);
            advance();
            return true;
        }

        return false;
    }

    const std::string& source_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    std::vector<Token> tokens_;
};

}  // namespace

bool is_keyword(const std::string& word) { return keyword_set().count(word) > 0; }

bool is_type_word(const std::string& word) {
    return word == "число" || word == "дробь" || word == "строка" || word == "флаг" ||
           word == "ничто";
}

std::vector<Token> lex(const std::string& source) { return Lexer(source).run(); }
