#ifndef SHADER_PROCESSOR_H
#define SHADER_PROCESSOR_H

#include <map>
#include <string>

#include "shared/core/interfaces.h"

class ShaderSourceProcessor {
   public:
    static std::string processShaderSource(const std::string& source, const char* sourcePath,
                                           const IAssetResolver& assets, bool isVertex);
    static void normalizePreprocessor(std::string& source);
    static void rewriteGlslCompatibility(std::string& source);
    static std::string extractCombos(const char* fsSource);
    static std::map<int, std::string> extractTextureLabels(const char* fsSource);
    // Bit n is set when g_Texture(n + 1) declares `"default":"util/white"` in its metadata comment.
    static unsigned int extractWhiteTextureDefaults(const char* fsSource);
    static std::string buildShaderPrefix();
};

#endif  // SHADER_PROCESSOR_H
