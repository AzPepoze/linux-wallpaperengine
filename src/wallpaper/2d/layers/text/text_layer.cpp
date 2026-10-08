#include "text_layer.h"

#include <math.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <thread>

#include "shared/core/engine_context.h"
#include "shared/core/load_trace.h"
#include "shared/core/logger.h"
#include "shared/core/task_pool.h"
#include "wallpaper/2d/alpha_curve.h"
#include "wallpaper/2d/layers/text/text_raster.h"

TextLayer::TextLayer(const char* name) : ImageLayer(name, (sg_image){SG_INVALID_ID}) {}

struct RasterOutput {
    bool ok = false;
    TextRasterResult result;
};

// One raster on its way from a worker; the layer keeps it until the texture is uploaded.
struct PendingRaster {
    std::string key;
    std::shared_future<RasterOutput> future;
    std::chrono::steady_clock::time_point started;
};

namespace {

// Allows two rasterizations at once, so text does not take over the worker pool.
class RasterGate {
   public:
    void enter() {
        std::unique_lock<std::mutex> lock(mutex_);
        slot_.wait(lock, [this] { return active_ < kMaxConcurrent; });
        ++active_;
    }

    void leave() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
        }
        slot_.notify_one();
    }

   private:
    static constexpr int kMaxConcurrent = 2;
    std::mutex mutex_;
    std::condition_variable slot_;
    int active_ = 0;
};

RasterGate& rasterGate() {
    static RasterGate gate;
    return gate;
}

RasterOutput rasterInBackground(TextRasterRequest request, bool crop) {
    RasterOutput output;
    rasterGate().enter();
    output.ok = rasterizeText(request, output.result);
    if (output.ok && crop) cropToContent(output.result);
    rasterGate().leave();
    return output;
}

// Rasters still wanted by some layer, so a second layer with the same text waits on the first one.
// Only the render thread touches this map.
std::map<std::string, std::shared_future<RasterOutput>>& inFlightRasters() {
    static std::map<std::string, std::shared_future<RasterOutput>> flights;
    return flights;
}

void purgeFinishedRasters(std::map<std::string, std::shared_future<RasterOutput>>& flights) {
    for (auto flight = flights.begin(); flight != flights.end();) {
        if (flight->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            flight = flights.erase(flight);
        else
            ++flight;
    }
}

}  // namespace

TextLayer* TextLayer::createPending(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    TextObjectConfig config = TextParser::parse(doc);
    TextLayer* layer = new TextLayer(config.name.c_str());
    layer->initFromDocument(doc, ctx);
    layer->alpha_document = doc.image;
    layer->config_ = config;
    layer->size[0] = doc.text.size[0];
    layer->size[1] = doc.text.size[1];
    layer->tint[0] = config.color[0];
    layer->tint[1] = config.color[1];
    layer->tint[2] = config.color[2];
    layer->tint[3] = std::clamp(config.alpha, 0.0f, 1.0f);
    layer->beginPreparation(ctx);
    LOG_I("Created text layer '%s' (font='%s', pointsize=%.1f)", config.name.c_str(), config.font.c_str(),
          config.pointsize);
    return layer;
}

TextLayer* TextLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    TextLayer* layer = createPending(doc, ctx);
    while (!layer->pollPreparation()) std::this_thread::yield();
    return layer;
}

bool TextLayer::propertyGetString(const std::string& name, std::string& out) const {
    if (name == "text")
        out = config_.text;
    else if (name == "font")
        out = config_.font;
    else if (name == "horizontalalign")
        out = config_.horizontal_align;
    else if (name == "verticalalign")
        out = config_.vertical_align;
    else if (name == "anchor")
        out = config_.anchor;
    else
        return false;
    return true;
}

bool TextLayer::propertySetString(const std::string& name, const std::string& value) {
    if (name == "text") {
        config_.text = value;
        return true;
    }
    if (name == "font") {
        config_.font = value;
        font_path_.clear();
    } else if (name == "horizontalalign") {
        config_.horizontal_align = value;
    } else if (name == "verticalalign") {
        config_.vertical_align = value;
    } else if (name == "anchor") {
        config_.anchor = value;
    } else {
        return false;
    }
    needs_rebuild_ = true;
    return true;
}

