#include "effect.h"

#include "../core/context.h"
#include "../core/logger.h"
#include "../core/utils.h"
#include "../ui/debugger.h"
#include "imgui.h"

ShaderPass::ShaderPass(cJSON* config, cJSON* instance_config) {
    cJSON* base_config = config;
    char* material_json_str = nullptr;

    // If config has a material reference, load that instead
    cJSON* mat_ref = cJSON_GetObjectItemCaseSensitive(config, "material");
    if (cJSON_IsString(mat_ref)) {
        char abs_mat[1024];
        if (state.asset_mgr.resolvePath(mat_ref->valuestring, abs_mat, sizeof(abs_mat))) {
            material_json_str = read_file_to_string(abs_mat);
            if (material_json_str) {
                cJSON* mat_json = cJSON_Parse(material_json_str);
                if (mat_json) {
                    // Check if material has passes, if so use the first one as base
                    cJSON* passes = cJSON_GetObjectItemCaseSensitive(mat_json, "passes");
                    if (cJSON_IsArray(passes) && cJSON_GetArraySize(passes) > 0) {
                        base_config = cJSON_Duplicate(cJSON_GetArrayItem(passes, 0), 1);
                        cJSON_Delete(mat_json);
                    } else {
                        base_config = mat_json;
                    }
                }
            }
        }
    }

    constant_values = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(base_config, "constantshadervalues"), 1);
    cJSON* shader_node = cJSON_GetObjectItemCaseSensitive(base_config, "shader");
    if (cJSON_IsString(shader_node)) {
        shader_name = shader_node->valuestring;
    }

    // Load base textures from the resolved base_config
    cJSON* textures_node = cJSON_GetObjectItemCaseSensitive(base_config, "textures");
    if (cJSON_IsArray(textures_node)) {
        cJSON* tex_node;
        cJSON_ArrayForEach(tex_node, textures_node) {
            if (cJSON_IsString(tex_node)) {
                std::string path;
                sg_image img = state.asset_mgr.resolveTexture(tex_node->valuestring, &path);
                textures.push_back(img);
                texture_paths.push_back(path);
                texture_masks.push_back(true);
                effect_log.info("ShaderPass %s: Loaded texture %d: %s (id: %d)", shader_name.c_str(),
                                (int)textures.size() - 1, path.c_str(), img.id);
            } else {
                // Auto-resolve depth map if it's the second slot and previous was a .tex
                if (textures.size() == 1 && !texture_paths[0].empty() && strstr(texture_paths[0].c_str(), ".tex")) {
                    std::string path;
                    sg_image img = state.asset_mgr.resolveTexture(texture_paths[0].c_str(), &path, 1);
                    if (img.id != SG_INVALID_ID) {
                        textures.push_back(img);
                        texture_paths.push_back(path + "#1");
                        texture_masks.push_back(true);
                        effect_log.info("ShaderPass %s: Auto-resolved texture 1 from %s (index 1, id: %d)",
                                        shader_name.c_str(), texture_paths[0].c_str(), img.id);
                        continue;
                    }
                }
                textures.push_back((sg_image){SG_INVALID_ID});
                texture_paths.push_back("");
                texture_masks.push_back(true);
            }
        }
    }

    // Clean up temporary material JSON if we loaded one
    if (material_json_str) {
        cJSON_Delete(base_config);
        free(material_json_str);
    }

    // Apply instance overrides from scene.json
    if (instance_config) {
        cJSON* inst_textures = cJSON_GetObjectItemCaseSensitive(instance_config, "textures");
        if (cJSON_IsArray(inst_textures)) {
            for (int i = 0; i < cJSON_GetArraySize(inst_textures); i++) {
                cJSON* tex_node = cJSON_GetArrayItem(inst_textures, i);
                if (cJSON_IsString(tex_node)) {
                    std::string path;
                    sg_image img = state.asset_mgr.resolveTexture(tex_node->valuestring, &path);
                    if (img.id != SG_INVALID_ID) {
                        if (i < (int)textures.size()) {
                            // Replace existing slot
                            if (textures[i].id != SG_INVALID_ID) sg_destroy_image(textures[i]);
                            textures[i] = img;
                            texture_paths[i] = path;
                            effect_log.info("ShaderPass %s: Overrode texture %d: %s (id: %d)", shader_name.c_str(), i,
                                            path.c_str(), img.id);
                        } else {
                            // Expand and add new slot
                            while ((int)textures.size() < i) {
                                textures.push_back((sg_image){SG_INVALID_ID});
                                texture_paths.push_back("");
                                texture_masks.push_back(true);
                            }
                            textures.push_back(img);
                            texture_paths.push_back(path);
                            texture_masks.push_back(true);
                            effect_log.info("ShaderPass %s: Added texture %d: %s (id: %d)", shader_name.c_str(), i,
                                            path.c_str(), img.id);
                        }
                    }
                }
            }
        }
        // Merge constants
        cJSON* inst_const = cJSON_GetObjectItemCaseSensitive(instance_config, "constantshadervalues");
        if (cJSON_IsObject(inst_const)) {
            if (!constant_values)
                constant_values = cJSON_Duplicate(inst_const, 1);
            else {
                cJSON* item;
                cJSON_ArrayForEach(item, inst_const) {
                    cJSON* existing = cJSON_GetObjectItemCaseSensitive(constant_values, item->string);
                    if (existing) {
                        cJSON_DeleteItemFromObjectCaseSensitive(constant_values, item->string);
                    }
                    cJSON_AddItemToObject(constant_values, item->string, cJSON_Duplicate(item, 1));
                }
            }
        }
    }

    // Parse uniforms from constants
    if (constant_values) {
        cJSON* item;
        cJSON_ArrayForEach(item, constant_values) {
            std::vector<float> vals;
            if (cJSON_IsNumber(item)) {
                vals.push_back((float)item->valuedouble);
            } else if (cJSON_IsString(item)) {
                float f1, f2, f3, f4;
                int count = sscanf(item->valuestring, "%f %f %f %f", &f1, &f2, &f3, &f4);
                if (count >= 1) vals.push_back(f1);
                if (count >= 2) vals.push_back(f2);
                if (count >= 3) vals.push_back(f3);
                if (count >= 4) vals.push_back(f4);
            }
            if (!vals.empty()) {
                // Map common Wallpaper Engine uniform names to g_ prefixed ones
                std::string name = item->string;
                if (name == "scale")
                    name = "g_Scale";
                else if (name == "sens")
                    name = "g_Sensitivity";
                else if (name == "center")
                    name = "g_Center";
                else if (name == "speed")
                    name = "g_Speed";
                else if (name == "strength")
                    name = "g_Strength";
                else if (name == "direction")
                    name = "g_Direction";
                else if (name == "perspective")
                    name = "g_Perspective";
                else if (name.find("g_") != 0) {
                    std::string mapped = "g_";
                    mapped += (char)toupper(name[0]);
                    mapped += name.substr(1);
                    name = mapped;
                }

                uniforms[name] = vals;
            }
        }
    }
}

