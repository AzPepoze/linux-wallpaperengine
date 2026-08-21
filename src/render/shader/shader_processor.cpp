#include "shader_processor.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

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

const char* legacyHeader(const std::string& include) {
    if (include == "common.h") {
        return "#define M_PI 3.14159265358979323846\n"
               "#define M_PI_2 1.57079632679\n"
               "#define M_2PI 6.28318530718\n"
               "vec2 rotateVec2(vec2 v, float a) {\n"
               "    float s = sin(a);\n"
               "    float c = cos(a);\n"
               "    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);\n"
               "}\n";
    }
    if (include == "common_perspective.h") {
        return "mat3 squareToQuad(vec2 p0, vec2 p1, vec2 p2, vec2 p3) {\n"
               "    float dx1 = p1.x - p2.x, dy1 = p1.y - p2.y;\n"
               "    float dx2 = p3.x - p2.x, dy2 = p3.y - p2.y;\n"
               "    float sx = p0.x - p1.x + p2.x - p3.x, sy = p0.y - p1.y + p2.y - p3.y;\n"
               "    float g = (sx * dy2 - dx2 * sy) / (dx1 * dy2 - dx2 * dy1);\n"
               "    float h = (dx1 * sy - sx * dy1) / (dx1 * dy2 - dx2 * dy1);\n"
               "    return mat3(p1.x - p0.x + g * p1.x, p1.y - p0.y + g * p1.y, g, p3.x - p0.x + h * p3.x, p3.y - p0.y "
               "+ "
               "h * p3.y, h, p0.x, p0.y, 1.0);\n"
               "}\n";
    }
    return nullptr;
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
            } else if (const char* compatibility = legacyHeader(include)) {
                result += compatibility;
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

    return result;
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
