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
    if (ps) ps->showInspector();
}
