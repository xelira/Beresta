#pragma once

#include <exception>
#include <string>

struct Diagnostic {
    std::string message;
    std::string hint;
    int line = 0;
    int col = 0;
    int length = 1;
    bool has_position = false;
    //: если ошибка пришла из модуля, здесь лежит его файл и исходник
    std::string filename;
    std::string source;
};

class BerestaError : public std::exception {
public:
    explicit BerestaError(Diagnostic diagnostic) : diag_(std::move(diagnostic)) {}

    const char* what() const noexcept override { return diag_.message.c_str(); }
    const Diagnostic& diagnostic() const { return diag_; }

private:
    Diagnostic diag_;
};

//: Бросить ошибку компиляции. Больше ничего бросать не принято.
[[noreturn]] void fail(std::string message, int line = 0, int col = 0, int length = 1,
                       std::string hint = "");

//: Красивый вывод диагностики с фрагментом исходника и кареткой.
std::string render_diagnostic(const std::string& filename, const std::string& source,
                              const Diagnostic& diagnostic);
