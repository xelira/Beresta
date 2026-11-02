#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "codegen.hpp"
#include "diagnostic.hpp"
#include "paths.hpp"
#include "driver.hpp"
#include "dump.hpp"
#include "lexer.hpp"
#include "modules.hpp"
#include "target.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* VERSION = "0.14.0";

void usage() {
    std::cout <<
        "береста " << VERSION << " — компилятор языка береста\n"
        "цель: x86-64, GNU as, ELF\n\n"
        "использование:\n"
        "  berestac build <файл.бер> [-o <бинарник>] [--target linux|windows] [-S]\n"
        "  berestac run <файл.бер>\n"
        "  berestac check <файл.бер>\n"
        "  berestac asm <файл.бер> [-o <файл.s>]\n"
        "  berestac tokens <файл.бер>\n"
        "  berestac ast <файл.бер>\n"
        "  berestac version\n\n"
        "пример:\n"
        "  berestac run examples/привет.бер\n";
}

#ifdef _WIN32
std::string utf8_from_wide(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
    if (size > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], size, nullptr, nullptr);
    }
    return result;
}

std::vector<std::string> wide_arguments() {
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> result;
    if (wide == nullptr) return result;
    for (int i = 1; i < count; ++i) result.push_back(utf8_from_wide(wide[i]));
    LocalFree(wide);
    return result;
}
#endif

std::string take_option(const std::vector<std::string>& args, const std::string& name) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == name && i + 1 < args.size()) return args[i + 1];
    }
    return "";
}

Target selected_target(const std::vector<std::string>& args) {
    const std::string value = take_option(args, "--target");
    if (value == "windows" || value == "win") return Target::Windows;
    if (value == "linux") return Target::Linux;
    return default_target();
}

bool has_flag(const std::vector<std::string>& args, const std::string& name) {
    for (const auto& arg : args) {
        if (arg == name) return true;
    }
    return false;
}

void print_tokens(const std::string& source) {
    for (const Token& token : lex(source)) {
        std::cout << token.line << ":" << token.col << "  ";
        switch (token.kind) {
            case TokKind::Ident: std::cout << "имя      "; break;
            case TokKind::Keyword: std::cout << "слово    "; break;
            case TokKind::Int: std::cout << "число    "; break;
            case TokKind::Float: std::cout << "дробь    "; break;
            case TokKind::Str: std::cout << "строка   "; break;
            case TokKind::Op: std::cout << "знак     "; break;
            case TokKind::Eof: std::cout << "конец    "; break;
        }
        std::cout << "«" << token.value << "»\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    const std::vector<std::string> args = wide_arguments();
#else
    const std::vector<std::string> args(argv + 1, argv + argc);
#endif

    if (args.empty()) {
        usage();
        return 2;
    }

    const std::string command = args[0];

    if (command == "version" || command == "--version" || command == "-v") {
        std::cout << "береста " << VERSION << "\n";
        return 0;
    }

    if (command == "help" || command == "--help" || command == "-h") {
        usage();
        return 0;
    }

    if (args.size() < 2) {
        std::cerr << "ошибка: не указан файл\n\n";
        usage();
        return 2;
    }

    const std::string input = args[1];

    try {
        if (command == "tokens") {
            const LoadedSource loaded = load_source(input);
            try {
                print_tokens(loaded.combined);
            } catch (const BerestaError& error) {
                throw BerestaError(remap(loaded, error.diagnostic()));
            }
            return 0;
        }

        if (command == "ast" || command == "check" || command == "asm") {
            Compilation compilation = compile_program(input);

            if (command == "ast") {
                std::cout << dump_ast(compilation.program);
                return 0;
            }

            if (command == "check") {
                std::cout << "ок: " << input << "\n";
                return 0;
            }

            const std::string assembly = generate(compilation.program, selected_target(args));
            const std::string output = take_option(args, "-o");
            if (output.empty()) {
                std::cout << assembly;
            } else {
                std::ofstream stream(path_from_utf8(output), std::ios::binary | std::ios::trunc);
                if (!stream) fail("не удалось записать «" + output + "»");
                stream << assembly;
            }
            return 0;
        }

        if (command == "build" || command == "run") {
            BuildOptions options;
            options.output = take_option(args, "-o");
            options.asm_path = take_option(args, "--asm");
            options.emit_asm = has_flag(args, "-S") || has_flag(args, "--emit-asm");
            options.keep_asm = has_flag(args, "--keep-asm");
            options.target = selected_target(args);

            std::string binary;
            if (command == "run") {
                const fs::path temp =
                    fs::temp_directory_path() /
                    ("beresta-run-" + std::to_string(process_id()));
                options.output = temp.string();
                binary = build(input, options);
                const int status = std::system(binary.c_str());
                fs::remove(binary);
                if (status == -1) return 1;
#ifdef _WIN32
                return status;
#else
                return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
            }

            binary = build(input, options);
            if (!options.emit_asm) std::cout << "собрано: " << binary << "\n";
            return 0;
        }

        std::cerr << "ошибка: неизвестная команда «" << command << "»\n\n";
        usage();
        return 2;
    } catch (const BerestaError& error) {
        const Diagnostic& diagnostic = error.diagnostic();
        if (!diagnostic.filename.empty()) {
            std::cerr << render_diagnostic(diagnostic.filename, diagnostic.source, diagnostic);
        } else {
            std::cerr << render_diagnostic(input, "", diagnostic);
        }
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "внутренняя ошибка: " << error.what() << "\n";
        return 1;
    }
}
