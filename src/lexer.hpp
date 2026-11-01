#pragma once

#include <string>
#include <vector>

#include "token.hpp"

//: Разбирает исходник на токены. Пробелы и переводы строк не значимы.
std::vector<Token> lex(const std::string& source);