ShaderPass::~ShaderPass() {
    if (constant_values) cJSON_Delete(constant_values);
    if (pipeline.id != SG_INVALID_ID) sg_destroy_pipeline(pipeline);
    if (shader.id != SG_INVALID_ID) sg_destroy_shader(shader);
}

static std::string process_shader_source(const std::string& source, bool is_vertex) {
    std::string result = source;

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

    // 3. Translate attribute/varying
    if (is_vertex) {
        // attribute -> in
        size_t pos = 0;
        while ((pos = result.find("attribute ", pos)) != std::string::npos) {
            result.replace(pos, 9, "in ");
            pos += 3;
        }
        // varying -> out
        pos = 0;
        while ((pos = result.find("varying ", pos)) != std::string::npos) {
            result.replace(pos, 8, "out ");
            pos += 4;
        }
    } else {
        // varying -> in
        size_t pos = 0;
        while ((pos = result.find("varying ", pos)) != std::string::npos) {
            result.replace(pos, 8, "in ");
            pos += 3;
        }
        // gl_FragColor -> frag_color
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

void ShaderPass::init() {
    if (shader_name.empty()) {
        effect_log.warn("ShaderPass init: shader_name is empty!");
        return;
    }

    effect_log.info("Initializing ShaderPass: %s", shader_name.c_str());

    char vert_path[256], frag_path[256];
    if (shader_name.find("shaders/") == 0) {
        snprintf(vert_path, sizeof(vert_path), "%s.vert", shader_name.c_str());
        snprintf(frag_path, sizeof(frag_path), "%s.frag", shader_name.c_str());
    } else {
        snprintf(vert_path, sizeof(vert_path), "shaders/%s.vert", shader_name.c_str());
        snprintf(frag_path, sizeof(frag_path), "shaders/%s.frag", shader_name.c_str());
    }

    char abs_vert[1024], abs_frag[1024];
    char* vs_src = nullptr;
    char* fs_src = nullptr;

    if (state.asset_mgr.resolvePath(vert_path, abs_vert, sizeof(abs_vert))) {
        vs_src = read_file_to_string(abs_vert);
    } else {
        // Try extracted path
        char extracted_path[512];
        snprintf(extracted_path, sizeof(extracted_path), "extracted/%s", vert_path);
        if (state.asset_mgr.resolvePath(extracted_path, abs_vert, sizeof(abs_vert))) {
            vs_src = read_file_to_string(abs_vert);
        }
    }

    if (state.asset_mgr.resolvePath(frag_path, abs_frag, sizeof(abs_frag))) {
        fs_src = read_file_to_string(abs_frag);
        effect_log.debug("Loaded frag shader for labels: %s", abs_frag);
    } else {
        // Try extracted path
        char extracted_path[512];
        snprintf(extracted_path, sizeof(extracted_path), "extracted/%s", frag_path);
        if (state.asset_mgr.resolvePath(extracted_path, abs_frag, sizeof(abs_frag))) {
            fs_src = read_file_to_string(abs_frag);
            effect_log.debug("Loaded frag shader for labels: %s", abs_frag);
        }
    }

    if (!vs_src || !fs_src) {
        effect_log.error("Failed to load shader sources for %s (vert: %s, frag: %s)", shader_name.c_str(), vert_path,
                         frag_path);
        if (vs_src) free(vs_src);
        if (fs_src) free(fs_src);
        return;
    }

    std::string combo_defines = "";
    if (constant_values) {
        cJSON* item;
        cJSON_ArrayForEach(item, constant_values) {
            if (cJSON_IsNumber(item)) {
                std::string name = item->string;
                // Uppercase for combos
                for (auto& c : name) c = toupper(c);
                combo_defines += "#define " + name + " " + std::to_string((int)item->valuedouble) + "\n";
            }
        }
    }
    // Also add defines for textures being present
    for (int i = 0; i < (int)textures.size(); i++) {
        if (textures[i].id != SG_INVALID_ID && texture_masks[i]) {
            if (i == 1) combo_defines += "#define MASK 1\n";
            if (i == 2) combo_defines += "#define TIMEOFFSET 1\n";
        }
    }

    const char* shader_prefix =
        "#version 330\n"
        "#define mul(v, m) (m * v)\n"
        "#define texSample2D(s, uv) texture(s, uv)\n"
        "#define texture2D texture\n"
        "#define CAST2(x) vec2(x)\n"
        "#define CAST3(x) vec3(x)\n"
        "#define CAST4(x) vec4(x)\n"
        "#define CAST3X3(x) mat3(x)\n"
        "#define saturate(x) clamp(x, 0.0, 1.0)\n"
        "#define lerp mix\n"
        "precision mediump float;\n"
        "uniform vec4 tint;\n";

    std::string full_vs = shader_prefix + combo_defines + process_shader_source(vs_src, true);
    std::string full_fs = shader_prefix + combo_defines + process_shader_source(fs_src, false);

    // Extract texture labels from comments in fragment shader
    // Supports:
    // 1. // [Label] g_TextureX
    // 2. uniform sampler2D g_TextureX; // {"label":"..."}
    {
        const char* p = fs_src;
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

                // Try JSON format: {"label":"..."}
                size_t json_start = line.find('{', comment_pos);
                size_t label_key = line.find("\"label\"", comment_pos);
                if (json_start != std::string::npos && label_key != std::string::npos) {
                    size_t colon = line.find(':', label_key);
                    size_t quote1 = line.find('\"', colon);
                    size_t quote2 = line.find('\"', quote1 + 1);
                    if (quote1 != std::string::npos && quote2 != std::string::npos) {
                        label = line.substr(quote1 + 1, quote2 - quote1 - 1);
                    }
                }
                // Try bracket format: [Label]
                else {
                    size_t b_open = line.find('[', comment_pos);
                    size_t b_close = line.find(']', b_open);
                    if (b_open != std::string::npos && b_close != std::string::npos) {
                        label = line.substr(b_open + 1, b_close - b_open - 1);
                    }
                }

                if (!label.empty()) {
                    // Map common localization keys
                    if (label == "ui_editor_properties_water_normal")
                        label = "Water Normal";
                    else if (label == "ui_editor_properties_opacity_mask")
                        label = "Opacity Mask";
                    else if (label == "ui_editor_properties_specular")
                        label = "Specular";
                    else if (label.find("ui_editor_properties_") == 0) {
                        // Clean up other keys: remove prefix and replace underscores
                        label = label.substr(21);
                        for (size_t i = 0; i < label.length(); i++) {
                            if (label[i] == '_') label[i] = ' ';
                            if (i == 0 || label[i - 1] == ' ') label[i] = toupper(label[i]);
                        }
                    }

                    texture_labels[slot] = label;
                    effect_log.info("Found label for g_Texture%d: %s", slot, label.c_str());
                }
            }
            p = line_end ? line_end + 1 : nullptr;
        }
    }
    sg_shader_desc shd_desc = {};
    shd_desc.attrs[0].glsl_name = "a_Position";
    shd_desc.attrs[1].glsl_name = "a_TexCoord";
    shd_desc.vertex_func.source = full_vs.c_str();
    shd_desc.fragment_func.source = full_fs.c_str();

    // Slot 0: Built-in Uniforms (WPE style)
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(mat4x4);
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "g_ModelViewProjectionMatrix";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[1].size = sizeof(builtin_uniforms_t) - sizeof(mat4x4);  // -mvp
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "g_Texture0Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "g_Texture1Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[2].glsl_name = "g_Texture2Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[3].glsl_name = "g_Texture3Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[4].glsl_name = "g_Texture4Resolution";
    shd_desc.uniform_blocks[1].glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[5].glsl_name = "g_ParallaxPosition";
    shd_desc.uniform_blocks[1].glsl_uniforms[5].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[1].glsl_uniforms[6].glsl_name = "g_Time";
    shd_desc.uniform_blocks[1].glsl_uniforms[6].type = SG_UNIFORMTYPE_FLOAT;
    shd_desc.uniform_blocks[1].glsl_uniforms[7].glsl_name = "g_Screen";
    shd_desc.uniform_blocks[1].glsl_uniforms[7].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.uniform_blocks[1].glsl_uniforms[8].glsl_name = "g_EffectTextureProjectionMatrix";
    shd_desc.uniform_blocks[1].glsl_uniforms[8].type = SG_UNIFORMTYPE_MAT4;
    shd_desc.uniform_blocks[1].glsl_uniforms[9].glsl_name = "g_EffectTextureProjectionMatrixInverse";
    shd_desc.uniform_blocks[1].glsl_uniforms[9].type = SG_UNIFORMTYPE_MAT4;

    // Slot 2: Fragment Tint
    shd_desc.uniform_blocks[2].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[2].size = sizeof(float) * 4;
    shd_desc.uniform_blocks[2].glsl_uniforms[0].glsl_name = "tint";
    shd_desc.uniform_blocks[2].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;

    // Custom uniforms (up to 8 slots total in Sokol)
    int u_idx = 3;
    for (auto const& [name, vals] : uniforms) {
        if (u_idx >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) break;

        sg_uniform_type type = SG_UNIFORMTYPE_FLOAT4;
        if (full_vs.find("uniform float " + name) != std::string::npos ||
            full_fs.find("uniform float " + name) != std::string::npos) {
            type = SG_UNIFORMTYPE_FLOAT;
        } else if (full_vs.find("uniform vec2 " + name) != std::string::npos ||
                   full_fs.find("uniform vec2 " + name) != std::string::npos) {
            type = SG_UNIFORMTYPE_FLOAT2;
        } else if (full_vs.find("uniform vec3 " + name) != std::string::npos ||
                   full_fs.find("uniform vec3 " + name) != std::string::npos) {
            type = SG_UNIFORMTYPE_FLOAT3;
        }

        bool in_vs = full_vs.find(name) != std::string::npos;
        bool in_fs = full_fs.find(name) != std::string::npos;

        if (in_vs)
            shd_desc.uniform_blocks[u_idx].stage = SG_SHADERSTAGE_VERTEX;
        else
            shd_desc.uniform_blocks[u_idx].stage = SG_SHADERSTAGE_FRAGMENT;

        shd_desc.uniform_blocks[u_idx].size = sizeof(float) * 4;
        shd_desc.uniform_blocks[u_idx].glsl_uniforms[0].glsl_name = name.c_str();
        shd_desc.uniform_blocks[u_idx].glsl_uniforms[0].type = type;
        u_idx++;
    }

    // Samplers
    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].glsl_name = "g_Texture0";
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;

    for (int i = 0; i < (int)textures.size() && i < 11; i++) {
        char tex_name[32];
        snprintf(tex_name, sizeof(tex_name), "g_Texture%d", i + 1);
        shd_desc.views[i + 1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.views[i + 1].texture.image_type = SG_IMAGETYPE_2D;
        shd_desc.texture_sampler_pairs[i + 1].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.texture_sampler_pairs[i + 1].glsl_name = tex_name;
        shd_desc.texture_sampler_pairs[i + 1].view_slot = i + 1;
        shd_desc.texture_sampler_pairs[i + 1].sampler_slot = 0;  // Use same sampler
    }

    shader = sg_make_shader(&shd_desc);
    free(vs_src);
    free(fs_src);

    if (shader.id == SG_INVALID_ID) {
        effect_log.error("Failed to create shader for %s", shader_name.c_str());
        return;
    }

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shader;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT16;
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline = sg_make_pipeline(&pip_desc);

    if (pipeline.id == SG_INVALID_ID) {
        effect_log.error("Failed to create pipeline for %s", shader_name.c_str());
    } else {
        effect_log.info("Created pipeline for %s", shader_name.c_str());
    }
}

