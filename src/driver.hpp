#pragma once

#include <string>

struct BuildOptions {
    std::string output;    //: куда положить бинарь (пусто — рядом с исходником)
    std::string asm_path;  //: куда положить .s (пусто — рядом с исходником)
    bool emit_asm = false; //: только сгенерировать ассемблер, не собирать
    bool keep_asm = false; //: оставить .s после сборки
};

//: Полный цикл: исходник -> .s -> .o -> ELF. Возвращает путь к бинарю.
std::string build(const std::string& input, const BuildOptions& options);
