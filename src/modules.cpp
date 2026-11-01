#include "modules.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"

namespace fs = std::filesystem;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        Diagnostic diagnostic;
        diagnostic.message = "не удалось открыть файл «" + path + "»";
        diagnostic.filename = path;
        throw BerestaError(std::move(diagnostic));
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string trim_left(const std::string& text) {
    const std::size_t start = text.find_first_not_of(" \t\r");
    return start == std::string::npos ? "" : text.substr(start);
}

//: Разбирает строку вида: привлечь "путь.бер";
//: Возвращает пустую строку, если это не привлечение.
std::string parse_import(const std::string& line) {
    const std::string text = trim_left(line);
    const std::string keyword = "привлечь";
    if (text.compare(0, keyword.size(), keyword) != 0) return "";

    std::size_t i = keyword.size();
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i >= text.size() || text[i] != '"') return "";

    const std::size_t open = i++;
    const std::size_t close = text.find('"', i);
    if (close == std::string::npos) return "";

    return text.substr(open + 1, close - open - 1);
}

std::string canonical_key(const fs::path& raw_path) {
    try {
        return fs::weakly_canonical(fs::absolute(raw_path)).string();
    } catch (const std::exception&) {
        return raw_path.string();
    }
}

class Loader {
public:
    LoadedSource run(const std::string& entry) {
        load(fs::path(entry), 1);
        return LoadedSource{combined_, segments_};
    }

private:
    [[noreturn]] void error(const std::string& message, const std::string& file,
                            const std::string& source, int line) {
        Diagnostic diagnostic;
        diagnostic.message = message;
        diagnostic.filename = file;
        diagnostic.source = source;
        diagnostic.line = line;
        diagnostic.col = 1;
        diagnostic.has_position = true;
        throw BerestaError(std::move(diagnostic));
    }

    void load(const fs::path& raw_path, int import_line) {
        const std::string key = canonical_key(raw_path);
        const fs::path path = fs::path(key);

        if (done_.count(key) > 0) return;

        for (const std::string& active : stack_) {
            if (active == key) {
                error("циклическое привлечение «" + key + "»", key, "", import_line);
            }
        }

        const std::string source = read_file(key);

        stack_.push_back(key);
        std::istringstream stream(source);
        std::string line;
        std::string processed;
        int emitted = 0;

        while (std::getline(stream, line)) {
            const std::string imported = parse_import(line);
            if (!imported.empty()) {
                fs::path child = imported;
                if (child.is_relative()) child = path.parent_path() / child;
                const std::string child_key = canonical_key(child);
                for (const std::string& active : stack_) {
                    if (active == child_key) {
                        error("циклическое привлечение «" + child_key + "»", key, source,
                              emitted + 1);
                    }
                }
                load(fs::path(child_key), emitted + 1);
                processed += "\n";
            } else {
                processed += line;
                processed += "\n";
            }
            ++emitted;
        }

        stack_.pop_back();
        done_.insert(key);

        Segment segment;
        segment.file = key;
        segment.source = source;
        segment.start_line = cursor_;
        segment.lines = emitted;

        combined_ += processed;
        cursor_ += emitted;
        segments_.push_back(std::move(segment));
    }

    std::string combined_;
    std::vector<Segment> segments_;
    std::unordered_set<std::string> done_;
    std::vector<std::string> stack_;
    int cursor_ = 1;
};

}  // namespace

LoadedSource load_source(const std::string& entry) { return Loader().run(entry); }

Diagnostic remap(const LoadedSource& loaded, const Diagnostic& diagnostic) {
    if (!diagnostic.has_position) return diagnostic;

    const Segment* best = nullptr;
    for (const Segment& segment : loaded.segments) {
        if (segment.start_line <= diagnostic.line &&
            diagnostic.line < segment.start_line + segment.lines) {
            best = &segment;
        }
    }

    if (best == nullptr) return diagnostic;

    Diagnostic result = diagnostic;
    result.filename = best->file;
    result.source = best->source;
    result.line = diagnostic.line - best->start_line + 1;
    return result;
}

Compilation compile_program(const std::string& entry) {
    Compilation compilation;
    compilation.loaded = load_source(entry);

    try {
        compilation.program = parse(lex(compilation.loaded.combined));
        analyze(compilation.program);
    } catch (const BerestaError& error) {
        throw BerestaError(remap(compilation.loaded, error.diagnostic()));
    }

    return compilation;
}
