#ifndef SCRIPT_SCENE_BACKEND_H
#define SCRIPT_SCENE_BACKEND_H

#include <stdint.h>

#include <string>
#include <vector>

// What scripts can see and change of the running scene. Layers are addressed by scene object id (0 = none).
class ScriptSceneBackend {
   public:
    virtual ~ScriptSceneBackend() = default;

    virtual bool layerExists(uint32_t id) = 0;
    virtual std::string layerName(uint32_t id) = 0;

    // Vector properties: origin, scale, angles (degrees), size, parallaxDepth. Bool property: visible.
    virtual bool getVector(uint32_t id, const std::string& property, double out[3], int& components) = 0;
    virtual bool setVector(uint32_t id, const std::string& property, const double value[3]) = 0;
    virtual bool getBool(uint32_t id, const std::string& property, bool& out) = 0;
    virtual bool setBool(uint32_t id, const std::string& property, bool value) = 0;

    // Playback commands of sound layers (play, stop, pause).
    virtual bool layerCommand(uint32_t /*id*/, const std::string& /*command*/) {
        return false;
    }
    // World transform, column-major.
    virtual bool getWorldMatrix(uint32_t /*id*/, double /*out*/[16]) {
        return false;
    }
    virtual bool getNumber(uint32_t /*id*/, const std::string& /*property*/, double& /*out*/) {
        return false;
    }
    virtual bool setNumber(uint32_t /*id*/, const std::string& /*property*/, double /*value*/) {
        return false;
    }
    virtual bool getString(uint32_t /*id*/, const std::string& /*property*/, std::string& /*out*/) {
        return false;
    }
    virtual bool setString(uint32_t /*id*/, const std::string& /*property*/, const std::string& /*value*/) {
        return false;
    }

    virtual uint32_t parentOf(uint32_t id) = 0;
    virtual std::vector<uint32_t> childrenOf(uint32_t id) = 0;
    // Parent 0 detaches the layer. `adjust_transforms` keeps the layer where it is in the world (its local transform
    // is recomputed); otherwise the local transform stays. `attachment` names a puppet attachment of the parent.
    virtual bool setParent(uint32_t /*id*/, uint32_t /*parent*/, const std::string& /*attachment*/,
                           bool /*adjust_transforms*/) {
        return false;
    }

    virtual uint32_t findLayerByName(const std::string& name) = 0;
    virtual std::vector<uint32_t> allLayers() = 0;  // in scene (draw) order

    // Animations are addressed by an opaque non-zero handle. `kind` is timeline, texture (sprite sheet), layer (puppet
    // animation layer by name or index) or any (timeline, else puppet layer by name, else sprite sheet).
    virtual uint32_t findAnimation(uint32_t /*layer_id*/, const std::string& /*kind*/, const std::string& /*key*/) {
        return 0;
    }
    virtual bool animationGet(uint32_t /*handle*/, const std::string& /*field*/, double& /*out*/) {
        return false;
    }
    virtual bool animationGetString(uint32_t /*handle*/, const std::string& /*field*/, std::string& /*out*/) {
        return false;
    }
    virtual bool animationSet(uint32_t /*handle*/, const std::string& /*field*/, double /*value*/) {
        return false;
    }
    virtual bool animationCommand(uint32_t /*handle*/, const std::string& /*command*/) {
        return false;
    }
    virtual std::vector<uint32_t> takeEndedAnimations() {
        return {};
    }
    virtual int animationLayerCount(uint32_t /*layer_id*/) {
        return 0;
    }
    // `config_json`: {"animation": name|id, "rate", "blend", "additive", "name", "once", "autoRemove"}. Returns the
    // animation handle, 0 when the model has no such clip.
    virtual uint32_t createAnimationLayer(uint32_t /*layer_id*/, const std::string& /*config_json*/) {
        return 0;
    }
    virtual bool destroyAnimationLayer(uint32_t /*layer_id*/, const std::string& /*name_or_index*/) {
        return false;
    }
    // Puppet attachments by name or index: fields origin, angles (degrees, world space) and matrix (world space).
    virtual int findAttachment(uint32_t /*layer_id*/, const std::string& /*name*/) {
        return -1;
    }
    virtual bool getAttachment(uint32_t /*layer_id*/, const std::string& /*name_or_index*/,
                               const std::string& /*field*/, std::vector<double>& /*out*/) {
        return false;
    }
    // A 3x3 (column-major, 9 values) taking a puppet attachment of `attachment_layer` into this layer's texture space.
    virtual bool transformAttachmentToTexture(uint32_t /*layer_id*/, uint32_t /*attachment_layer*/,
                                              const std::string& /*key*/, std::vector<double>& /*out*/) {
        return false;
    }
    // Rotates around the layer's own axes; angles in degrees.
    virtual bool rotateObjectSpace(uint32_t /*id*/, const double /*angles*/[3]) {
        return false;
    }

