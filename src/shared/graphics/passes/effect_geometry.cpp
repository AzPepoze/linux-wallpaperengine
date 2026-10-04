#include "effect_geometry.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "shared/core/logger.h"

namespace {
enum class Truth { False, True, Unknown };

struct ConditionalFrame {
    Truth parent = Truth::True;
    Truth prior = Truth::False;
    Truth active = Truth::True;
};

std::string trim(std::string value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace((unsigned char)value[begin])) ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace((unsigned char)value[end - 1])) --end;
    return value.substr(begin, end - begin);
}

bool isIdentifier(const std::string& value) {
    if (value.empty() || !(std::isalpha((unsigned char)value[0]) || value[0] == '_')) return false;
    for (size_t i = 1; i < value.size(); ++i) {
        if (!(std::isalnum((unsigned char)value[i]) || value[i] == '_')) return false;
    }
    return true;
}

Truth truthNot(Truth value) {
    if (value == Truth::True) return Truth::False;
    if (value == Truth::False) return Truth::True;
    return Truth::Unknown;
}

Truth truthAnd(Truth lhs, Truth rhs) {
    if (lhs == Truth::False || rhs == Truth::False) return Truth::False;
    if (lhs == Truth::True && rhs == Truth::True) return Truth::True;
    return Truth::Unknown;
}

Truth truthOr(Truth lhs, Truth rhs) {
    if (lhs == Truth::True || rhs == Truth::True) return Truth::True;
    if (lhs == Truth::False && rhs == Truth::False) return Truth::False;
    return Truth::Unknown;
}

std::string stripOuterParens(std::string value) {
    value = trim(value);
    while (value.size() >= 2 && value.front() == '(' && value.back() == ')') {
        int depth = 0;
        bool wraps_all = true;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '(') ++depth;
            if (value[i] == ')') --depth;
            if (depth == 0 && i + 1 < value.size()) {
                wraps_all = false;
                break;
            }
        }
        if (!wraps_all) break;
        value = trim(value.substr(1, value.size() - 2));
    }
    return value;
}

size_t findTopLevelOperator(const std::string& expression, const char* op) {
    int depth = 0;
    const size_t op_len = std::char_traits<char>::length(op);
    for (size_t i = 0; i + op_len <= expression.size(); ++i) {
        if (expression[i] == '(') ++depth;
        if (expression[i] == ')') --depth;
        if (depth == 0 && expression.compare(i, op_len, op) == 0) return i;
    }
    return std::string::npos;
}

Truth evaluateCondition(std::string expression, const std::map<std::string, int>& defines) {
    const size_t comment = expression.find("//");
    if (comment != std::string::npos) expression.erase(comment);
    expression = stripOuterParens(expression);
    if (expression.empty()) return Truth::Unknown;

    size_t op = findTopLevelOperator(expression, "||");
    if (op != std::string::npos)
        return truthOr(evaluateCondition(expression.substr(0, op), defines),
                       evaluateCondition(expression.substr(op + 2), defines));

    op = findTopLevelOperator(expression, "&&");
    if (op != std::string::npos)
        return truthAnd(evaluateCondition(expression.substr(0, op), defines),
                        evaluateCondition(expression.substr(op + 2), defines));

    if (expression[0] == '!') return truthNot(evaluateCondition(expression.substr(1), defines));

    if (expression.compare(0, 7, "defined") == 0) {
        std::string name = trim(expression.substr(7));
        name = stripOuterParens(name);
        if (!isIdentifier(name)) return Truth::Unknown;
        return defines.count(name) ? Truth::True : Truth::False;
    }

    char* end = nullptr;
    long numeric = std::strtol(expression.c_str(), &end, 0);
    if (end && *end == '\0') return numeric == 0 ? Truth::False : Truth::True;

    if (!isIdentifier(expression)) return Truth::Unknown;
    auto it = defines.find(expression);
    if (it == defines.end()) return Truth::False;
    return it->second == 0 ? Truth::False : Truth::True;
}

Truth currentState(const std::vector<ConditionalFrame>& stack) {
    return stack.empty() ? Truth::True : stack.back().active;
}

void readDefine(const std::string& directive, std::map<std::string, int>& defines) {
    std::istringstream stream(directive);
    std::string hash_define;
    std::string name;
    std::string value;
    stream >> hash_define >> name >> value;
    if (!isIdentifier(name)) return;

    int parsed = 1;
    if (!value.empty()) {
        char* end = nullptr;
        long numeric = std::strtol(value.c_str(), &end, 0);
        if (!end || *end != '\0') return;
        parsed = (int)numeric;
    }
    defines[name] = parsed;
}

