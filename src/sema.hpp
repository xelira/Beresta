#pragma once

#include <vector>

#include "ast.hpp"
#include "symbol.hpp"

//: Проверяет семантику и связывает имена с символами. Ничего не возвращает:
//: дерево (и Program::symbols) дополняется на месте.
void analyze(Program& program);
