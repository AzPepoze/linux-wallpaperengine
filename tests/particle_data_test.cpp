#include <limits>

#include "test_util.h"
#include "wallpaper/2d/layers/particle/particle_parser.h"
#include "wallpaper/2d/layers/particle/particle_rope.h"

int main() {
    cJSON* json = cJSON_Parse(R"({"controlpoint":[{"id":0,"flags":1,"offset":"2 3 0"},
        {"id":8,"flags":1}],"emitter":[{"name":"sphererandom","flags":2,"rate":32}],
        "renderer":[{"name":"ropetrail","length":3,"segments":24}],"children":[]})");
    const auto config = ParticleParser::parse(json);
    cJSON_Delete(json);
    CHECK(config.control_points[0].flags == 1);
    CHECK(config.control_points[0].offset[0] == 2 && config.control_points[0].offset[1] == 3);
    CHECK(config.emitters.size() == 1 && config.emitters[0].flags == 2);
    CHECK(config.renderer.type == "ropetrail" && config.renderer.length == 3);
    CHECK(config.renderer.segments == 24);

    std::vector<ParticleRopeVertex> vertices;
    std::vector<uint32_t> indices;
    const std::vector<ParticleRopePoint> points = {
        {{0, 0, 0}, {1, 0, 0, 0.25f}, 8}, {{10, 0, 0}, {0, 1, 0, 0.5f}, 4}, {{10, 10, 0}, {0, 0, 1, 1}, 2}};
    appendParticleRope(points, vertices, indices);
    CHECK(vertices.size() == 8 && indices.size() == 12);
    CHECK(vertices[0].position[3] == 4 && vertices[0].control_end[3] == 2);
    CHECK(vertices[0].endpoint[0] == 10 && vertices[0].endpoint[3] == 3);
    CHECK(vertices[0].color[3] == 0.25f && vertices[0].color_end[3] == 0.5f);
    CHECK(vertices[4].control_start[3] == 1);
    for (auto index : indices) CHECK(index < vertices.size());

    vertices.clear();
    indices.clear();
    appendParticleRope({points[0], points[0]}, vertices, indices);
    CHECK(vertices.empty() && indices.empty());
    auto bad = points[1];
    bad.position[0] = std::numeric_limits<float>::infinity();
    appendParticleRope({points[0], bad}, vertices, indices);
    CHECK(vertices.empty() && indices.empty());
    appendParticleRope({points[0]}, vertices, indices);
    CHECK(vertices.empty() && indices.empty());
    return test::finish("particle data checks");
}