std::string activeShaderCode(const std::string& source) {
    std::map<std::string, int> defines;
    std::vector<ConditionalFrame> stack;
    std::string active;
    std::istringstream input(source);
    std::string line;

    while (std::getline(input, line)) {
        const std::string directive = trim(line);
        if (directive.compare(0, 7, "#define") == 0) {
            if (currentState(stack) == Truth::True) readDefine(directive, defines);
            continue;
        }
        if (directive.compare(0, 6, "#ifdef") == 0 || directive.compare(0, 7, "#ifndef") == 0 ||
            directive.compare(0, 3, "#if") == 0) {
            Truth condition = Truth::Unknown;
            if (directive.compare(0, 6, "#ifdef") == 0) {
                const std::string name = trim(directive.substr(6));
                condition = defines.count(name) ? Truth::True : Truth::False;
            } else if (directive.compare(0, 7, "#ifndef") == 0) {
                const std::string name = trim(directive.substr(7));
                condition = defines.count(name) ? Truth::False : Truth::True;
            } else {
                condition = evaluateCondition(directive.substr(3), defines);
            }

            ConditionalFrame frame;
            frame.parent = currentState(stack);
            frame.prior = condition;
            frame.active = truthAnd(frame.parent, condition);
            stack.push_back(frame);
            continue;
        }
        if (directive.compare(0, 5, "#elif") == 0 && !stack.empty()) {
            ConditionalFrame& frame = stack.back();
            const Truth condition = evaluateCondition(directive.substr(5), defines);
            const Truth available = truthNot(frame.prior);
            frame.active = truthAnd(frame.parent, truthAnd(available, condition));
            frame.prior = truthOr(frame.prior, condition);
            continue;
        }
        if (directive == "#else" && !stack.empty()) {
            ConditionalFrame& frame = stack.back();
            frame.active = truthAnd(frame.parent, truthNot(frame.prior));
            frame.prior = Truth::True;
            continue;
        }
        if (directive == "#endif" && !stack.empty()) {
            stack.pop_back();
            continue;
        }

        if (currentState(stack) != Truth::False) {
            active += line;
            active += '\n';
        }
    }

    return active;
}

std::string compact(std::string value) {
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
        if (!std::isspace((unsigned char)c)) result.push_back(c);
    }
    return result;
}
}  // namespace

bool effectShaderUsesClipSpaceGeometry(const std::string& vertex_source, const char* shader_name) {
    const std::string active = activeShaderCode(vertex_source);
    const std::string compacted = compact(active);

    // Shaders often copy a_Position into a local before assigning gl_Position
    // (e.g. `vec3 position = a_Position; gl_Position = mul(vec4(position, 1.0), mvp);`).
    // Track those aliases so the assignment is still recognised as position-based.
    std::vector<std::string> position_aliases;
    size_t alias_search = 0;
    while ((alias_search = active.find("a_Position", alias_search)) != std::string::npos) {
        size_t equals = alias_search;
        while (equals > 0 && std::isspace((unsigned char)active[equals - 1])) --equals;
        if (equals == 0 || active[equals - 1] != '=') {
            alias_search += 10;
            continue;
        }
        size_t name_end = equals - 1;
        while (name_end > 0 && std::isspace((unsigned char)active[name_end - 1])) --name_end;
        size_t name_start = name_end;
        while (name_start > 0 && (std::isalnum((unsigned char)active[name_start - 1]) || active[name_start - 1] == '_'))
            --name_start;
        if (name_end > name_start) position_aliases.push_back(active.substr(name_start, name_end - name_start));
        alias_search += 10;
    }

    bool clip_space = false;
    bool layer_space = false;

    size_t search = 0;
    while ((search = compacted.find("gl_Position", search)) != std::string::npos) {
        const size_t semicolon = compacted.find(';', search);
        if (semicolon == std::string::npos) break;

        const std::string statement = compacted.substr(search, semicolon - search + 1);
        bool uses_position = statement.find("a_Position") != std::string::npos;
        if (!uses_position) {
            for (const std::string& alias : position_aliases) {
                if (statement.find(alias) != std::string::npos) {
                    uses_position = true;
                    break;
                }
            }
        }
        if (!uses_position) {
            search = semicolon + 1;
            continue;
        }

        if (statement.find("g_ModelViewProjectionMatrix") != std::string::npos) {
            layer_space = true;
        } else if (statement.find("gl_Position=vec4(") != std::string::npos ||
                   statement.find("gl_Position=float4(") != std::string::npos) {
            clip_space = true;
        }
        search = semicolon + 1;
    }

    if (clip_space != layer_space) return clip_space;

    effect_log.warn("ShaderPass %s: ambiguous vertex geometry contract; using layer-space quad",
                    shader_name ? shader_name : "(unknown)");
    return false;
}
