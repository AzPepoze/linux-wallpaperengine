#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

#include "shader_processor_internal.h"

using namespace shader_processor_internal;

namespace shader_processor_internal {
std::map<std::string, int> collectVectorWidths(const std::string& source) {
    static const std::regex vector_decl(
        R"(\b(vec[234]|float[234]|ivec[234]|int[234])\s+([A-Za-z_][A-Za-z0-9_]*)\s*[;=,\[\)])");
    static const std::regex nonvector_decl(
        R"(\b(float|double|int|uint|bool|mat[234])\s+([A-Za-z_][A-Za-z0-9_]*)\s*[;=,\[\)])");
    std::map<std::string, int> widths;
    std::set<std::string> conflicting;
    for (std::sregex_iterator it(source.begin(), source.end(), vector_decl), end; it != end; ++it) {
        const std::string type = (*it)[1].str();
        const std::string name = (*it)[2].str();
        const int width = type.back() - '0';
        const auto existing = widths.find(name);
        if (existing == widths.end()) {
            widths[name] = width;
        } else if (existing->second != width) {
            conflicting.insert(name);
        }
    }
    for (std::sregex_iterator it(source.begin(), source.end(), nonvector_decl), end; it != end; ++it) {
        conflicting.insert((*it)[2].str());
    }
    for (const std::string& name : conflicting) widths.erase(name);
    return widths;
}

std::string swizzleForWidth(int width) {
    if (width == 2) return ".xy";
    if (width == 3) return ".xyz";
    return {};
}

// GLSL drivers silently truncate vectors when assigning a wider value to a
// narrower variable; Slang requires the swizzle to be explicit.
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

}  // namespace shader_processor_internal

namespace {
bool isVectorIdentifier(const std::map<std::string, int>& widths, const std::string& token, int& width) {
    static const std::regex identifier(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
    if (!std::regex_match(token, identifier)) return false;
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

}  // namespace

namespace shader_processor_internal {
// Sampler uv/derivative arguments are vec2; GLSL truncates a vec4 passed
// there, Slang rejects it.
void rewriteSamplerArguments(std::string& source) {
    const std::map<std::string, int> widths = collectVectorWidths(source);
    if (widths.empty()) return;

    static const std::regex sampler_call(
        R"(\b(texSample2D|texSample2DLod|texSample2DGrad|texture2D|texture2DLod|textureGrad|texture|textureLod)\s*\()");
    std::string result;
    size_t copied = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), sampler_call), end; it != end; ++it) {
        const size_t open = (size_t)it->position() + it->length() - 1;
        if (open < copied) continue;
        const size_t close = findMatchingParen(source, open);
        if (close == std::string::npos) continue;

        std::vector<std::string> arguments;
        splitTopLevelArguments(source.substr(open + 1, close - open - 1), arguments);
        bool changed = false;
        for (size_t index = 1; index < arguments.size(); ++index) {
            const std::string narrowed = narrowBareVec4InExpression(arguments[index], widths);
            if (narrowed != arguments[index]) {
                arguments[index] = narrowed;
                changed = true;
            }
        }
        if (!changed) continue;

        result.append(source, copied, open + 1 - copied);
        for (size_t index = 0; index < arguments.size(); ++index) {
            if (index) result += ",";
            result += arguments[index];
        }
        copied = close;
    }
    if (copied == 0) return;
    result.append(source, copied, std::string::npos);
    source.swap(result);
}

// GLSL resolves `mix(vec4, vec3, float)` by widening the vec3. Slang only
// accepts matching operand types, so widen the narrow operand explicitly.
void rewriteMixVectorWidths(std::string& source) {
    const std::map<std::string, int> widths = collectVectorWidths(source);
    if (widths.empty()) return;

    std::string result;
    size_t copied = 0;
    size_t pos = 0;
    while ((pos = source.find("mix(", pos)) != std::string::npos) {
        if (pos > 0 && isIdentifierChar(source[pos - 1])) {
            pos += 4;
            continue;
        }
        const size_t open = pos + 3;
        const size_t close = findMatchingParen(source, open);
        if (close == std::string::npos) break;

        std::vector<std::string> arguments;
        splitTopLevelArguments(source.substr(open + 1, close - open - 1), arguments);
        if (arguments.size() == 3) {
            const std::string first = trimSpaces(arguments[0]);
            const std::string second = trimSpaces(arguments[1]);
            const std::string third = trimSpaces(arguments[2]);
            int width1 = 0, width2 = 0, width3 = 0;
            const bool first_is_vector = isVectorIdentifier(widths, first, width1);
            const bool second_is_vector = isVectorIdentifier(widths, second, width2);
            const bool third_is_vector = isVectorIdentifier(widths, third, width3);

            std::string replacement;
            if (first_is_vector && second_is_vector && !third_is_vector && width1 == 4 && width2 == 3) {
                replacement = "mix((" + first + ").rgb, (" + second + "), (" + third + "))";
            } else if (first_is_vector && second_is_vector && !third_is_vector && width1 == 3 && width2 == 4) {
                replacement = "mix((" + first + "), (" + second + ").rgb, (" + third + "))";
            }
            if (!replacement.empty()) {
                result.append(source, copied, pos - copied);
                result += replacement;
                copied = close + 1;
                pos = close + 1;
                continue;
            }
        }
        pos = close + 1;
    }
    if (copied == 0) return;
    result.append(source, copied, std::string::npos);
    source.swap(result);
}

}  // namespace shader_processor_internal

