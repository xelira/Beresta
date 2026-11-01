#pragma once

#include <string>
#include <vector>

#include "types.hpp"

struct Overload {
    std::vector<TypeKind> params;
    TypeKind ret = TypeKind::Void;
    //: имя функции в рантайме; пусто — аргумент подставляется как есть
    std::string cname;
};

//: Запись имени в таблице символов. Живёт в Program::symbols.
struct Symbol {
    enum class Kind { Var, Const, Param, Func, Builtin, Loop };

    std::string name;
    TypeKind type = TypeKind::Void;
    Kind kind = Kind::Var;
    std::string cname;
    int line = 0;
    int col = 0;
    int slot = -1;
    bool global = false;
    //: для массивов — тип элемента и длина
    TypeKind element = TypeKind::Void;
    int count = 0;
    std::vector<TypeKind> params;
    TypeKind ret = TypeKind::Void;
    const std::vector<Overload>* overloads = nullptr;
};