bool TextLayer::propertyGetBool(const std::string& name, bool& out) const {
    if (name == "opaquebackground")
        out = config_.opaque_background;
    else if (name == "limitrows")
        out = config_.limit_rows;
    else if (name == "limitwidth")
        out = config_.limit_width;
    else
        return false;
    return true;
}

bool TextLayer::propertySetBool(const std::string& name, bool value) {
    if (name == "opaquebackground")
        config_.opaque_background = value;
    else if (name == "limitrows")
        config_.limit_rows = value;
    else if (name == "limitwidth")
        config_.limit_width = value;
    else
        return false;
    needs_rebuild_ = true;
    return true;
}

bool TextLayer::propertyGetVector(const std::string& name, double out[3]) const {
    if (name != "backgroundcolor") return false;
    for (size_t i = 0; i < 3; ++i) out[i] = config_.background_color[i];
    return true;
}

bool TextLayer::propertySetVector(const std::string& name, const double value[3]) {
    if (name != "backgroundcolor") return false;
    for (size_t i = 0; i < 3; ++i) config_.background_color[i] = (float)value[i];
    needs_rebuild_ = true;
    return true;
}

bool TextLayer::propertyGetNumber(const std::string& name, double& out) const {
    if (name == "pointsize")
        out = config_.pointsize;
    else if (name == "maxwidth")
        out = config_.maxwidth;
    else if (name == "maxrows")
        out = config_.max_rows;
    else if (name == "alpha")
        out = config_.alpha;
    else if (name == "brightness")
        out = config_.brightness;
    else if (name == "backgroundbrightness")
        out = config_.background_brightness;
    else if (name == "padding")
        out = config_.padding;
    else
        return false;
    return true;
}

bool TextLayer::propertySetNumber(const std::string& name, double value) {
    if (name == "alpha") {
        config_.alpha = (float)value;
        return true;
    }
    if (name == "brightness")
        config_.brightness = (float)value;
    else if (name == "backgroundbrightness")
        config_.background_brightness = (float)value;
    else if (name == "pointsize")
        config_.pointsize = (float)value;
    else if (name == "maxwidth")
        config_.maxwidth = (float)value;
    else if (name == "maxrows")
        config_.max_rows = (int)value;
    else if (name == "padding")
        config_.padding = (float)value;
    else
        return false;
    needs_rebuild_ = true;
    return true;
}

ImageLayer::ScreenRect TextLayer::screenRect(EngineContext& ctx) const {
    ScreenRect rect = ImageLayer::screenRect(ctx);
    // Wallpaper Engine anchors a text object at the corner selected by the
    // alignment (e.g. the origin is the top-left of the text for left/top) and
    // rotates around it. The base centres the sprite on the node, so offset it
    // to put the alignment corner on the node instead.
    float anchor_x = 0.0f;
    float anchor_y = 0.0f;
    if (config_.horizontal_align == "left")
        anchor_x = -rect.width * 0.5f;
    else if (config_.horizontal_align == "right")
        anchor_x = rect.width * 0.5f;
    if (config_.vertical_align == "top")
        anchor_y = -rect.height * 0.5f;
    else if (config_.vertical_align == "bottom")
        anchor_y = rect.height * 0.5f;

    if (anchor_x != 0.0f || anchor_y != 0.0f) {
        const float angle = rect.rotation * (float)M_PI / 180.0f;
        rect.x -= cosf(angle) * anchor_x - sinf(angle) * anchor_y;
        rect.y -= sinf(angle) * anchor_x + cosf(angle) * anchor_y;
    }
    return rect;
}

void TextLayer::update(float, EngineContext& ctx) {
    pollPreparation();
    refreshForEffects(ctx);
    // A hidden layer keeps its pending change and rasterizes it on the frame it becomes visible.
    if (render_active && (config_.text != current_text_ || needs_rebuild_)) {
        needs_rebuild_ = false;
        rebuild(ctx);
    }
    tint[3] = std::clamp(config_.alpha, 0.0f, 1.0f) * evaluateImageAlpha(alpha_document, ctx.time);
    if (!is_fullscreen && render_active) renderEffectChain(ctx);
}

