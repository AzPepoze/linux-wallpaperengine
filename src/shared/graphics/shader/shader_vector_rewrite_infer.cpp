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

namespace {

bool isNumericLiteral(const std::string& text) {
    const size_t size = text.size();
    size_t i = 0;
    while (i < size && isDigit(text[i])) ++i;
    const bool has_integer_part = i > 0;
    if (i < size && text[i] == '.') {
        ++i;
        const size_t fraction_start = i;
        while (i < size && isDigit(text[i])) ++i;
        if (!has_integer_part && i == fraction_start) return false;
    } else if (!has_integer_part) {
        return false;
    }
    if (i < size && (text[i] == 'e' || text[i] == 'E')) {
        size_t j = i + 1;
        if (j < size && (text[j] == '+' || text[j] == '-')) ++j;
        const size_t exponent_start = j;
        while (j < size && isDigit(text[j])) ++j;
        if (j > exponent_start) i = j;
    }
    if (i < size && (text[i] == 'f' || text[i] == 'F')) ++i;
    return i == size;
}

bool parseSwizzledIdentifier(const std::string& text, std::string& name, int& swizzle_length) {
    const size_t dot = text.find('.');
    name = text.substr(0, dot);
    if (!isIdentifier(name)) return false;
    swizzle_length = -1;
    if (dot == std::string::npos) return true;
    const std::string swizzle = text.substr(dot + 1);
    if (swizzle.empty() || swizzle.size() > 4) return false;
    for (char c : swizzle) {
        if (std::string("xyzwrgba").find(c) == std::string::npos) return false;
    }
    swizzle_length = (int)swizzle.size();
    return true;
}

bool startsWithCall(const std::string& text) {
    size_t i = identifierLength(text);
    if (i == 0) return false;
    while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
    return i < text.size() && text[i] == '(';
}

bool isScalarReducingBuiltin(const std::string& name) {
    static const std::set<std::string> reducing = {"dot", "length", "distance", "determinant", "float",
                                                   "int", "uint",   "bool",     "any",         "all"};
    return reducing.count(name) != 0;
}

bool isTypePreservingBuiltin(const std::string& name) {
    static const std::set<std::string> preserving = {
        "abs",  "sign", "floor", "ceil",    "trunc",   "round", "roundEven", "fract",       "sqrt",
        "exp",  "exp2", "log",   "log2",    "sin",     "cos",   "tan",       "asin",        "acos",
        "sinh", "cosh", "tanh",  "radians", "degrees", "fabs",  "saturate",  "inversesqrt", "normalize"};
    return preserving.count(name) != 0;
}

// Position of the lowest-precedence top-level arithmetic operator, or npos.
size_t findTopLevelOperator(const std::string& text) {
    int depth = 0;
    size_t multiplicative = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            --depth;
        } else if (depth == 0) {
            if (c == '+' || c == '-') {
                size_t before = i;
                while (before > 0 && std::isspace((unsigned char)text[before - 1])) --before;
                const char previous = before == 0 ? '\0' : text[before - 1];
                if (previous != '\0' && std::strchr("(+-*/,<>=!&|?%", previous) == nullptr) return i;
            } else if (c == '*' || c == '/') {
                multiplicative = i;
            }
        }
    }
    return multiplicative;
}

int inferExpressionWidth(const std::string& expression, const std::map<std::string, int>& widths);

int inferCallWidth(const std::string& expression, const std::map<std::string, int>& widths) {
    const size_t open = expression.find('(');
    if (open == std::string::npos) return -1;
    const std::string name = trimSpaces(expression.substr(0, open));
    if (name == "vec2") return 2;
    if (name == "vec3") return 3;
    if (name == "vec4") return 4;
    if (isScalarReducingBuiltin(name)) return 0;
    if (isTypePreservingBuiltin(name)) {
        const size_t close = expression.rfind(')');
        if (close == std::string::npos || close <= open) return -1;
        std::vector<std::string> arguments;
        splitTopLevelArguments(expression.substr(open + 1, close - open - 1), arguments);
        if (arguments.size() != 1) return -1;
        return inferExpressionWidth(arguments[0], widths);
    }
    return -1;
}

// Vector width of an expression: 0 for a scalar, -1 when it cannot be determined safely.
int inferExpressionWidth(const std::string& expression, const std::map<std::string, int>& widths) {
    const std::string text = trimSpaces(expression);
    if (text.empty()) return -1;

    if (text.front() == '(' && text.back() == ')') {
        int depth = 0;
        bool spans_whole = true;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '(') {
                ++depth;
            } else if (text[i] == ')') {
                --depth;
                if (depth == 0 && i + 1 != text.size()) {
                    spans_whole = false;
                    break;
                }
            }
        }
        if (spans_whole && depth == 0) return inferExpressionWidth(text.substr(1, text.size() - 2), widths);
    }

    if (text.front() == '-' || text.front() == '+' || text.front() == '!' || text.front() == '~')
        return inferExpressionWidth(text.substr(1), widths);

    if (isNumericLiteral(text)) return 0;

    std::string identifier_name;
    int swizzle_length = -1;
    if (parseSwizzledIdentifier(text, identifier_name, swizzle_length)) {
        const auto found = widths.find(identifier_name);
        if (found == widths.end()) return 0;
        if (swizzle_length >= 0) return swizzle_length;
        return found->second;
    }

    if (startsWithCall(text)) return inferCallWidth(text, widths);

    const size_t op = findTopLevelOperator(text);
    if (op != std::string::npos) {
        const int left = inferExpressionWidth(text.substr(0, op), widths);
        const int right = inferExpressionWidth(text.substr(op + 1), widths);
        if (left < 0 || right < 0) return -1;
        return std::max(left, right);
    }
    return -1;
}

}  // namespace

void rewriteScalarVectorBroadcast(std::string& source) {
    const std::map<std::string, int> widths = collectVectorWidths(source);
    if (widths.empty()) return;

    static const std::regex call(R"(\b(min|max|clamp)\s*\()");
    std::string result;
    size_t copied = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), call), end; it != end; ++it) {
        const size_t open = (size_t)it->position() + it->length() - 1;
        if (open < copied) continue;
        const size_t close = findMatchingParen(source, open);
        if (close == std::string::npos) continue;

        std::vector<std::string> arguments;
        splitTopLevelArguments(source.substr(open + 1, close - open - 1), arguments);
        if (arguments.size() < 2) continue;

        const std::string first = trimSpaces(arguments[0]);
        if (!isNumericLiteral(first)) continue;

        int width = 0;
        bool known = true;
        for (size_t i = 1; i < arguments.size(); ++i) {
            const int argument_width = inferExpressionWidth(arguments[i], widths);
            if (argument_width < 0) {
                known = false;
                break;
            }
            width = std::max(width, argument_width);
        }
        if (!known || width < 2) continue;

        result.append(source, copied, open + 1 - copied);
        result += "vec" + std::to_string(width) + "(" + first + ")";
        for (size_t i = 1; i < arguments.size(); ++i) {
            result += ",";
            result += arguments[i];
        }
        copied = close;
    }
    if (copied == 0) return;
    result.append(source, copied, std::string::npos);
    source.swap(result);
}

}  // namespace shader_processor_internal