void ShaderPass::apply() {
    if (!enabled || pipeline.id == SG_INVALID_ID) return;
}

void ShaderPass::applyUniforms() {
    int u_idx = 3;
    for (auto const& [name, vals] : uniforms) {
        if (u_idx >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) break;
        float data[4] = {0, 0, 0, 0};
        for (int i = 0; i < (int)vals.size() && i < 4; i++) data[i] = vals[i];
        sg_range range = SG_RANGE(data);
        sg_apply_uniforms(u_idx, &range);
        u_idx++;
    }
}

void ShaderPass::showInspector(int id) {
    ImGui::PushID(id);

    // Pass header with enable toggle
    bool was_enabled = enabled;
    if (ImGui::Checkbox(shader_name.empty() ? "Pass" : shader_name.c_str(), &enabled)) {
        // Toggle logic if needed
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle this specific shader pass");
    }

    // Always show textures if the pass is enabled (or just always show them if expanded)
    ImGui::Indent();
    if (textures.empty()) {
        ImGui::TextDisabled("[No textures for this pass]");
    } else {
        for (int i = 0; i < (int)textures.size(); i++) {
            ImGui::PushID(i);

            // Texture Slot Description
            const char* slot_desc = "Extra Slot";
            if (texture_labels.count(i)) {
                slot_desc = texture_labels[i].c_str();
            } else {
                if (i == 0)
                    slot_desc = "Main/Mask";
                else if (i == 1)
                    slot_desc = "Secondary/Mask";
                else if (i == 2)
                    slot_desc = "Depth Map";
                else if (i == 3)
                    slot_desc = "Noise/Noise Mask";
            }

            // Mask/Enable toggle for this specific texture

            if (i < (int)texture_masks.size()) {
                bool m = texture_masks[i];
                if (ImGui::Checkbox("##mask", &m)) texture_masks[i] = m;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle this texture slot");
                ImGui::SameLine();
            }

            if (textures[i].id != SG_INVALID_ID) {
                if (ImGui::SmallButton("View")) {
                    sg_image_desc desc = sg_query_image_desc(textures[i]);
                    if (desc.width > 0 && desc.height > 0) {
                        Debugger::setPreviewTexture(textures[i], (float)desc.width / (float)desc.height);
                    }
                }
                ImGui::SameLine();
            }

            if (!texture_paths[i].empty()) {
                // Extract filename from path for cleaner UI
                const char* full_path = texture_paths[i].c_str();
                const char* filename = strrchr(full_path, '/');
                if (filename)
                    filename++;
                else
                    filename = full_path;

                ImGui::Text("%s: %s", slot_desc, filename);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", full_path);
            } else {
                ImGui::TextDisabled("%s: [Empty]", slot_desc);
            }
            ImGui::PopID();
        }
    }
    ImGui::Unindent();
    ImGui::PopID();
}

