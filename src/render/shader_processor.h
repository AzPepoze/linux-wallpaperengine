#ifndef SHADER_PROCESSOR_H
#define SHADER_PROCESSOR_H

#include <string>
#include <map>

class ShaderSourceProcessor {
public:
    static std::string processShaderSource(const std::string& source, bool isVertex);
    static std::string extractCombos(const char* fsSource);
    static std::map<int, std::string> extractTextureLabels(const char* fsSource);
    static std::string buildShaderPrefix();
};

#endif // SHADER_PROCESSOR_H
