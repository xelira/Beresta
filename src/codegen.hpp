#pragma once

#include <string>

#include "ast.hpp"

//: Программа -> текст на ассемблере GNU as (AT&T, x86-64 System V).
std::string generate(Program& program);