bool TextLayer::resolveFontPath(EngineContext& ctx) {
    if (!font_path_.empty()) return true;

    std::string candidate = config_.font;
    if (candidate.empty() || candidate.rfind("systemfont", 0) == 0) candidate = "fonts/NotoSans-Regular.ttf";

    char resolved[1024] = {};
    if (ctx.asset_mgr->resolvePath(candidate.c_str(), resolved, sizeof(resolved))) {
        font_path_ = resolved;
        return true;
    }
    LOG_W("Text layer '%s': font not found: %s", config_.name.c_str(), config_.font.c_str());
    return false;
}

TextRasterRequest TextLayer::rasterRequest() const {
    TextRasterRequest request;
    request.config = config_;
    request.font_path = font_path_;
    std::copy(tint, tint + 3, request.tint);
    return request;
}

// One uploaded text texture, shared by every layer whose text and raster settings match.
struct TextTexture {
    GfxImage image;
    GfxView view;
    GfxBuffer quad;  // draws only the cropped part of the layer, when cropped
    bool cropped = false;
    float size[2] = {0.0f, 0.0f};
};

namespace {

// Entries are weak, so a texture is freed when the last layer using it is destroyed.
std::map<std::string, std::weak_ptr<TextTexture>>& textureCache() {
    static std::map<std::string, std::weak_ptr<TextTexture>> cache;
    return cache;
}

void appendNumber(std::string& key, float value) {
    char number[32];
    snprintf(number, sizeof(number), "%a|", value);
    key += number;
}

// Every setting that changes the rasterized pixels. Alpha is applied at draw time, so it is left out.
std::string textureKey(const TextObjectConfig& config, const std::string& font_path, const float tint[3],
                       bool crop) {
    std::string key = font_path + "\n" + config.text + (crop ? "\ncrop\n" : "\nfull\n");
    appendNumber(key, config.pointsize);
    appendNumber(key, config.size[0]);
    appendNumber(key, config.size[1]);
    appendNumber(key, config.maxwidth);
    appendNumber(key, config.padding);
    appendNumber(key, config.brightness);
    appendNumber(key, config.background_brightness);
    appendNumber(key, (float)config.max_rows);
    key += std::to_string(config.limit_width) + std::to_string(config.limit_rows) +
           std::to_string(config.opaque_background) + "|" + config.horizontal_align + "|" + config.vertical_align +
           "|";
    for (float value : config.background_color) appendNumber(key, value);
    // Tint only reaches the pixels through the opaque background.
    if (config.opaque_background)
        for (int channel = 0; channel < 3; ++channel) appendNumber(key, tint[channel]);
    return key;
}

std::shared_ptr<TextTexture> findTexture(const std::string& key) {
    auto found = textureCache().find(key);
    return found == textureCache().end() ? nullptr : found->second.lock();
}

void rememberTexture(const std::string& key, const std::shared_ptr<TextTexture>& texture) {
    auto& cache = textureCache();
    for (auto entry = cache.begin(); entry != cache.end();)
        entry = entry->second.expired() ? cache.erase(entry) : std::next(entry);
    cache[key] = texture;
}

}  // namespace

// A cropped texture only lines up with the layer quad when nothing else samples the full texture.
bool TextLayer::canCropTexture() const {
    return effects.empty() && !requiresSceneColor();
}

// Effects or scene-colour blending need the full texture, so re-upload it when they appear.
void TextLayer::refreshForEffects(EngineContext& ctx) {
    if (texture_ && texture_->cropped && !canCropTexture()) rebuild(ctx);
}

void TextLayer::useTexture(const std::shared_ptr<TextTexture>& texture) {
    texture_ = texture;
    img = GfxImage::borrow(texture->image);
    cached_view = GfxView::borrow(texture->view);
    source_quad = {SG_INVALID_ID};
    if (texture->cropped) source_quad = texture->quad;
    size[0] = texture->size[0];
    size[1] = texture->size[1];
}

bool TextLayer::beginPreparation(EngineContext& ctx) {
    current_text_ = config_.text;
    pending_.reset();
    if (!resolveFontPath(ctx)) return false;
    const bool crop = canCropTexture();
    const std::string key = textureKey(config_, font_path_, tint, crop);
    if (std::shared_ptr<TextTexture> cached = findTexture(key)) {
        useTexture(cached);
        return true;
    }

    auto pending = std::make_shared<PendingRaster>();
    pending->key = key;
    pending->started = std::chrono::steady_clock::now();
    auto& flights = inFlightRasters();
    purgeFinishedRasters(flights);
    auto flight = flights.find(key);
    if (flight != flights.end()) {
        pending->future = flight->second;
    } else {
        pending->future = TaskPool::instance().enqueue(rasterInBackground, rasterRequest(), crop).share();
        flights[key] = pending->future;
    }
    pending_ = pending;
    return true;
}

