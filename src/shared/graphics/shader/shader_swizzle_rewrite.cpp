#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <set>
#include <vector>

#include "shader_processor_internal.h"

using namespace shader_processor_internal;

namespace {
size_t swizzleComponentIndex(char c) {
    switch (c) {
        case 'x':
        case 'r':
            return 0;
        case 'y':
        case 'g':
            return 1;
        case 'z':
        case 'b':
            return 2;
        case 'w':
        case 'a':
            return 3;
        default:
            return std::string::npos;
    }
}

bool hasRepeatedComponent(const std::string& components) {
    for (size_t a = 0; a < components.size(); ++a) {
        for (size_t b = a + 1; b < components.size(); ++b) {
            if (components[a] == components[b]) return true;
        }
    }
    return false;
}

bool isAssignmentTarget(const std::string& source, size_t pos) {
    while (pos < source.size() && std::isspace((unsigned char)source[pos])) ++pos;
    if (pos >= source.size()) return false;
    if (source[pos] == '=') return pos + 1 >= source.size() || source[pos + 1] != '=';
    return pos + 1 < source.size() && source[pos + 1] == '=' && std::strchr("+-*/", source[pos]) != nullptr;
}

}  // namespace

namespace shader_processor_internal {
// Some Wallpaper Engine shaders swizzle components the declared vector width
// does not have (e.g. `.zw` on a vec2). Remap them onto the available
// components so Slang accepts the expression.
void rewriteOutOfRangeSwizzles(std::string& source) {
    const std::map<std::string, int> widths = collectVectorWidths(source);
    if (widths.empty()) return;

    std::string result;
    size_t copied = 0;
    size_t i = 0;
    while (i < source.size()) {
        if (!std::isalpha((unsigned char)source[i]) && source[i] != '_') {
            ++i;
            continue;
        }
        const size_t name_start = i;
        while (i < source.size() && isIdentifierChar(source[i])) ++i;
        const std::string name = source.substr(name_start, i - name_start);
        const auto found = widths.find(name);
        if (found == widths.end()) continue;

        int running_width = found->second;
        size_t cursor = i;
        std::string chain;
        bool changed = false;
        bool introduced_repeat = false;
        while (cursor < source.size() && source[cursor] == '.') {
            size_t component_end = cursor + 1;
            while (component_end < source.size() && std::isalpha((unsigned char)source[component_end])) ++component_end;
            const std::string components = source.substr(cursor + 1, component_end - cursor - 1);
            if (components.empty()) break;

            std::string mapped;
            bool segment_changed = false;
            for (char component : components) {
                const size_t index = swizzleComponentIndex(component);
                if (index == std::string::npos) {
                    mapped += component;
                    continue;
                }
                size_t remapped = index;
                if (index >= (size_t)running_width) {
                    remapped = index % (size_t)running_width;
                    segment_changed = true;
                    changed = true;
                }
                mapped += "xyzw"[remapped];
            }
            if (segment_changed && hasRepeatedComponent(mapped) && !hasRepeatedComponent(components)) {
                introduced_repeat = true;
            }
            chain += "." + (segment_changed ? mapped : components);
            running_width = (int)components.size();
            cursor = component_end;
        }
        // A repeated-component swizzle is never an l-value, so remapping an
        // assignment target onto one would turn a bad guess into a new error.
        if (introduced_repeat && isAssignmentTarget(source, cursor)) changed = false;
        if (!changed) {
            i = cursor;
            continue;
        }
        result.append(source, copied, name_start - copied);
        result += name + chain;
        copied = cursor;
        i = cursor;
    }
    if (copied == 0) return;
    result.append(source, copied, std::string::npos);
    source.swap(result);
}

}  // namespace shader_processor_internal

namespace {
bool isCallArgument(const std::string& statement, size_t position) {
    int depth = 0;
    for (size_t i = position; i-- > 0;) {
        const char c = statement[i];
        if (c == ')') {
            ++depth;
        } else if (c == '(') {
            if (depth > 0) {
                --depth;
                continue;
            }
            size_t before = i;
            while (before > 0 && std::isspace((unsigned char)statement[before - 1])) --before;
            if (before == 0) return false;
            const char previous = statement[before - 1];
            return isIdentifierChar(previous) || previous == ')' || previous == ']';
        }
    }
    return false;
}

}  // namespace

namespace shader_processor_internal {
// GLSL drivers accept a vector-valued call assigned to a float by taking the
// first component; Slang requires it explicitly.
void rewriteScalarFromVectorCall(std::string& source) {
    static const std::regex scalar_call(
        R"(\bfloat\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(texSample2D|texSample2DLod|texSample2DGrad|texture2D|texture2DLod|texture|textureLod|textureGrad)\s*\(([^;]*)\)\s*;)");
    std::string result;
    size_t copied = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), scalar_call), end; it != end; ++it) {
        const std::smatch& match = *it;
        const size_t position = (size_t)match.position();
        result.append(source, copied, position - copied);
        result += "float " + match[1].str() + " = " + match[2].str() + "(" + match[3].str() + ").x;";
        copied = position + match.length();
    }
    if (copied == 0) return;
    result.append(source, copied, std::string::npos);
    source.swap(result);
}

}  // namespace shader_processor_internal