namespace {
int vectorWidthFromTypeToken(const std::string& token) {
    static const std::regex vector_type(R"(^(?:(?:[biu]?vec)|(?:float)|(?:int))([234])$)");
    static const std::regex scalar_type(R"(^(?:float|double|int|uint|bool|mat[234])$)");
    std::smatch match;
    if (std::regex_match(token, match, vector_type)) return match[1].str()[0] - '0';
    if (std::regex_match(token, scalar_type)) return 0;
    return -1;
}

int classifyParameter(const std::string& parameter) {
    static const std::regex qualifier(R"(^(?:in|out|inout|const|lowp|mediump|highp|flat|noperspective|smooth)$)");
    std::istringstream stream(parameter);
    std::string token;
    while (stream >> token) {
        if (std::regex_match(token, qualifier)) continue;
        const int width = vectorWidthFromTypeToken(token);
        if (width != -1) return width;
        return -1;
    }
    return -1;
}

std::map<std::string, std::vector<int>> collectFunctionSignatures(const std::string& source) {
    static const std::regex function(
        R"(\b(?:void|float|double|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234])\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;{}]*)\))");
    std::map<std::string, std::vector<int>> signatures;
    std::set<std::string> ambiguous;
    for (std::sregex_iterator it(source.begin(), source.end(), function), end; it != end; ++it) {
        const std::string name = (*it)[1].str();
        std::vector<std::string> parameters;
        splitTopLevelArguments((*it)[2].str(), parameters);
        std::vector<int> widths;
        for (const std::string& parameter : parameters) {
            if (trimSpaces(parameter).empty()) continue;
            widths.push_back(classifyParameter(parameter));
        }
        const auto existing = signatures.find(name);
        if (existing == signatures.end()) {
            signatures[name] = std::move(widths);
        } else if (existing->second != widths) {
            ambiguous.insert(name);
        }
    }
    for (const std::string& name : ambiguous) signatures.erase(name);
    return signatures;
}

}  // namespace

namespace shader_processor_internal {
// GLSL truncates a wider vector argument to a narrower function parameter;
// Slang rejects the call. Only narrows arguments whose declared parameter
// width is known and unambiguous.
void rewriteFunctionArguments(std::string& source) {
    const std::map<std::string, int> widths = collectVectorWidths(source);
    const std::map<std::string, std::vector<int>> signatures = collectFunctionSignatures(source);
    if (widths.empty() || signatures.empty()) return;

    static const std::regex call(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\()");
    std::string result;
    size_t copied = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), call), end; it != end; ++it) {
        const auto signature = signatures.find((*it)[1].str());
        if (signature == signatures.end()) continue;
        const size_t open = (size_t)it->position() + it->length() - 1;
        if (open < copied) continue;
        const size_t close = findMatchingParen(source, open);
        if (close == std::string::npos) continue;

        std::vector<std::string> arguments;
        splitTopLevelArguments(source.substr(open + 1, close - open - 1), arguments);
        if (arguments.size() != signature->second.size()) continue;

        bool changed = false;
        for (size_t index = 0; index < arguments.size(); ++index) {
            const int parameter_width = signature->second[index];
            if (parameter_width != 2 && parameter_width != 3) continue;
            const std::string argument = trimSpaces(arguments[index]);
            const auto argument_width = widths.find(argument);
            if (argument_width == widths.end() || argument_width->second != 4) continue;
            arguments[index] = argument + swizzleForWidth(parameter_width);
            changed = true;
        }
        if (!changed) continue;

        result.append(source, copied, open + 1 - copied);
        for (size_t index = 0; index < arguments.size(); ++index) {
            if (index) result += ",";
            result += arguments[index];
        }
        copied = close;
    }
    if (copied == 0) return;
    result.append(source, copied, std::string::npos);
    source.swap(result);
}

}  // namespace shader_processor_internal

namespace {
bool isNumericLiteral(const std::string& text) {
    static const std::regex number(R"(^(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?[fF]?$)");
    return std::regex_match(text, number);
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

// Vector width an expression evaluates to, 0 for a scalar, or -1 when the
// shape cannot be determined safely.
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

    static const std::regex identifier(R"(^([A-Za-z_][A-Za-z0-9_]*)(?:\.([xyzwrgba]{1,4}))?$)");
    std::smatch match;
    if (std::regex_match(text, match, identifier)) {
        const auto found = widths.find(match[1].str());
        if (found == widths.end()) return 0;
        if (match[2].matched) return (int)match[2].str().size();
        return found->second;
    }

    static const std::regex call(R"(^([A-Za-z_][A-Za-z0-9_]*)\s*\()");
    if (std::regex_search(text, match, call) && match.position() == 0) return inferCallWidth(text, widths);

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

namespace shader_processor_internal {
// Wallpaper Engine shaders are compiled as HLSL, which broadcasts a scalar to
// the width of a vector argument (e.g. `max(1, someVec2)`). GLSL only defines
// the vector-first overload, so broadcast the scalar explicitly for Slang.
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
