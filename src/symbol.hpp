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

struct StructDef;

//: Поле уклада. Может быть простого типа или другим укладом.
struct Field {
    std::string name;
    std::string type_name;  // как написано в исходнике
    TypeKind type = TypeKind::Void;
    const StructDef* layout = nullptr;  // если поле — уклад
    int offset = 0;                     // смещение в слотах
};

//: Описание уклада: имя, поля по порядку и полный размер в слотах.
struct StructDef {
    std::string name;
    std::vector<Field> fields;
    int size_slots = 0;
};

//: Описание параметра функции. Массивы и уклады передаются по ссылке.
struct ParamInfo {
    TypeKind type = TypeKind::Void;
    TypeKind element = TypeKind::Void;  // для массива
    int count = 0;                      // для массива
    TypeKind pointee = TypeKind::Void;  // для указателя
    const StructDef* pointee_layout = nullptr;
    const StructDef* layout = nullptr;  // для уклада
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
    //: параметр-ссылка на массив или уклад: в слоте лежит указатель
    bool by_ref = false;
    //: для массивов — тип элемента и длина
    TypeKind element = TypeKind::Void;
    int count = 0;
    //: для указателя — на что он указывает
    TypeKind pointee = TypeKind::Void;
    const StructDef* pointee_layout = nullptr;
    //: для уклада — его описание
    const StructDef* layout = nullptr;
    std::vector<ParamInfo> params;
    TypeKind ret = TypeKind::Void;
    const std::vector<Overload>* overloads = nullptr;
};
