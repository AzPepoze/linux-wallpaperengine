#include <cctype>
#include <set>
#include <vector>

#include "shader_processor_internal.h"

using namespace shader_processor_internal;

namespace shader_processor_internal {
bool isIdentifierChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

}  // namespace shader_processor_internal

namespace {
std::set<std::string> collectDefinedMacros(const std::string& source) {
    std::set<std::string> defined;
    size_t start = 0;
    while (start < source.size()) {
        const size_t end = source.find('\n', start);
        const std::string line = source.substr(start, (end == std::string::npos ? source.size() : end) - start);
        const size_t hash = line.find_first_not_of(" \t");
        if (hash != std::string::npos && line[hash] == '#') {
            size_t token = hash + 1;
            while (token < line.size() && (line[token] == ' ' || line[token] == '\t')) ++token;
            if (line.compare(token, 6, "define") == 0 &&
                (token + 6 >= line.size() || !isIdentifierChar(line[token + 6]))) {
                size_t name_start = token + 6;
                while (name_start < line.size() && (line[name_start] == ' ' || line[name_start] == '\t')) ++name_start;
                size_t name_end = name_start;
                while (name_end < line.size() && isIdentifierChar(line[name_end])) ++name_end;
                if (name_end > name_start) defined.insert(line.substr(name_start, name_end - name_start));
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return defined;
}

// Slang rejects member access on undefined identifiers, so rewrite them (and their chains) to zero.
std::string rewriteConditionExpression(const std::string& expression, const std::set<std::string>& defined) {
    std::string code = expression;
    std::string comment;
    const size_t comment_pos = code.find("//");
    if (comment_pos != std::string::npos) {
        comment = code.substr(comment_pos);
        code.erase(comment_pos);
    }

    size_t tail = code.find_last_not_of(" \t\r");
    if (tail != std::string::npos) code.erase(tail + 1);
    while (!code.empty() && code.back() == ';') {
        code.pop_back();
        tail = code.find_last_not_of(" \t\r");
        code.erase(tail == std::string::npos ? 0 : tail + 1);
    }

    std::string out;
    size_t i = 0;
    while (i < code.size()) {
        const char c = code[i];
        if (std::isalpha((unsigned char)c) || c == '_') {
            const size_t token_start = i;
            while (i < code.size() && isIdentifierChar(code[i])) ++i;
            const std::string name = code.substr(token_start, i - token_start);
            if (name == "defined") {
                out += name;
                while (i < code.size() && std::isspace((unsigned char)code[i])) out += code[i++];
                if (i < code.size() && code[i] == '(') {
                    int depth = 0;
                    do {
                        if (code[i] == '(')
                            ++depth;
                        else if (code[i] == ')')
                            --depth;
                        out += code[i++];
                    } while (i < code.size() && depth > 0);
                } else {
                    while (i < code.size() && isIdentifierChar(code[i])) out += code[i++];
                }
                continue;
            }
            size_t next = i;
            while (next < code.size() && std::isspace((unsigned char)code[next])) ++next;
            const bool looks_like_call = next < code.size() && code[next] == '(';
            if (defined.count(name) || looks_like_call) {
                out += name;
                continue;
            }
            out += '0';
            while (i < code.size() && code[i] == '.') {
                size_t member = i + 1;
                if (member >= code.size() || !isIdentifierChar(code[member])) break;
                while (member < code.size() && isIdentifierChar(code[member])) ++member;
                i = member;
            }
            continue;
        }
        if (std::isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < code.size() && std::isdigit((unsigned char)code[i + 1]))) {
            out += code[i++];
            while (i < code.size() && (std::isalnum((unsigned char)code[i]) || code[i] == '.')) out += code[i++];
            continue;
        }
        out += code[i++];
    }
    return out + comment;
}

}  // namespace

namespace shader_processor_internal {
std::string fixPreprocessorDirectives(const std::string& source) {
    const std::set<std::string> defined = collectDefinedMacros(source);
    std::string result;
    std::vector<bool> open_conditionals;
    size_t start = 0;
    while (start <= source.size()) {
        const size_t end = source.find('\n', start);
        const bool last = end == std::string::npos;
        std::string line = source.substr(start, (last ? source.size() : end) - start);

        const size_t hash = line.find_first_not_of(" \t");
        if (hash != std::string::npos && line[hash] == '#') {
            size_t token = hash + 1;
            while (token < line.size() && (line[token] == ' ' || line[token] == '\t')) ++token;
            size_t token_end = token;
            while (token_end < line.size() && std::isalpha((unsigned char)line[token_end])) ++token_end;
            const std::string directive = line.substr(token, token_end - token);

            if (directive == "if" || directive == "ifdef" || directive == "ifndef") {
                open_conditionals.push_back(true);
                if (directive == "if")
                    line = line.substr(0, hash) + "#if" + rewriteConditionExpression(line.substr(token_end), defined);
                result += line;
            } else if (directive == "elif") {
                if (!open_conditionals.empty()) {
                    line = line.substr(0, hash) + "#elif" + rewriteConditionExpression(line.substr(token_end), defined);
                    result += line;
                }
            } else if (directive == "else") {
                if (!open_conditionals.empty()) result += line;
            } else if (directive == "endif") {
                if (!open_conditionals.empty()) {
                    open_conditionals.pop_back();
                    result += line;
                }
            } else {
                result += line;
            }
        } else {
            result += line;
        }
        result += '\n';
        if (last) break;
        start = end + 1;
    }
    for (size_t i = 0; i < open_conditionals.size(); ++i) result += "#endif\n";
    return result;
}

}  // namespace shader_processor_internal