Effect::Effect(cJSON* config) {
    cJSON* passes_node = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (cJSON_IsArray(passes_node)) {
        cJSON* pass_json;
        cJSON_ArrayForEach(pass_json, passes_node) {
            passes.push_back(new ShaderPass(pass_json, nullptr));
        }
    }
}

Effect::~Effect() {
    for (auto p : passes) delete p;
    passes.clear();
}

Effect* Effect::load(const char* rel_path, cJSON* instance_config) {
    // Only support depthparallax for now as requested
    if (strstr(rel_path, "depthparallax") == nullptr) {
        effect_log.warn("Ignoring unsupported effect: %s", rel_path);
        return nullptr;
    }

    char abs_path[1024];
    if (!state.asset_mgr.resolvePath(rel_path, abs_path, sizeof(abs_path))) return nullptr;

    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return nullptr;

    cJSON* config = cJSON_Parse(json_str);
    free(json_str);
    if (!config) return nullptr;

    Effect* effect = new Effect(config);
    effect->file_path = rel_path;

    // Apply instance overrides to passes
    cJSON* inst_passes = cJSON_GetObjectItemCaseSensitive(instance_config, "passes");
    if (cJSON_IsArray(inst_passes)) {
        for (int i = 0; i < cJSON_GetArraySize(inst_passes); i++) {
            if (i < (int)effect->passes.size()) {
                cJSON* pass_config = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(config, "passes"), i);
                cJSON* inst_pass_config = cJSON_GetArrayItem(inst_passes, i);
                delete effect->passes[i];
                effect->passes[i] = new ShaderPass(pass_config, inst_pass_config);
            }
        }
    }

    cJSON* vis = cJSON_GetObjectItemCaseSensitive(instance_config, "visible");
    if (cJSON_IsBool(vis))
        effect->visible = cJSON_IsTrue(vis);
    else if (cJSON_IsObject(vis)) {
        cJSON* val = cJSON_GetObjectItemCaseSensitive(vis, "value");
        if (cJSON_IsBool(val)) effect->visible = cJSON_IsTrue(val);
    }

    effect->init();

    cJSON_Delete(config);
    return effect;
}

