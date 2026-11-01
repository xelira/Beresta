#include "diagnostic.hpp"

#include <sstream>
#include <vector>

void fail(std::string message, int line, int col, int length, std::string hint) {
    Diagnostic diagnostic;
    diagnostic.message = std::move(message);
    diagnostic.hint = std::move(hint);
    diagnostic.line = line;
    diagnostic.col = col;
    diagnostic.length = length;
    diagnostic.has_position = line > 0;
    throw BerestaError(std::move(diagnostic));
}

static std::vector<std::string> split_lines(const std::string& source) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : source) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (ch != '\r') {
            current.push_back(ch);
        }
    }
    lines.push_back(current);
    return lines;
}

std::string render_diagnostic(const std::string& filename, const std::string& source,
                              const Diagnostic& diagnostic) {
    std::ostringstream out;
    out << "ошибка: " << diagnostic.message << "\n";

    if (!diagnostic.has_position) {
        if (!diagnostic.hint.empty()) out << "  подсказка: " << diagnostic.hint << "\n";
        return out.str();
    }

    const int col = diagnostic.col > 0 ? diagnostic.col : 1;
    out << "  --> " << filename << ":" << diagnostic.line << ":" << col << "\n";

    const std::vector<std::string> lines = split_lines(source);
    if (diagnostic.line >= 1 && static_cast<size_t>(diagnostic.line) <= lines.size()) {
        const std::string& raw = lines[static_cast<size_t>(diagnostic.line - 1)];
        const std::string number = std::to_string(diagnostic.line);
        const std::string gutter(number.size(), ' ');

        out << gutter << " |\n";
        out << number << " | " << raw << "\n";
        out << gutter << " | " << std::string(static_cast<size_t>(col - 1), ' ')
            << std::string(static_cast<size_t>(diagnostic.length > 0 ? diagnostic.length : 1),
                           '^')
            << "\n";
    }

    if (!diagnostic.hint.empty()) out << "  подсказка: " << diagnostic.hint << "\n";
    return out.str();
}
