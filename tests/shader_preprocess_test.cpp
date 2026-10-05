// Unit-style checks for the GLSL -> Slang source compatibility layer.
// Snippets are synthetic and exercise one pattern class each.

#include <cstdio>
#include <string>

#include "shared/graphics/shader/shader_processor.h"
#include "test_util.h"

namespace {

void expectContains(const char* test, const std::string& text, const std::string& needle) {
    ++test::checks;
    if (text.find(needle) == std::string::npos) {
        std::printf("FAIL %s: missing '%s'\n---\n%s\n---\n", test, needle.c_str(), text.c_str());
        ++test::failures;
    }
}

void expectNotContains(const char* test, const std::string& text, const std::string& needle) {
    ++test::checks;
    if (text.find(needle) != std::string::npos) {
        std::printf("FAIL %s: unexpected '%s'\n---\n%s\n---\n", test, needle.c_str(), text.c_str());
        ++test::failures;
    }
}

int countOccurrences(const std::string& text, const std::string& needle) {
    int count = 0;
    for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos; pos += needle.size()) ++count;
    return count;
}

// Resolves nothing; used to exercise the full source-processing pipeline.
struct StubResolver : public IAssetResolver {
    GfxImage resolveTexture(const char*, std::string* = nullptr, int = 0) const override {
        return GfxImage{};
    }
    GfxImage resolveMaterialTexture(const char*, std::string* = nullptr) const override {
        return GfxImage{};
    }
    bool resolvePath(const char*, char*, int) const override {
        return false;
    }
};

void testPreprocessorUndefinedMacro() {
    std::string source = "#if UNKNOWN == 1\nint a;\n#endif\n";
    ShaderSourceProcessor::normalizePreprocessor(source);
    expectContains("preprocess.undefined", source, "#if 0 == 1");
}

void testPreprocessorDefinedMacroPreserved() {
    std::string source = "#define KNOWN 2\n#if KNOWN == 2\nint a;\n#endif\n";
    ShaderSourceProcessor::normalizePreprocessor(source);
    expectContains("preprocess.defined", source, "#if KNOWN == 2");
}

void testPreprocessorDefinedOperator() {
    std::string source = "#define A 1\n#if defined(A)\nint a;\n#endif\n";
    ShaderSourceProcessor::normalizePreprocessor(source);
    expectContains("preprocess.defined_operator", source, "#if defined(A)");
}

void testPreprocessorUnmatchedEndifDropped() {
    std::string source = "int a;\n#endif\nint b;\n";
    ShaderSourceProcessor::normalizePreprocessor(source);
    ++test::checks;
    if (countOccurrences(source, "#endif") != 0) {
        std::printf("FAIL preprocess.unmatched_endif: #endif was not dropped\n");
        ++test::failures;
    }
}

void testPreprocessorUnclosedIfClosed() {
    std::string source = "#if X\nint a;\n";
    ShaderSourceProcessor::normalizePreprocessor(source);
    ++test::checks;
    if (countOccurrences(source, "#endif") != 1) {
        std::printf("FAIL preprocess.unclosed_if: missing synthesized #endif\n");
        ++test::failures;
    }
}

void testPreprocessorTrailingSemicolon() {
    std::string source = "#if A == 1\n#elif A == 2;\n#endif\n";
    ShaderSourceProcessor::normalizePreprocessor(source);
    expectContains("preprocess.elif_semicolon", source, "#elif 0 == 2");
    expectNotContains("preprocess.elif_semicolon", source, "2;");
}

void testPreprocessorMemberAccess() {
    std::string source = "#if resolution.x < resolution.y\nint a;\n#endif\n";
    ShaderSourceProcessor::normalizePreprocessor(source);
    expectContains("preprocess.member_access", source, "#if 0 < 0");
}

void testNarrowingAssignment() {
    std::string source = "void main() {\nvec4 a;\nvec2 b;\nb = a;\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectContains("rewrite.narrowing_assignment", source, "b = a.xy;");
}

void testOutOfRangeSwizzle() {
    std::string source = "varying vec2 uv;\nvoid main() {\nvec2 x = uv.zw;\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectContains("rewrite.swizzle", source, "uv.xy");
    expectNotContains("rewrite.swizzle", source, "uv.zw");
}

void testChainedOutOfRangeSwizzle() {
    std::string source = "varying vec4 v;\nvoid main() {\nvec2 x = v.xy.zw;\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectContains("rewrite.chained_swizzle", source, "v.xy.xy");
    expectNotContains("rewrite.chained_swizzle", source, "v.xy.zw");
}

void testImmediateBinaryMismatch() {
    std::string source = "void main() {\nvec4 a;\nvec2 b;\nfloat c = dot(a * b, b);\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectContains("rewrite.binary_operand", source, "a.xy * b");
}

void testMixVectorWidths() {
    std::string source = "void main() {\nvec4 a;\nvec3 b;\nfloat m;\nfrag_color = vec4(mix(a, b, m));\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectContains("rewrite.mix_widths", source, "mix((a).rgb, (b), (m))");
}

void testScalarFromVectorCall() {
    std::string source = "void main() {\nfloat m = texture(s, uv);\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectContains("rewrite.scalar_call", source, "texture(s, uv).x");
}