    // Puppet skeleton: bones by index. Fields origin, angles (degrees), scale (local pose) and matrix (model space).
    virtual int boneCount(uint32_t /*layer_id*/) {
        return 0;
    }
    virtual int findBone(uint32_t /*layer_id*/, const std::string& /*name*/) {
        return -1;
    }
    virtual std::string boneName(uint32_t /*layer_id*/, int /*bone*/) {
        return "";
    }
    virtual int boneParent(uint32_t /*layer_id*/, int /*bone*/) {
        return -1;
    }
    virtual bool getBone(uint32_t /*layer_id*/, int /*bone*/, const std::string& /*field*/,
                         std::vector<double>& /*out*/) {
        return false;
    }
    virtual bool setBone(uint32_t /*layer_id*/, int /*bone*/, const std::string& /*field*/,
                         const std::vector<double>& /*value*/) {
        return false;
    }
    virtual bool resetBone(uint32_t /*layer_id*/, int /*bone*/) {
        return false;
    }

    // The scene object's JSON as authored; empty for unknown layers.
    virtual std::string initialLayerConfig(uint32_t /*id*/) {
        return "";
    }

    // Scene settings (bloomstrength, clearcolor, camerashake...), by the SceneScript property name; booleans are 0 / 1.
    virtual bool getSceneProperty(const std::string& /*name*/, std::vector<double>& /*out*/) {
        return false;
    }
    virtual bool setSceneProperty(const std::string& /*name*/, const std::vector<double>& /*value*/) {
        return false;
    }

    // Dynamic layers. `config_json` is a scene object (image, particle, text, sound...); returns the new object id, or 0.
    // Destruction is applied between frames; sortLayer moves a layer to a draw-order index.
    virtual uint32_t createLayer(const std::string& /*config_json*/) {
        return 0;
    }
    virtual bool destroyLayer(uint32_t /*id*/) {
        return false;
    }
    virtual bool sortLayer(uint32_t /*id*/, int /*index*/) {
        return false;
    }

    // Effects of a layer, by index in the layer's effect list.
    virtual int effectCount(uint32_t /*layer_id*/) {
        return 0;
    }
    virtual int findEffect(uint32_t /*layer_id*/, const std::string& /*name*/) {
        return -1;
    }
    virtual std::string effectName(uint32_t /*layer_id*/, int /*effect*/) {
        return "";
    }
    virtual bool effectVisible(uint32_t /*layer_id*/, int /*effect*/, bool& /*out*/) {
        return false;
    }
    virtual bool setEffectVisible(uint32_t /*layer_id*/, int /*effect*/, bool /*value*/) {
        return false;
    }
    // Material constants of an effect's passes; a set applies to every pass that has the constant.
    virtual bool getMaterialProperty(uint32_t /*layer_id*/, int /*effect*/, const std::string& /*name*/,
                                     std::vector<double>& /*out*/) {
        return false;
    }
    virtual bool setMaterialProperty(uint32_t /*layer_id*/, int /*effect*/, const std::string& /*name*/,
                                     const std::vector<double>& /*value*/) {
        return false;
    }
    // One pass of an effect is one material (IMaterial).
    virtual int effectPassCount(uint32_t /*layer_id*/, int /*effect*/) {
        return 0;
    }
    virtual bool getPassMaterialProperty(uint32_t /*layer_id*/, int /*effect*/, int /*pass*/,
                                         const std::string& /*name*/, std::vector<double>& /*out*/) {
        return false;
    }
    virtual bool setPassMaterialProperty(uint32_t /*layer_id*/, int /*effect*/, int /*pass*/,
                                         const std::string& /*name*/, const std::vector<double>& /*value*/) {
        return false;
    }
    // Runs a function the effect defines (the fluid simulation's clear functions).
    virtual bool executeMaterialFunction(uint32_t /*layer_id*/, int /*effect*/, const std::string& /*name*/) {
        return false;
    }
};

#endif  // SCRIPT_SCENE_BACKEND_H