void Effect::init() {
    for (auto p : passes) p->init();
}

void Effect::apply() {
    for (auto p : passes) p->apply();
}

void Effect::showInspector(int id) {
    ImGui::PushID(id);

    // Visibility Toggle
    if (ImGui::Button(visible ? "V" : " ", ImVec2(25, 0))) {
        visible = !visible;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Effect Visibility");

    ImGui::SameLine();

    // Solo Toggle
    bool was_solo = solo;
    if (was_solo) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.5f, 0.0f, 1.0f));
    }
    if (ImGui::Button("S", ImVec2(25, 0))) {
        solo = !solo;
    }
    if (was_solo) ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo Effect (Ctrl+Click effect name also works)");

    ImGui::SameLine();

    std::string effect_name = "Unknown Effect";
    if (!passes.empty() && !passes[0]->shader_name.empty()) {
        effect_name = passes[0]->shader_name;
    } else {
        size_t last_slash = file_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            std::string sub = file_path.substr(0, last_slash);
            size_t prev_slash = sub.find_last_of('/');
            if (prev_slash != std::string::npos) {
                effect_name = sub.substr(prev_slash + 1);
            } else {
                effect_name = sub;
            }
        } else {
            effect_name = file_path;
        }
    }

    if (!effect_name.empty()) {
        effect_name[0] = toupper(effect_name[0]);
    }

    bool open = ImGui::TreeNodeEx(effect_name.c_str(), ImGuiTreeNodeFlags_FramePadding);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s (Ctrl+Click to toggle Solo)", file_path.c_str());
    }
    if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
        solo = !solo;
    }

    if (open) {
        ImGui::Indent();
        for (int i = 0; i < (int)passes.size(); i++) {
            passes[i]->showInspector(i);
        }
        ImGui::Unindent();
        ImGui::TreePop();
    }
    ImGui::PopID();
}
