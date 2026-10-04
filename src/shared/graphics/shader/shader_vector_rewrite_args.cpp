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

namespace {

int vectorWidthFromTypeToken(const std::string& token) {
    static const char* const kVectorBases[] = {"vec", "bvec", "ivec", "uvec", "float", "int"};
    static const char* const kScalarTypes[] = {"float", "double", "int", "uint", "bool", "mat2", "mat3", "mat4"};
    if (token.size() >= 2 && token.back() >= '2' && token.back() <= '4') {
        const std::string base = token.substr(0, token.size() - 1);
        for (const char* candidate : kVectorBases) {
            if (base == candidate) return token.back() - '0';
        }
    }
    for (const char* scalar : kScalarTypes) {
        if (token == scalar) return 0;
    }
    return -1;
}

int classifyParameter(const std::string& parameter) {
    static const char* const kQualifiers[] = {"in",      "out",   "inout", "const",         "lowp",
                                              "mediump", "highp", "flat",  "noperspective", "smooth"};
    std::istringstream stream(parameter);
    std::string token;
    while (stream >> token) {
        if (std::any_of(std::begin(kQualifiers), std::end(kQualifiers),
                        [&](const char* qualifier) { return token == qualifier; }))
            continue;
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
