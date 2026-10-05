#include <string>

#include "shared/graphics/shader/shader_backend_internal.h"
#include "test_util.h"

using namespace shader_backend_internal;

namespace {
bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// The vertex declaration order differs from the pipeline layout (a_Color is declared last), which is how
// the native rope particle shader is written.
void testVertexInputsFollowPipelineLayout() {
    sg_shader_desc desc = {};
    desc.attrs[0].glsl_name = "a_PositionVec4";
    desc.attrs[1].glsl_name = "a_TexCoordVec4";
    desc.attrs[2].glsl_name = "a_Color";
    desc.attrs[3].glsl_name = "a_TexCoordVec4C1";

    const std::string source =
        "#version 450\n"
        "in vec4 a_PositionVec4;\n"
        "in vec4 a_TexCoordVec4;\n"
        "in vec4 a_TexCoordVec4C1;\n"
        "in vec4 a_Color;\n"
        "out vec4 v_Color;\n"
        "void main() { v_Color = a_Color; }\n";

    const std::string vertex = make_vulkan_source(desc, source, SG_SHADERSTAGE_VERTEX);
    CHECK(contains(vertex, "layout(location = 0) in vec4 a_PositionVec4;"));
    CHECK(contains(vertex, "layout(location = 1) in vec4 a_TexCoordVec4;"));
    CHECK(contains(vertex, "layout(location = 2) in vec4 a_Color;"));
    CHECK(contains(vertex, "layout(location = 3) in vec4 a_TexCoordVec4C1;"));
    // Varyings are not vertex inputs.
    CHECK(!contains(vertex, "layout(location = 0) out"));

    const std::string fragment = make_vulkan_source(desc, "in vec4 a_Color;\n", SG_SHADERSTAGE_FRAGMENT);
    CHECK(!contains(fragment, "layout(location"));
}
}  // namespace

int main() {
    testVertexInputsFollowPipelineLayout();
    return test::finish("vulkan source tests");
}
