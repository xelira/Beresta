#pragma once

#include <string>
#include <vector>

#include "ast.hpp"
#include "diagnostic.hpp"

//: Кусок общего исходника, пришедший из одного файла.
struct Segment {
    std::string file;
    std::string source;
    int start_line = 1;
    int lines = 0;
};

//: Склеенный исходник и карта к файлам.
struct LoadedSource {
    std::string combined;
    std::vector<Segment> segments;
};

//: Полная компиляция: загрузка модулей, разбор, семантика.
struct Compilation {
    LoadedSource loaded;
    Program program;
};

//: Читает файл и рекурсивно подключает всё, что он привлекает.
LoadedSource load_source(const std::string& entry);

//: Переводит строку общего исходника в файл и локальную строку.
Diagnostic remap(const LoadedSource& loaded, const Diagnostic& diagnostic);

//: load_source + lex + parse + analyze. Ошибки уже привязаны к файлам.
Compilation compile_program(const std::string& entry);