void testAmbiguousWidthSwizzlePreserved() {
    std::string source =
        "vec4 decompress(vec4 normal) {\nnormal.xw = normal.wx;\nreturn normal;\n}\n"
        "vec3 compute(vec3 normal) {\nreturn normal.xyz;\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectContains("rewrite.ambiguous_width", source, "normal.xw = normal.wx;");
    expectNotContains("rewrite.ambiguous_width", source, "normal.xx");
}

void testFunctionArgumentNarrowing() {
    std::string source =
        "vec2 scale2(vec2 v) {\nreturn v;\n}\n"
        "void main() {\nvec4 a;\nvec2 b = scale2(a);\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectContains("rewrite.function_argument", source, "scale2(a.xy)");
}

void testScalarFirstVectorBroadcast() {
    std::string source = "uniform vec2 scale;\nvoid main() {\nfloat f = max(1, abs(scale));\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectContains("rewrite.scalar_vector_broadcast", source, "max(vec2(1), abs(scale))");
}

void testVectorFirstScalarPreserved() {
    std::string source = "uniform vec2 scale;\nvoid main() {\nvec2 f = max(scale, 1.0);\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectNotContains("rewrite.vector_scalar_preserved", source, "vec2(1.0)");
}

void testHlslVectorToScalarInitializerWithCall() {
    StubResolver resolver;
    std::string source = "uniform vec2 scale;\nvoid main() {\nfloat f = (1 + abs(scale)) * max(1, abs(scale));\n}\n";
    const std::string processed = ShaderSourceProcessor::processShaderSource(source, "test.vert", resolver, true);
    expectContains("rewrite.hlsl_scalar_call", processed, "max(vec2(1), abs(scale))");
    expectContains("rewrite.hlsl_scalar_call", processed, ").x;");
}

}  // namespace

void testRepeatedSwizzleNeverAssigned() {
    std::string source =
        "vec3 decode(vec4 normal) {\n"
        "normal.xw = normal.wx;\n"
        "normal.yx = normal.yw * 2.0 - vec2(0.5, 1.0);\n"
        "normal.xy = normal.rg * 2.0 - 1.0;\n"
        "return normal.xyz;\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectNotContains("rewrite.repeated_swizzle_lvalue", source, "normal.xx =");
    expectNotContains("rewrite.repeated_swizzle_lvalue", source, "normal.yy =");
}

void testRepeatedSwizzleNotIntroducedOnAssignmentTarget() {
    std::string source = "vec3 n;\nvoid main() {\nn.xw = n.wx;\nvec2 t = n.zw;\n}\n";
    ShaderSourceProcessor::rewriteGlslCompatibility(source);
    expectNotContains("rewrite.repeated_swizzle_target", source, "n.xx =");
    expectContains("rewrite.repeated_swizzle_target", source, "n.xw =");
}

void testWhiteTextureDefaults() {
    const char* source =
        "uniform sampler2D g_Texture0; // {\"material\":\"framebuffer\",\"hidden\":true}\n"
        "uniform sampler2D g_Texture1; // {\"combo\":\"OPACITY\",\"default\":\"util/white\",\"material\":\"mask\"}\n"
        "uniform sampler2D g_Texture2; // {\"default\":\"util/black\",\"format\":\"r8\"}\n"
        "uniform sampler2D g_Texture3; // {\"label\":\"x\",\"default\": \"util/white\"}\n"
        "// uniform sampler2D g_Texture4; not a declaration {\"default\":\"util/white\"}\n"
        "float g_Texture5 = 1.0; // {\"default\":\"util/white\"}\n";
    const unsigned int mask = ShaderSourceProcessor::extractWhiteTextureDefaults(source);
    CHECK((mask & 1u) != 0);   // g_Texture1
    CHECK((mask & 2u) == 0);   // g_Texture2 defaults to black
    CHECK((mask & 4u) != 0);   // g_Texture3, with whitespace after the colon
    CHECK((mask & 8u) == 0);   // commented-out declaration
    CHECK((mask & 16u) == 0);  // not a sampler
    CHECK((mask & ~(1u | 4u)) == 0);
}

int main() {
    testWhiteTextureDefaults();
    testRepeatedSwizzleNeverAssigned();
    testRepeatedSwizzleNotIntroducedOnAssignmentTarget();
    testPreprocessorUndefinedMacro();
    testPreprocessorDefinedMacroPreserved();
    testPreprocessorDefinedOperator();
    testPreprocessorUnmatchedEndifDropped();
    testPreprocessorUnclosedIfClosed();
    testPreprocessorTrailingSemicolon();
    testPreprocessorMemberAccess();
    testNarrowingAssignment();
    testOutOfRangeSwizzle();
    testChainedOutOfRangeSwizzle();
    testImmediateBinaryMismatch();
    testMixVectorWidths();
    testScalarFromVectorCall();
    testAmbiguousWidthSwizzlePreserved();
    testFunctionArgumentNarrowing();
    testScalarFirstVectorBroadcast();
    testVectorFirstScalarPreserved();
    testHlslVectorToScalarInitializerWithCall();

    return test::finish("shader preprocess tests");
}
