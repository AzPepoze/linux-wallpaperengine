#ifndef SHADER_VECTOR_REWRITE_INTERNAL_H
#define SHADER_VECTOR_REWRITE_INTERNAL_H

#include <map>
#include <string>
#include <vector>

namespace shader_processor_internal {

// Scanning helpers shared by the vector-width rewrite passes; defined in shader_vector_rewrite.cpp.
size_t identifierLength(const std::string& text);
bool isIdentifier(const std::string& text);
bool isDigit(char c);
bool isVectorIdentifier(const std::map<std::string, int>& widths, const std::string& token, int& width);
size_t findMatchingParen(const std::string& text, size_t open);
void splitTopLevelArguments(const std::string& text, std::vector<std::string>& out);
std::string trimSpaces(const std::string& text);
std::string narrowBareVec4InExpression(const std::string& expression, const std::map<std::string, int>& widths);

}  // namespace shader_processor_internal

#endif  // SHADER_VECTOR_REWRITE_INTERNAL_H
