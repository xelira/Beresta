#pragma once

#include <string>

#include "ast.hpp"

//: Человекочитаемый дамп дерева для отладки компилятора.
std::string dump_ast(const Program& program);
