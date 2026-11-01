#include "driver.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "codegen.hpp"
#include "diagnostic.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"

namespace fs = std::filesystem;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail("не удалось открыть файл «" + path + "»");

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void write_file(const std::string& path, const std::string& contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) fail("не удалось записать файл «" + path + "»");
    stream << contents;
    if (!stream) fail("не удалось записать файл «" + path + "»");
}

std::string quote(const std::string& text) {
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
}

int run_command(const std::string& command) {
    const int status = std::system(command.c_str());
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

fs::path make_temp_dir() {
    const char* base = std::getenv("TMPDIR");
    std::string pattern = std::string(base && *base ? base : "/tmp") + "/beresta-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');

    if (mkdtemp(buffer.data()) == nullptr) fail("не удалось создать временный каталог");
    return fs::path(buffer.data());
}

std::string read_asm_source(const std::string& input) {
    const std::string source = read_file(input);
    Program program = parse(lex(source));
    analyze(program);
    return generate(program);
}

}  // namespace

std::string build(const std::string& input, const BuildOptions& options) {
    const fs::path input_path(input);
    if (!fs::exists(input_path)) fail("файл «" + input + "» не найден");

    const std::string assembly = read_asm_source(input);

    fs::path asm_path;
    if (!options.asm_path.empty()) {
        asm_path = options.asm_path;
    } else if (options.emit_asm && !options.output.empty()) {
        // «-S -o файл.s» — кладём ассемблер туда, куда просили
        asm_path = options.output;
    } else {
        asm_path = input_path;
        asm_path.replace_extension(".s");
    }

    fs::path output_path;
    if (!options.output.empty()) {
        output_path = options.output;
    } else {
        output_path = input_path;
        output_path.replace_extension("");
    }

    if (options.emit_asm) {
        write_file(asm_path.string(), assembly);
        return asm_path.string();
    }

    const fs::path temp_dir = make_temp_dir();
    const fs::path temp_asm = temp_dir / "программа.s";
    const fs::path temp_obj = temp_dir / "программа.o";

    write_file(temp_asm.string(), assembly);

    if (options.keep_asm) {
        write_file(asm_path.string(), assembly);
    }

    const int assemble_status = run_command("as --64 -o " + quote(temp_obj.string()) + " " +
                                            quote(temp_asm.string()));
    if (assemble_status != 0) {
        fs::remove_all(temp_dir);
        fail("ассемблер не принял сгенерированный код — это баг берёсты");
    }

    const int link_status =
        run_command("cc -o " + quote(output_path.string()) + " " + quote(temp_obj.string()));
    fs::remove_all(temp_dir);

    if (link_status != 0) fail("не удалось слинковать программу (cc вернул ошибку)");

    return output_path.string();
}
