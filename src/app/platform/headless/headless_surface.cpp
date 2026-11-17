#include "app/platform/headless/headless_surface.h"

namespace headless {
namespace {
struct Size {
    int width = 1920;
    int height = 1080;
    int fps = 60;
};

Size g_size;
}  // namespace

void setSize(int width, int height, int fps) {
    g_size = {width, height, fps};
}

Surface::Surface(sg_environment environment) : environment_(environment) {}

Surface::~Surface() {
    releaseTargets();
}

int Surface::width() const {
    return g_size.width;
}

int Surface::height() const {
    return g_size.height;
}

float Surface::dpiScale() const {
    return 1.0f;
}

double Surface::frameDuration() const {
    return 1.0 / g_size.fps;
}

void Surface::requestQuit() {
    quit_.store(true);
}

sg_environment Surface::environment() {
    return environment_;
}

sg_swapchain Surface::acquireSwapchain() {
    return {};
}

bool Surface::hasSwapchain() const {
    return false;
}

sg_attachments Surface::offscreenAttachments() {
    ensureTargets();
    return attachments_;
}

sg_image Surface::offscreenColor() {
    ensureTargets();
    return color_;
}

bool Surface::quitRequested() const {
    return quit_.load();
}

// Created on first use, because sg_make_image needs sg_setup to have run.
void Surface::ensureTargets() {
    if (color_.id != SG_INVALID_ID) return;

    sg_image_desc color_desc = {};
    color_desc.usage.color_attachment = true;
    color_desc.width = g_size.width;
    color_desc.height = g_size.height;
    color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    color_desc.sample_count = 1;
    color_desc.label = "headless color";
    color_ = sg_make_image(&color_desc);
    sg_view_desc color_view_desc = {};
    color_view_desc.color_attachment.image = color_;
    color_view_ = sg_make_view(&color_view_desc);
    attachments_.colors[0] = color_view_;

    if (environment_.defaults.depth_format == SG_PIXELFORMAT_NONE) return;
    sg_image_desc depth_desc = {};
    depth_desc.usage.depth_stencil_attachment = true;
    depth_desc.width = g_size.width;
    depth_desc.height = g_size.height;
    depth_desc.pixel_format = environment_.defaults.depth_format;
    depth_desc.sample_count = 1;
    depth_desc.label = "headless depth";
    depth_ = sg_make_image(&depth_desc);
    sg_view_desc depth_view_desc = {};
    depth_view_desc.depth_stencil_attachment.image = depth_;
    depth_view_ = sg_make_view(&depth_view_desc);
    attachments_.depth_stencil = depth_view_;
}

void Surface::releaseTargets() {
    if (depth_view_.id != SG_INVALID_ID) sg_destroy_view(depth_view_);
    if (depth_.id != SG_INVALID_ID) sg_destroy_image(depth_);
    if (color_view_.id != SG_INVALID_ID) sg_destroy_view(color_view_);
    if (color_.id != SG_INVALID_ID) sg_destroy_image(color_);
    depth_view_ = {SG_INVALID_ID};
    depth_ = {SG_INVALID_ID};
    color_view_ = {SG_INVALID_ID};
    color_ = {SG_INVALID_ID};
    attachments_ = {};
}

}  // namespace headless
