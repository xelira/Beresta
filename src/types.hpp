#pragma once

#include <optional>
#include <string>

//: Типы бересты. Массив и уклад — отдельные виды.
enum class TypeKind { Int, Float, String, Bool, Void, Array, Struct };

inline const char* type_name(TypeKind kind) {
    switch (kind) {
        case TypeKind::Int: return "число";
        case TypeKind::Float: return "дробь";
        case TypeKind::String: return "строка";
        case TypeKind::Bool: return "флаг";
        case TypeKind::Void: return "ничто";
        case TypeKind::Array: return "массив";
        case TypeKind::Struct: return "уклад";
    }
    return "?";
}

inline bool is_numeric(TypeKind kind) {
    return kind == TypeKind::Int || kind == TypeKind::Float;
}

inline std::optional<TypeKind> common_numeric(TypeKind a, TypeKind b) {
    if (!is_numeric(a) || !is_numeric(b)) return std::nullopt;
    if (a == TypeKind::Int && b == TypeKind::Int) return TypeKind::Int;
    return TypeKind::Float;
}

//: число без потерь расширяется до дроби; обратно — только явно.
inline bool assignable(TypeKind src, TypeKind dst) {
    if (src == dst) return true;
    return src == TypeKind::Int && dst == TypeKind::Float;
}

inline std::optional<TypeKind> type_from_word(const std::string& word) {
    if (word == "число") return TypeKind::Int;
    if (word == "дробь") return TypeKind::Float;
    if (word == "строка") return TypeKind::String;
    if (word == "флаг") return TypeKind::Bool;
    if (word == "ничто") return TypeKind::Void;
    return std::nullopt;
}
