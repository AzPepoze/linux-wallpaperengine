#ifndef SHADER_PROCESSOR_H
#define SHADER_PROCESSOR_H

#include <map>
#include <string>

#include "core/interfaces.h"

class ShaderSourceProcessor {
   public:
    static std::string processShaderSource(const std::string& source, const char* sourcePath,
                                           const IAssetResolver& assets, bool isVertex);
    static std::string extractCombos(const char* fsSource);
    static std::map<int, std::string> extractTextureLabels(const char* fsSource);
    static std::string buildShaderPrefix();
};

#endif  // SHADER_PROCESSOR_H
