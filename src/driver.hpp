#pragma once

#include <string>

#include "target.hpp"

struct BuildOptions {
    std::string output;    //: куда положить бинарь (пусто — рядом с исходником)
    std::string asm_path;  //: куда положить .s (пусто — рядом с исходником)
    bool emit_asm = false; //: только сгенерировать ассемблер, не собирать
    bool keep_asm = false; //: оставить .s после сборки
    Target target = default_target();
};

//: Полный цикл: исходник -> .s -> .o -> исполняемый файл.
std::string build(const std::string& input, const BuildOptions& options);

//: Идентификатор процесса для временных файлов.
long process_id();
