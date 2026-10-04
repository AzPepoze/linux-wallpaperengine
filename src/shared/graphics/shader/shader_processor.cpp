#include "shader_processor.h"

#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>

#include "shader_processor_internal.h"
#include "shared/core/vfs.h"

using namespace shader_processor_internal;

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
        R"(\bfloat\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z0-9_\.\s\+\-\*/\(\),]+);)");

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
        if (vfs::exists(localPath.c_str())) {
            strncpy(path, localPath.c_str(), sizeof(path) - 1);
        }
    }
    if (path[0] == '\0' && !assets.resolvePath(include.c_str(), path, sizeof(path))) {
        const std::string stockPath = "shaders/" + include;
        if (!assets.resolvePath(stockPath.c_str(), path, sizeof(path))) return false;
    }

    FILE* file = vfs::open(path);
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
    rewriteScalarVectorBroadcast(source);
    rewriteScalarFromVectorCall(source);
    rewriteOutOfRangeSwizzles(source);
    rewriteNarrowingConversions(source);
    rewriteBinaryVectorMismatch(source);
    rewriteMixVectorWidths(source);
}

void ShaderSourceProcessor::normalizePreprocessor(std::string& source) {
    source = fixPreprocessorDirectives(source);
}
