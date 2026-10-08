#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

#include "shader_processor_internal.h"
#include "shader_vector_rewrite_internal.h"

using namespace shader_processor_internal;

namespace shader_processor_internal {

std::map<std::string, int> collectVectorWidths(const std::string& source) {
    static const char* const kNonVectorTypes[] = {"float", "double", "int", "uint", "bool", "mat2", "mat3", "mat4"};
    const auto isNonVectorType = [](const std::string& word) {
        return std::any_of(std::begin(kNonVectorTypes), std::end(kNonVectorTypes),
                           [&](const char* type) { return word == type; });
    };

    std::map<std::string, int> widths;
    std::set<std::string> conflicting;
    forEachDeclaration(source, ";=,[)", isVectorTypeWord,
                       [&](const std::string& type, const std::string& name, size_t) {
                           const int width = type.back() - '0';
                           const auto existing = widths.find(name);
                           if (existing == widths.end()) {
                               widths[name] = width;
                           } else if (existing->second != width) {
                               conflicting.insert(name);
                           }
                           return false;
                       });
    forEachDeclaration(source, ";=,[)", isNonVectorType, [&](const std::string&, const std::string& name, size_t) {
        conflicting.insert(name);
        return false;
    });
    for (const std::string& name : conflicting) widths.erase(name);
    return widths;
}

std::string swizzleForWidth(int width) {
    if (width == 2) return ".xy";
    if (width == 3) return ".xyz";
    return {};
}

// Slang requires an explicit swizzle when a wider vector is assigned to a narrower variable.
void rewriteNarrowingConversions(std::string& source) {
    const std::map<std::string, int> widths = collectVectorWidths(source);
    if (widths.empty()) return;

    static const std::regex assignment(R"(\b([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*;)");
    std::string result;
    size_t copied = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), assignment), end; it != end; ++it) {
        const std::smatch& match = *it;
        const auto target = widths.find(match[1].str());
        const auto value = widths.find(match[2].str());
        if (target == widths.end() || value == widths.end() || target->second >= value->second) continue;
        const std::string swizzle = swizzleForWidth(target->second);
        if (swizzle.empty()) continue;

        const size_t match_pos = (size_t)match.position();
        result.append(source, copied, match_pos - copied);
        result += match[1].str() + " = " + match[2].str() + swizzle + ";";
        copied = match_pos + match.length();
    }
    if (copied == 0) return;
    result.append(source, copied, std::string::npos);
    source.swap(result);
}

namespace {

bool isIdentifierStart(char c) {
    return std::isalpha((unsigned char)c) || c == '_';
}

}  // namespace

size_t identifierLength(const std::string& text) {
    if (text.empty() || !isIdentifierStart(text[0])) return 0;
    size_t length = 1;
    while (length < text.size() && isIdentifierChar(text[length])) ++length;
    return length;
}

bool isIdentifier(const std::string& text) {
    return !text.empty() && identifierLength(text) == text.size();
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isVectorIdentifier(const std::map<std::string, int>& widths, const std::string& token, int& width) {
    if (!isIdentifier(token)) return false;
    const auto it = widths.find(token);
    if (it == widths.end()) return false;
    width = it->second;
    return true;
}

size_t findMatchingParen(const std::string& text, size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '(')
            ++depth;
        else if (text[i] == ')') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

void splitTopLevelArguments(const std::string& text, std::vector<std::string>& out) {
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(' || c == '[')
            ++depth;
        else if (c == ')' || c == ']')
            --depth;
        else if (c == ',' && depth == 0) {
            out.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(text.substr(start));
}

std::string trimSpaces(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string narrowBareVec4InExpression(const std::string& expression, const std::map<std::string, int>& widths) {
    static const std::regex identifier(R"([A-Za-z_][A-Za-z0-9_]*)");
    for (std::sregex_iterator it(expression.begin(), expression.end(), identifier), end; it != end; ++it) {
        size_t next = (size_t)it->position() + it->str().size();
        while (next < expression.size() && std::isspace((unsigned char)expression[next])) ++next;
        if (next < expression.size() && expression[next] == '(') return expression;
    }

    std::string result;
    size_t copied = 0;
    for (std::sregex_iterator it(expression.begin(), expression.end(), identifier), end; it != end; ++it) {
        const auto found = widths.find(it->str());
        if (found == widths.end() || found->second != 4) continue;
        const size_t position = (size_t)it->position();
        const size_t after = position + it->str().size();
        size_t next = after;
        while (next < expression.size() && std::isspace((unsigned char)expression[next])) ++next;
        if (next < expression.size() && expression[next] == '.') continue;
        if (position > 0 && expression[position - 1] == '.') continue;

        result.append(expression, copied, position - copied);
        result += it->str() + ".xy";
        copied = after;
    }
    if (copied == 0) return expression;
    result.append(expression, copied, std::string::npos);
    return result;
}

}  // namespace shader_processor_internal
