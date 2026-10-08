#ifndef SHADER_PROCESSOR_INTERNAL_H
#define SHADER_PROCESSOR_INTERNAL_H

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <map>
#include <string>

namespace shader_processor_internal {
std::string fixPreprocessorDirectives(const std::string& source);
bool isIdentifierChar(char c);
std::map<std::string, int> collectVectorWidths(const std::string& source);
std::string swizzleForWidth(int width);
void rewriteSamplerArguments(std::string& source);
void rewriteFunctionArguments(std::string& source);
void rewriteMixVectorWidths(std::string& source);
void rewriteNarrowingConversions(std::string& source);
void rewriteOutOfRangeSwizzles(std::string& source);
void rewriteScalarFromVectorCall(std::string& source);
void rewriteBinaryVectorMismatch(std::string& source);
void rewriteScalarVectorBroadcast(std::string& source);
}  // namespace shader_processor_internal

namespace shader_processor_internal {
inline bool isWordChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

inline bool isVectorTypeWord(const std::string& word) {
    static const char* const kTypes[] = {"vec2",  "vec3",  "vec4",  "float2", "float3", "float4",
                                         "ivec2", "ivec3", "ivec4", "int2",   "int3",   "int4"};
    return std::any_of(std::begin(kTypes), std::end(kTypes), [&](const char* type) { return word == type; });
}

// Calls `visit` for each `type name` followed by a terminator; stops once visit returns true.
template <class IsType, class Visit>
void forEachDeclaration(const std::string& source, const char* terminators, IsType&& isType, Visit&& visit) {
    const size_t size = source.size();
    size_t i = 0;
    while (i < size) {
        if (!isWordChar(source[i])) {
            ++i;
            continue;
        }
        const size_t word_start = i;
        while (i < size && isWordChar(source[i])) ++i;
        const std::string word = source.substr(word_start, i - word_start);
        if (!isType(word)) continue;

        size_t cursor = i;
        if (cursor >= size || !std::isspace((unsigned char)source[cursor])) continue;
        while (cursor < size && std::isspace((unsigned char)source[cursor])) ++cursor;
        const size_t name_start = cursor;
        if (cursor >= size || !(std::isalpha((unsigned char)source[cursor]) || source[cursor] == '_')) continue;
        while (cursor < size && isWordChar(source[cursor])) ++cursor;
        const size_t name_end = cursor;
        while (cursor < size && std::isspace((unsigned char)source[cursor])) ++cursor;
        if (cursor >= size || source[cursor] == '\0' || std::strchr(terminators, source[cursor]) == nullptr) continue;

        if (visit(word, source.substr(name_start, name_end - name_start), name_start)) return;
        i = cursor + 1;
    }
}
}  // namespace shader_processor_internal

#endif  // SHADER_PROCESSOR_INTERNAL_H
