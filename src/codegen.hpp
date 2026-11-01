#pragma once

#include <string>

#include "ast.hpp"
#include "target.hpp"

//: Программа -> текст на ассемблере.
//: Linux: GNU as, System V AMD64. Windows: GNU as, Microsoft x64.
std::string generate(Program& program, Target target);

//: Исходник рантайма на C для Windows-цели.
const char* windows_runtime_c();
