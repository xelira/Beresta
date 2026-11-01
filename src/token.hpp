#pragma once

#include <string>

enum class TokKind { Ident, Keyword, Int, Float, Str, Op, Eof };

struct Token {
    TokKind kind = TokKind::Eof;
    std::string value;
    int line = 1;
    int col = 1;
    int length = 1;

    std::string describe() const {
        if (kind == TokKind::Eof) return "конец файла";
        return "«" + value + "»";
    }
};

bool is_keyword(const std::string& word);
bool is_type_word(const std::string& word);
