#include "shader_processor.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

namespace {
void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool isVectorReference(const std::string& expression, const std::string& name) {
    size_t pos = 0;
    while ((pos = expression.find(name, pos)) != std::string::npos) {
        const size_t end = pos + name.size();
        const bool left_boundary =
            pos == 0 || !(std::isalnum((unsigned char)expression[pos - 1]) || expression[pos - 1] == '_');
        const bool right_boundary =
            end == expression.size() || !(std::isalnum((unsigned char)expression[end]) || expression[end] == '_');
        if (!left_boundary || !right_boundary) {
            pos = end;
            continue;
        }

        size_t swizzle = end;
        while (swizzle < expression.size() && std::isspace((unsigned char)expression[swizzle])) ++swizzle;
        if (swizzle >= expression.size() || expression[swizzle] != '.') return true;
        ++swizzle;
        size_t swizzle_end = swizzle;
        while (swizzle_end < expression.size() && (expression[swizzle_end] == 'x' || expression[swizzle_end] == 'y' ||
                                                   expression[swizzle_end] == 'z' || expression[swizzle_end] == 'w')) {
            ++swizzle_end;
        }
        if (swizzle_end - swizzle != 1) return true;
        pos = swizzle_end;
    }
    return false;
}

void normalizeHlslVectorToScalarInitializers(std::string& source) {
    static const std::regex vector_uniform(R"(\buniform\s+(?:vec[234]|float[234])\s+([A-Za-z_][A-Za-z0-9_]*))");
    static const std::regex float_initializer(
        R"(\bfloat\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z0-9_\.\s\+\-\*/\(\)]+);)");

    std::set<std::string> vector_names;
    for (std::sregex_iterator it(source.begin(), source.end(), vector_uniform), end; it != end; ++it) {
        vector_names.insert((*it)[1].str());
    }

    std::string normalized;
    size_t copied = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), float_initializer), end; it != end; ++it) {
        const std::smatch& match = *it;
        const std::string expression = match[2].str();
        bool vector_expression = false;
        for (const std::string& vector_name : vector_names) {
            if (isVectorReference(expression, vector_name)) {
                vector_expression = true;
                break;
            }
        }
        if (!vector_expression) continue;

        const size_t match_pos = (size_t)match.position();
        normalized.append(source, copied, match_pos - copied);
        normalized += "float " + match[1].str() + " = (" + expression + ").x;";
        copied = match_pos + match.length();
    }
    if (copied == 0) return;
    normalized.append(source, copied, std::string::npos);
    source.swap(normalized);
}

bool readInclude(const std::string& include, const std::string& sourcePath, const IAssetResolver& assets,
                 std::string& resolvedPath, std::string& contents) {
    char path[1024] = {};
    const size_t slash = sourcePath.rfind('/');
    if (slash != std::string::npos) {
        const std::string localPath = sourcePath.substr(0, slash + 1) + include;
        if (access(localPath.c_str(), R_OK) == 0) {
            strncpy(path, localPath.c_str(), sizeof(path) - 1);
        }
    }
    if (path[0] == '\0' && !assets.resolvePath(include.c_str(), path, sizeof(path))) {
        const std::string stockPath = "shaders/" + include;
        if (!assets.resolvePath(stockPath.c_str(), path, sizeof(path))) return false;
    }

    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return false;
    }
    contents.resize((size_t)size);
    if (size > 0) fread(contents.data(), 1, (size_t)size, file);
    fclose(file);
    resolvedPath = path;
    return true;
}

