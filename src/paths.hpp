#pragma once

#include <filesystem>
#include <string>

//: Строит путь из UTF-8. На Windows std::filesystem сам переводит его в UTF-16.
inline std::filesystem::path path_from_utf8(const std::string& text) {
#ifdef _WIN32
    std::u8string utf8(text.begin(), text.end());
    return std::filesystem::path(utf8);
#else
    return std::filesystem::path(text);
#endif
}