bool TextLayer::pollPreparation() {
    if (!pending_) return true;
    if (pending_->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;

    // Hold the future before dropping the pending state, so the output stays alive while it is read.
    std::shared_future<RasterOutput> finished = pending_->future;
    const std::string key = pending_->key;
    const auto started = pending_->started;
    pending_.reset();
    const RasterOutput& output = finished.get();

    const char* source = "cache";
    double upload_ms = 0.0;
    if (std::shared_ptr<TextTexture> cached = findTexture(key)) {
        useTexture(cached);
    } else if (output.ok) {
        source = "upload";
        const auto upload_start = std::chrono::steady_clock::now();
        if (std::shared_ptr<TextTexture> texture = uploadTexture(key, output.result)) useTexture(texture);
        upload_ms = load_trace::milliseconds(std::chrono::steady_clock::now() - upload_start);
    }
    if (load_trace::enabled())
        LOG_TAG_I("LOAD_TRACE", "text_prepare_ms=%.3f text_source=%s text_upload_ms=%.3f pixels=%dx%d",
                  load_trace::milliseconds(std::chrono::steady_clock::now() - started), source, upload_ms,
                  output.result.width, output.result.height);
    return true;
}

namespace {

// Places the cropped texture where its pixels sit inside the full layer quad.
GfxBuffer makeCropQuad(const TextRasterResult& raster) {
    const float left = raster.content_x / (float)raster.full_width;
    const float top = raster.content_y / (float)raster.full_height;
    const float right = (raster.content_x + raster.width) / (float)raster.full_width;
    const float bottom = (raster.content_y + raster.height) / (float)raster.full_height;
    const vertex_t vertices[4] = {{left, top, 0.0f, 0.0f}, {right, top, 1.0f, 0.0f}, {right, bottom, 1.0f, 1.0f},
                                  {left, bottom, 0.0f, 1.0f}};
    sg_buffer_desc desc = {};
    desc.size = sizeof(vertices);
    desc.usage.vertex_buffer = true;
    desc.data = SG_RANGE(vertices);
    return GfxBuffer(sg_make_buffer(&desc));
}

}  // namespace

std::shared_ptr<TextTexture> TextLayer::uploadTexture(const std::string& key, const TextRasterResult& raster) {
    auto texture = std::make_shared<TextTexture>();
    texture->size[0] = raster.size[0];
    texture->size[1] = raster.size[1];
    texture->cropped = raster.cropped;
    if (raster.cropped) {
        texture->quad = makeCropQuad(raster);
        if (texture->quad.id == SG_INVALID_ID) return nullptr;
    }

    sg_image_desc desc = {};
    desc.width = raster.width;
    desc.height = raster.height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
    desc.data.mip_levels[0] = {raster.pixels.data(), raster.pixels.size() * sizeof(float)};
    texture->image = GfxImage(sg_make_image(&desc));
    if (texture->image.id == SG_INVALID_ID) return nullptr;

    sg_view_desc view_desc = {};
    view_desc.texture.image = texture->image;
    texture->view = GfxView(sg_make_view(&view_desc));
    rememberTexture(key, texture);
    return texture;
}

// Synchronous rebuild for live text edits; the layer keeps its previous texture until this succeeds.
bool TextLayer::rebuild(EngineContext& ctx) {
    current_text_ = config_.text;
    pending_.reset();
    if (!resolveFontPath(ctx)) return false;
    const bool crop = canCropTexture();
    const std::string key = textureKey(config_, font_path_, tint, crop);
    if (std::shared_ptr<TextTexture> cached = findTexture(key)) {
        useTexture(cached);
        return true;
    }

    TextRasterResult raster;
    if (!rasterizeText(rasterRequest(), raster) || raster.pixels.empty()) return false;
    if (crop) cropToContent(raster);
    std::shared_ptr<TextTexture> texture = uploadTexture(key, raster);
    if (!texture) return false;
    useTexture(texture);
    return true;
}