namespace {
std::string narrowStatementVectors(const std::string& statement, const std::map<std::string, int>& widths) {
    const size_t first = statement.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && statement[first] == '#') return statement;

    int target_width = 0;
    size_t target_position = 0;
    forEachDeclaration(statement, "=", isVectorTypeWord,
                       [&](const std::string& type, const std::string&, size_t name_position) {
                           target_width = type.back() - '0';
                           target_position = name_position;
                           return true;
                       });
    if (target_width != 2 && target_width != 3) return statement;

    const std::string swizzle = swizzleForWidth(target_width);

    std::string result;
    size_t copied = 0;
    static const std::regex identifier(R"([A-Za-z_][A-Za-z0-9_]*)");
    for (std::sregex_iterator it(statement.begin(), statement.end(), identifier), end; it != end; ++it) {
        const auto found = widths.find(it->str());
        if (found == widths.end() || found->second != 4) continue;

        const size_t position = (size_t)it->position();
        if (position == target_position) continue;
        const size_t after = position + it->str().size();
        size_t next = after;
        while (next < statement.size() && std::isspace((unsigned char)statement[next])) ++next;
        if (next < statement.size() && statement[next] == '.') continue;
        if (position > 0 && statement[position - 1] == '.') continue;
        if (isCallArgument(statement, position)) continue;

        result.append(statement, copied, position - copied);
        result += it->str() + swizzle;
        copied = after;
    }
    if (copied == 0) return statement;
    result.append(statement, copied, std::string::npos);
    return result;
}

// GLSL accepts a vec4 operand combined with a vec2/vec3 operand by truncating
// the wider value; Slang rejects the operator call. Narrow the wider operand,
// preserving any explicit swizzle by truncating it instead of appending one.
void rewriteImmediateVectorMismatch(std::string& source) {
    const std::map<std::string, int> widths = collectVectorWidths(source);
    if (widths.empty()) return;

    static const std::regex binary_op(
        R"(\b([A-Za-z_][A-Za-z0-9_]*)(?:\.([xyzwrgba]{1,4}))?[ \t]*([-+*/])[ \t]*([A-Za-z_][A-Za-z0-9_]*)(?:\.([xyzwrgba]{1,4}))?\b)");
    std::string result;
    size_t copied = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), binary_op), end; it != end; ++it) {
        const std::smatch& match = *it;
        const auto left_type = widths.find(match[1].str());
        const auto right_type = widths.find(match[4].str());
        if (left_type == widths.end() || right_type == widths.end()) continue;

        const std::string left_swizzle = match[2].matched ? match[2].str() : std::string();
        const std::string right_swizzle = match[5].matched ? match[5].str() : std::string();
        const int left_width = left_swizzle.empty() ? left_type->second : (int)left_swizzle.size();
        const int right_width = right_swizzle.empty() ? right_type->second : (int)right_swizzle.size();
        if (left_width < 2 || right_width < 2) continue;

        int narrow_left = 0;
        int narrow_right = 0;
        if (left_width == 4 && right_width < 4) {
            narrow_left = right_width;
        } else if (right_width == 4 && left_width < 4) {
            narrow_right = left_width;
        }
        if (!narrow_left && !narrow_right) continue;

        const size_t position = (size_t)match.position();
        result.append(source, copied, position - copied);
        result += match[1].str();
        if (narrow_left) {
            result += "." + (left_swizzle.empty() ? swizzleForWidth(narrow_left).substr(1)
                                                  : left_swizzle.substr(0, (size_t)narrow_left));
        } else if (!left_swizzle.empty()) {
            result += "." + left_swizzle;
        }
        result += " " + match[3].str() + " ";
        result += match[4].str();
        if (narrow_right) {
            result += "." + (right_swizzle.empty() ? swizzleForWidth(narrow_right).substr(1)
                                                   : right_swizzle.substr(0, (size_t)narrow_right));
        } else if (!right_swizzle.empty()) {
            result += "." + right_swizzle;
        }
        copied = position + match.length();
    }
    if (copied == 0) return;
    result.append(source, copied, std::string::npos);
    source.swap(result);
}

}  // namespace

namespace shader_processor_internal {
void rewriteBinaryVectorMismatch(std::string& source) {
    const std::map<std::string, int> widths = collectVectorWidths(source);
    if (widths.empty()) return;

    rewriteImmediateVectorMismatch(source);

    std::string result;
    size_t emitted = 0;
    size_t start = 0;
    bool changed = false;
    while (true) {
        const size_t end = source.find(';', start);
        if (end == std::string::npos) break;
        const std::string statement = source.substr(start, end - start + 1);
        const std::string narrowed = narrowStatementVectors(statement, widths);
        if (narrowed != statement) {
            result.append(source, emitted, start - emitted);
            result += narrowed;
            emitted = end + 1;
            changed = true;
        }
        start = end + 1;
    }
    if (!changed) return;
    result.append(source, emitted, std::string::npos);
    source.swap(result);
}

}  // namespace shader_processor_internal
