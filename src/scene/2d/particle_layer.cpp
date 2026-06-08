#include "particle_layer.h"

#include "../../core/context.h"
#include "../../core/utils.h"
#include "imgui.h"

ParticleLayer::ParticleLayer(const char* name, ParticleSystem* ps) : Layer(name), ps(ps) {}

ParticleLayer::~ParticleLayer() {
    if (ps) delete ps;
}

ParticleLayer* ParticleLayer::createFromJSON(cJSON* node) {
    ParticleSystem* ps = ParticleSystem::createFromJSON(node, state.asset_mgr, state.scene_w, state.scene_h);
    if (ps) {
        ParticleLayer* layer = new ParticleLayer("Particle", ps);
        layer->loadBaseProperties(node);
        layer->path = ps->config_path;
        return layer;
    }
    return nullptr;
}

void ParticleLayer::update(float dt) {
    if (ps) ps->update(dt);
}

void ParticleLayer::draw() {
    if (visible && ps) {
        // Sync transformation to particle system
        ps->layer_origin[0] = origin[0];
        ps->layer_origin[1] = origin[1];
        ps->layer_origin[2] = origin[2];
        ps->parallax[0] = parallax[0];
        ps->parallax[1] = parallax[1];

        for (auto eff : effects) eff->apply();
        ps->draw();
    }
}

void ParticleLayer::drawDebug() {
    if (ps) ps->drawDebugBounds();
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
    ImGui::DragFloat3("Position", (float*)origin, 1.0f);
    ImGui::DragFloat2("Parallax", (float*)parallax, 0.01f, -10, 10);

    showEffectsInspector();
}
