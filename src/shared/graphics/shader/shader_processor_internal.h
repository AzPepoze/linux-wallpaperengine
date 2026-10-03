#ifndef SHADER_PROCESSOR_INTERNAL_H
#define SHADER_PROCESSOR_INTERNAL_H

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
}  // namespace shader_processor_internal

#endif  // SHADER_PROCESSOR_INTERNAL_H
