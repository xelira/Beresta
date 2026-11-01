#pragma once

#include <vector>

#include "ast.hpp"
#include "token.hpp"

//: Рекурсивный спуск: токены -> AST. Полная грамматика в docs/GRAMMAR.md.
Program parse(std::vector<Token> tokens);
