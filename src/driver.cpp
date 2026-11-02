#include "driver.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "codegen.hpp"
#include "diagnostic.hpp"
#include "paths.hpp"
#include "modules.hpp"
#include "net_runtime.hpp"
#include "target.hpp"

namespace fs = std::filesystem;

namespace {

void write_file(const std::string& path, const std::string& contents) {
    std::ofstream stream(path_from_utf8(path), std::ios::binary | std::ios::trunc);
    if (!stream) fail("не удалось записать файл «" + path + "»");
    stream << contents;
    if (!stream) fail("не удалось записать файл «" + path + "»");
}

//: Имя программы кавычим только при необходимости: cmd.exe ломается,
//: когда команда начинается с кавычки.
std::string quote(const std::string& text);

std::string quote_program(const std::string& name) {
    if (name.find_first_of(" \t") == std::string::npos) return name;
    return quote(name);
}

std::string quote(const std::string& text) {
#ifdef _WIN32
    std::string out = "\"";
    for (char ch : text) {
        if (ch == '"') {
            out += "\\\"";
        } else {
            out += ch;
        }
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
#endif
}

int run_command(const std::string& command) {
    const int status = std::system(command.c_str());
    if (status == -1) return -1;
#ifdef _WIN32
    return status;
#else
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

//: Инструмент можно подменить через окружение: BERESTA_AS, BERESTA_CC.
std::string env_tool(const char* variable) {
    const char* value = std::getenv(variable);
    return (value != nullptr && *value != '\0') ? std::string(value) : std::string();
}

std::string assembler_program(Target target) {
    const std::string override = env_tool("BERESTA_AS");
    if (!override.empty()) return override;
#ifdef _WIN32
    (void)target;
    return "as";
#else
    return target == Target::Windows ? "x86_64-w64-mingw32-as" : "as";
#endif
}

std::string linker_program(Target target) {
    const std::string override = env_tool("BERESTA_CC");
    if (!override.empty()) return override;
#ifdef _WIN32
    (void)target;
    return "gcc";
#else
    return target == Target::Windows ? "x86_64-w64-mingw32-gcc" : "cc";
#endif
}

fs::path make_temp_dir() {
#ifdef _WIN32
    const fs::path base = fs::temp_directory_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path directory = base / ("beresta-" + std::to_string(stamp));
    fs::create_directories(directory);
    return directory;
#else
    const char* base = std::getenv("TMPDIR");
    std::string pattern = std::string(base && *base ? base : "/tmp") + "/beresta-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    if (mkdtemp(buffer.data()) == nullptr) fail("не удалось создать временный каталог");
    return fs::path(buffer.data());
#endif
}

std::string read_asm_source(const std::string& input, Target target, bool& uses_tls) {
    Compilation compilation = compile_program(input);
    uses_tls = compilation.program.uses_tls;
    return generate(compilation.program, target);
}

}  // namespace

long process_id() {
#ifdef _WIN32
    return static_cast<long>(
        std::chrono::steady_clock::now().time_since_epoch().count() & 0x7fffffff);
#else
    return static_cast<long>(getpid());
#endif
}

std::string build(const std::string& input, const BuildOptions& options) {
    const fs::path input_path = path_from_utf8(input);
    if (!fs::exists(input_path)) fail("файл «" + input + "» не найден");

    bool uses_tls = false;
    const std::string assembly = read_asm_source(input, options.target, uses_tls);

    fs::path asm_path;
    if (!options.asm_path.empty()) {
        asm_path = path_from_utf8(options.asm_path);
    } else if (options.emit_asm && !options.output.empty()) {
        // «-S -o файл.s» — кладём ассемблер туда, куда просили
        asm_path = options.output;
    } else {
        asm_path = input_path;
        asm_path.replace_extension(".s");
    }

    fs::path output_path;
    if (!options.output.empty()) {
        output_path = path_from_utf8(options.output);
    } else {
        output_path = input_path;
        output_path.replace_extension("");
        if (is_windows(options.target)) output_path += ".exe";
    }

    if (options.emit_asm) {
        write_file(asm_path.string(), assembly);
        return asm_path.string();
    }

    const fs::path temp_dir = make_temp_dir();
    const fs::path temp_asm = temp_dir / "program.s";
    const fs::path temp_obj = temp_dir / "program.o";
    const fs::path temp_runtime = temp_dir / "runtime.c";
    const fs::path temp_runtime_obj = temp_dir / "runtime.o";
    const fs::path temp_net = temp_dir / "net.c";
    const fs::path temp_net_obj = temp_dir / "net.o";

    write_file(temp_asm.string(), assembly);

    if (options.keep_asm) {
        write_file(asm_path.string(), assembly);
    }

    const std::string assembler = assembler_program(options.target);
    const std::string linker = linker_program(options.target);

    const int assemble_status =
        run_command(quote_program(assembler) + (is_windows(options.target) ? " -o " : " --64 -o ") +
                    quote(temp_obj.string()) + " " + quote(temp_asm.string()));
    if (assemble_status != 0) {
        fs::remove_all(temp_dir);
        fail("ассемблер не принял сгенерированный код — это баг берёсты");
    }

    std::string extra_object;
    if (is_windows(options.target)) {
        write_file(temp_runtime.string(), windows_runtime_c());

        const int runtime_status =
            run_command(quote_program(linker) + " -O2 -c -o " + quote(temp_runtime_obj.string()) + " " +
                        quote(temp_runtime.string()));
        if (runtime_status != 0) {
            fs::remove_all(temp_dir);
            fail("не удалось собрать рантайм для Windows");
        }
        extra_object = " " + quote(temp_runtime_obj.string()) + " -lws2_32";
    } else {
        write_file(temp_net.string(), NET_RUNTIME_C);
        const std::string tls_define = uses_tls ? " -DBERESTA_TLS" : "";
        const int net_status =
            run_command(quote_program(linker) + " -O2 -c" + tls_define + " -o " +
                        quote(temp_net_obj.string()) + " " + quote(temp_net.string()));
        if (net_status != 0) {
            fs::remove_all(temp_dir);
            fail("не удалось собрать сетевой рантайм");
        }
        extra_object = " " + quote(temp_net_obj.string());
        if (uses_tls) extra_object += " -lssl -lcrypto";
    }

    const int link_status = run_command(quote_program(linker) + " -o " + quote(output_path.string()) +
                                        " " + quote(temp_obj.string()) + extra_object);
    fs::remove_all(temp_dir);

    if (link_status != 0) fail("не удалось слинковать программу (cc вернул ошибку)");

    return output_path.string();
}
