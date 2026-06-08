#include "particle_layer.h"

#include "imgui.h"

ParticleLayer::ParticleLayer(const char* name, ParticleSystem* ps) : Layer(name), ps(ps) {}

ParticleLayer::~ParticleLayer() {
    if (ps) delete ps;
}

void ParticleLayer::update(float dt) {
    if (ps) ps->update(dt);
}

void ParticleLayer::draw() {
    if (visible && ps) ps->draw();
}

void ParticleLayer::showInspector() {
    ImGui::Checkbox("Visible", &visible);
    ImGui::Text("Type: Particle System");
    if (!path.empty()) {
        ImGui::Text("Path: %s", path.c_str());
    }

    ImGui::Separator();
    if (ps) ps->showInspector();

    ImGui::Separator();
    ImGui::DragFloat2("Position", (float*)origin, 1.0f);
    ImGui::DragFloat2("Parallax", (float*)parallax, 0.01f, -10, 10);
}
