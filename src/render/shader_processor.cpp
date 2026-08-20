#include "shader_processor.h"

#include <string.h>

#include "../../libs/cJSON.h"

std::string ShaderSourceProcessor::processShaderSource(const std::string& source, bool is_vertex) {
    std::string result = source;

    // Fix: g_Screen is declared vec3 in some WPE shaders but our uniform block uses vec2.
    if (is_vertex) {
        size_t pos = result.find("uniform vec3 g_Screen;");
        if (pos != std::string::npos) {
            result.replace(pos, 22, "uniform vec2 g_Screen;");
        }
    }

    // 0. Remove existing #version if any
    size_t version_pos = result.find("#version");
    if (version_pos != std::string::npos) {
        size_t version_end = result.find('\n', version_pos);
        if (version_end != std::string::npos) {
            result.erase(version_pos, version_end - version_pos + 1);
        }
    }

    // 1. Resolve #include "common.h"
    size_t include_pos = result.find("#include \"common.h\"");
    if (include_pos != std::string::npos) {
        const char* common_h =
            "#define M_PI 3.14159265358979323846\n"
            "#define M_PI_2 1.57079632679\n"
            "#define M_2PI 6.28318530718\n"
            "vec2 rotateVec2(vec2 v, float a) {\n"
            "    float s = sin(a);\n"
            "    float c = cos(a);\n"
            "    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);\n"
            "}\n";
        result.replace(include_pos, 19, common_h);
    }

    // 2. Resolve #include "common_perspective.h"
    include_pos = result.find("#include \"common_perspective.h\"");
    if (include_pos != std::string::npos) {
        const char* common_perspective_h =
            "mat3 squareToQuad(vec2 p0, vec2 p1, vec2 p2, vec2 p3) {\n"
            "    float dx1 = p1.x - p2.x, dy1 = p1.y - p2.y;\n"
            "    float dx2 = p3.x - p2.x, dy2 = p3.y - p2.y;\n"
            "    float sx = p0.x - p1.x + p2.x - p3.x, sy = p0.y - p1.y + p2.y - p3.y;\n"
            "    float g = (sx * dy2 - dx2 * sy) / (dx1 * dy2 - dx2 * dy1);\n"
            "    float h = (dx1 * sy - sx * dy1) / (dx1 * dy2 - dx2 * dy1);\n"
            "    return mat3(p1.x - p0.x + g * p1.x, p1.y - p0.y + g * p1.y, g, p3.x - p0.x + h * p3.x, p3.y - p0.y + "
            "h * p3.y, h, p0.x, p0.y, 1.0);\n"
            "}\n";
        result.replace(include_pos, 31, common_perspective_h);
    }

    // GLSL 330 translation
    if (is_vertex) {
        size_t pos = 0;
        while ((pos = result.find("attribute ", pos)) != std::string::npos) {
            result.replace(pos, 9, "in ");
            pos += 3;
        }
        pos = 0;
        while ((pos = result.find("varying ", pos)) != std::string::npos) {
            result.replace(pos, 8, "out ");
            pos += 4;
        }
    } else {
        size_t pos = 0;
        while ((pos = result.find("varying ", pos)) != std::string::npos) {
            result.replace(pos, 8, "in ");
            pos += 3;
        }
        if (result.find("gl_FragColor") != std::string::npos) {
            result = "out vec4 frag_color;\n" + result;
            size_t frag_pos = 0;
            while ((frag_pos = result.find("gl_FragColor", frag_pos)) != std::string::npos) {
                result.replace(frag_pos, 12, "frag_color");
                frag_pos += 10;
            }
        }
    }

    return result;
}

std::string ShaderSourceProcessor::extractCombos(const char* fsSource) {
    std::string combo_defines = "";
    const char* p = fsSource;
    while (p && *p) {
        const char* line_end = strchr(p, '\n');
        if (!line_end) line_end = p + strlen(p);
        const char* combo_pos = strstr(p, "// [COMBO]");
        if (combo_pos && combo_pos < line_end) {
            const char* json_start = strchr(combo_pos, '{');
            if (json_start && json_start < line_end) {
                std::string json(json_start, line_end - json_start);
                cJSON* combo = cJSON_Parse(json.c_str());
                if (combo) {
                    cJSON* name = cJSON_GetObjectItemCaseSensitive(combo, "combo");
                    cJSON* default_val = cJSON_GetObjectItemCaseSensitive(combo, "default");
                    if (cJSON_IsString(name) && cJSON_IsNumber(default_val)) {
                        std::string define_name = name->valuestring;
                        if (combo_defines.find("#define " + define_name) == std::string::npos) {
                            combo_defines +=
                                "#define " + define_name + " " + std::to_string((int)default_val->valuedouble) + "\n";
                        }
                    }
                    cJSON_Delete(combo);
                }
            }
        }
        p = (*line_end) ? line_end + 1 : nullptr;
    }
    return combo_defines;
}

std::map<int, std::string> ShaderSourceProcessor::extractTextureLabels(const char* fsSource) {
    std::map<int, std::string> labels;
    const char* p = fsSource;
    while (p && *p) {
        const char* line_end = strchr(p, '\n');
        std::string line;
        if (line_end) {
            line = std::string(p, line_end - p);
        } else {
            line = std::string(p);
        }

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
                        if (i == 0 || label[i - 1] == ' ') label[i] = toupper(label[i]);
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
           "#define mul(v, m) ((m) * (v))\n"
           "#define texSample2D(s, uv) texture(s, uv)\n"
           "#define texture2D(s, uv) texture(s, uv)\n"
           "#define CAST2(x) vec2(x)\n"
           "#define CAST3(x) vec3(x)\n"
           "#define CAST4(x) vec4(x)\n"
           "#define CAST3X3(x) mat3(x)\n"
           "#define saturate(x) clamp(x, 0.0, 1.0)\n"
           "#define lerp mix\n"
           "#define lowp\n"
           "#define mediump\n"
           "#define highp\n"
           "uniform vec4 tint;\n";
}
