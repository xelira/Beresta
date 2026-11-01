#include <unistd.h>
#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "codegen.hpp"
#include "diagnostic.hpp"
#include "driver.hpp"
#include "dump.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* VERSION = "0.2.0";

void usage() {
    std::cout <<
        "береста " << VERSION << " — компилятор языка береста\n"
        "цель: x86-64, GNU as, ELF\n\n"
        "использование:\n"
        "  berestac build <файл.бер> [-o <бинарник>] [-S] [--keep-asm] [--asm <файл.s>]\n"
        "  berestac run <файл.бер>\n"
        "  berestac check <файл.бер>\n"
        "  berestac asm <файл.бер> [-o <файл.s>]\n"
        "  berestac tokens <файл.бер>\n"
        "  berestac ast <файл.бер>\n"
        "  berestac version\n\n"
        "пример:\n"
        "  berestac run examples/привет.бер\n";
}

std::string read_source(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail("не удалось открыть файл «" + path + "»");
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string take_option(const std::vector<std::string>& args, const std::string& name) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == name && i + 1 < args.size()) return args[i + 1];
    }
    return "";
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
    const std::vector<std::string> args(argv + 1, argv + argc);

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
        const std::string source = read_source(input);

        if (command == "tokens") {
            print_tokens(source);
            return 0;
        }

        if (command == "ast") {
            Program program = parse(lex(source));
            analyze(program);
            std::cout << dump_ast(program);
            return 0;
        }

        if (command == "check") {
            Program program = parse(lex(source));
            analyze(program);
            std::cout << "ок: " << input << "\n";
            return 0;
        }

        if (command == "asm") {
            Program program = parse(lex(source));
            analyze(program);
            const std::string assembly = generate(program);

            const std::string output = take_option(args, "-o");
            if (output.empty()) {
                std::cout << assembly;
            } else {
                std::ofstream stream(output, std::ios::binary | std::ios::trunc);
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

            std::string binary;
            if (command == "run") {
                const fs::path temp =
                    fs::temp_directory_path() /
                    ("beresta-run-" + std::to_string(static_cast<long long>(getpid())));
                options.output = temp.string();
                binary = build(input, options);
                const int status = std::system(binary.c_str());
                fs::remove(binary);
                if (status == -1) return 1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            }

            binary = build(input, options);
            if (!options.emit_asm) std::cout << "собрано: " << binary << "\n";
            return 0;
        }

        std::cerr << "ошибка: неизвестная команда «" << command << "»\n\n";
        usage();
        return 2;
    } catch (const BerestaError& error) {
        std::cerr << render_diagnostic(input, read_source(input), error.diagnostic());
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "внутренняя ошибка: " << error.what() << "\n";
        return 1;
    }
}
