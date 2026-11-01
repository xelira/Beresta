#pragma once

#include <string>

//: Целевая платформа кодогенерации.
enum class Target { Linux, Windows };

inline bool is_windows(Target target) { return target == Target::Windows; }

inline const char* target_name(Target target) {
    return target == Target::Windows ? "windows" : "linux";
}

inline Target default_target() {
#ifdef _WIN32
    return Target::Windows;
#else
    return Target::Linux;
#endif
}