std::string expandIncludes(const std::string& source, const std::string& sourcePath, const IAssetResolver& assets,
                           std::set<std::string>& active, std::set<std::string>& expanded) {
    std::string result;
    size_t start = 0;
    while (start < source.size()) {
        const size_t end = source.find('\n', start);
        const size_t length = (end == std::string::npos ? source.size() : end) - start;
        const std::string line = source.substr(start, length);
        const size_t directive = line.find_first_not_of(" \t");
        const bool includeDirective = directive != std::string::npos && line.compare(directive, 8, "#include") == 0;
        const size_t quote1 = includeDirective ? line.find('"', directive + 8) : std::string::npos;
        const size_t quote2 = quote1 == std::string::npos ? std::string::npos : line.find('"', quote1 + 1);
        if (quote1 != std::string::npos && quote2 != std::string::npos) {
            const std::string include = line.substr(quote1 + 1, quote2 - quote1 - 1);
            std::string includePath;
            std::string includeSource;
            if (readInclude(include, sourcePath, assets, includePath, includeSource)) {
                if (active.count(includePath)) {
                } else if (!expanded.count(includePath)) {
                    active.insert(includePath);
                    result += expandIncludes(includeSource, includePath, assets, active, expanded);
                    active.erase(includePath);
                    expanded.insert(includePath);
                }
            } else {
                result += line;
                result += '\n';
            }
        } else {
            result += line;
            result += '\n';
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

bool isIdentifierChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

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

// GLSL evaluates identifiers that were never defined as zero. Slang only warns
// for those, but it rejects member access on them (e.g. `#if uniform.x < ...`),
// so rewrite undefined identifiers (and their member chains) to literal zero.
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

std::string narrowStatementVectors(const std::string& statement, const std::map<std::string, int>& widths) {
    static const std::regex declaration(R"(\b(vec|float|ivec|int)([234])\s+([A-Za-z_][A-Za-z0-9_]*)\s*=)");

    const size_t first = statement.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && statement[first] == '#') return statement;

    std::smatch declared;
    if (!std::regex_search(statement, declared, declaration)) return statement;
    const int target_width = declared[2].str()[0] - '0';
    if (target_width != 2 && target_width != 3) return statement;

    const size_t target_position = (size_t)declared.position(3);
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
}  // namespace

std::string ShaderSourceProcessor::processShaderSource(const std::string& source, const char* source_path,
                                                       const IAssetResolver& assets, bool is_vertex) {
    std::set<std::string> active;
    std::set<std::string> expanded;
    std::string result = expandIncludes(source, source_path ? source_path : "", assets, active, expanded);

    if (is_vertex) replaceAll(result, "uniform vec3 g_Screen;", "uniform vec2 g_Screen;");

    size_t version_pos = 0;
    while ((version_pos = result.find("#version", version_pos)) != std::string::npos) {
        size_t version_end = result.find('\n', version_pos);
        result.erase(version_pos,
                     version_end == std::string::npos ? result.size() - version_pos : version_end - version_pos + 1);
    }

    if (is_vertex) {
        replaceAll(result, "attribute ", "in ");
        replaceAll(result, "varying ", "out ");
    } else {
        replaceAll(result, "varying ", "in ");
        if (result.find("gl_FragColor") != std::string::npos || result.find("gl_FragData[0]") != std::string::npos) {
            result = "out vec4 frag_color;\n" + result;
            replaceAll(result, "gl_FragColor", "frag_color");
            replaceAll(result, "gl_FragData[0]", "frag_color");
        }
    }

    normalizeHlslVectorToScalarInitializers(result);
    rewriteGlslCompatibility(result);

    return result;
}

void ShaderSourceProcessor::rewriteGlslCompatibility(std::string& source) {
    rewriteSamplerArguments(source);
    rewriteFunctionArguments(source);
    rewriteScalarFromVectorCall(source);
    rewriteOutOfRangeSwizzles(source);
    rewriteNarrowingConversions(source);
    rewriteBinaryVectorMismatch(source);
    rewriteMixVectorWidths(source);
}

void ShaderSourceProcessor::normalizePreprocessor(std::string& source) {
    source = fixPreprocessorDirectives(source);
}

std::string ShaderSourceProcessor::extractCombos(const char* fsSource) {
    std::string combo_defines;
    const char* p = fsSource;
    while (p && *p) {
        const char* line_end = strchr(p, '\n');
        std::string line = line_end ? std::string(p, line_end - p) : std::string(p);

        size_t combo_pos = line.find("// [COMBO]");
        if (combo_pos != std::string::npos) {
            size_t combo_key = line.find("\"combo\"", combo_pos);
            size_t default_key = line.find("\"default\"", combo_pos);
            if (combo_key != std::string::npos && default_key != std::string::npos) {
                size_t colon_combo = line.find(':', combo_key);
                size_t q1 = (colon_combo != std::string::npos) ? line.find('\"', colon_combo) : std::string::npos;
                size_t q2 = (q1 != std::string::npos) ? line.find('\"', q1 + 1) : std::string::npos;
                size_t colon_def = line.find(':', default_key);

                if (q1 != std::string::npos && q2 != std::string::npos && colon_def != std::string::npos) {
                    std::string define_name = line.substr(q1 + 1, q2 - q1 - 1);
                    int default_val = atoi(line.c_str() + colon_def + 1);
                    if (!define_name.empty() &&
                        combo_defines.find("#define " + define_name + " ") == std::string::npos) {
                        combo_defines += "#define " + define_name + " " + std::to_string(default_val) + "\n";
                    }
                }
            }
        }
        p = line_end ? line_end + 1 : nullptr;
    }
    return combo_defines;
}

std::map<int, std::string> ShaderSourceProcessor::extractTextureLabels(const char* fsSource) {
    std::map<int, std::string> labels;
    const char* p = fsSource;
    while (p && *p) {
        const char* line_end = strchr(p, '\n');
        std::string line = line_end ? std::string(p, line_end - p) : std::string(p);

        size_t tex_pos = line.find("g_Texture");
        size_t comment_pos = line.find("//");

        if (tex_pos != std::string::npos && comment_pos != std::string::npos && comment_pos > tex_pos) {
            int slot = atoi(line.c_str() + tex_pos + 9);
            std::string label;

            size_t json_start = line.find('{', comment_pos);
            size_t label_key = line.find("\"label\"", comment_pos);
            if (json_start != std::string::npos && label_key != std::string::npos) {
                size_t colon = line.find(':', label_key);
                size_t quote1 = line.find('\"', colon);
                size_t quote2 = line.find('\"', quote1 + 1);
                if (quote1 != std::string::npos && quote2 != std::string::npos) {
                    label = line.substr(quote1 + 1, quote2 - quote1 - 1);
                }
            } else {
                size_t b_open = line.find('[', comment_pos);
                size_t b_close = line.find(']', b_open);
                if (b_open != std::string::npos && b_close != std::string::npos) {
                    label = line.substr(b_open + 1, b_close - b_open - 1);
                }
            }

            if (!label.empty()) {
                if (label == "ui_editor_properties_water_normal")
                    label = "Water Normal";
                else if (label == "ui_editor_properties_opacity_mask")
                    label = "Opacity Mask";
                else if (label == "ui_editor_properties_specular")
                    label = "Specular";
                else if (label.find("ui_editor_properties_") == 0) {
                    label = label.substr(21);
                    for (size_t i = 0; i < label.length(); i++) {
                        if (label[i] == '_') label[i] = ' ';
                        if (i == 0 || label[i - 1] == ' ') label[i] = (char)toupper((unsigned char)label[i]);
                    }
                }
                labels[slot] = label;
            }
        }
        p = line_end ? line_end + 1 : nullptr;
    }
    return labels;
}

std::string ShaderSourceProcessor::buildShaderPrefix() {
    return "#version 330\n"
           "#define HLSL 0\n"
           "#define GLSL 1\n"
           "#define float2 vec2\n"
           "#define float3 vec3\n"
           "#define float4 vec4\n"
           "#define int2 ivec2\n"
           "#define int3 ivec3\n"
           "#define int4 ivec4\n"
           "#define uint2 uvec2\n"
           "#define uint3 uvec3\n"
           "#define uint4 uvec4\n"
           "#define bool2 bvec2\n"
           "#define bool3 bvec3\n"
           "#define bool4 bvec4\n"
           "#define float2x2 mat2\n"
           "#define float3x3 mat3\n"
           "#define float4x4 mat4\n"
           "#define mul(v, m) ((m) * (v))\n"
           "#define texSample2D(s, uv) texture(s, uv)\n"
           "#define texSample2DLod(s, uv, lod) textureLod(s, uv, lod)\n"
           "#define texSample2DGrad(s, uv, dx, dy) textureGrad(s, uv, dx, dy)\n"
           "#define texture2D(s, uv) texture(s, uv)\n"
           "#define texture2DLod(s, uv, lod) textureLod(s, uv, lod)\n"
           "#define CAST2(x) vec2(x)\n"
           "#define CAST3(x) vec3(x)\n"
           "#define CAST4(x) vec4(x)\n"
           "#define CAST3X3(x) mat3(x)\n"
           "#define saturate(x) clamp(x, 0.0, 1.0)\n"
           "#define lerp mix\n"
           "#define frac fract\n"
           "#define ddx dFdx\n"
           "#define ddy dFdy\n"
           "#define atan2(y, x) atan(y, x)\n"
           "#define lowp\n"
           "#define mediump\n"
           "#define highp\n"
           "float dot(vec4 a, vec3 b) { return dot(a.xyz, b); }\n"
           "float dot(vec3 a, vec4 b) { return dot(a, b.xyz); }\n"
           "uniform vec4 tint;\n";
}
