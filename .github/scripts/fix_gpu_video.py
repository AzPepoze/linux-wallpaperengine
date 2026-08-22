from pathlib import Path


def rep(path, old, new):
    p = Path(path)
    s = p.read_text()
    if old not in s:
        raise RuntimeError(f"missing {old!r} in {path}")
    p.write_text(s.replace(old, new))

p = Path("src/formats/wallpaper_engine/texture/video_texture.cpp")
s = p.read_text()
s = s.replace('#include <gst/vulkan/vulkan.h>\n', '#include <gst/vulkan/vulkan.h>\n#include <drm_fourcc.h>\n')
s = s.replace('target.repr = pl_color_repr_srgb;', 'target.repr = pl_color_repr_rgb;')
s = s.replace('    void returnInputs(std::vector<struct WrappedInputPlaceholder>&) {}\n\n', '')
s = s.replace('        VkSemaphore semaphore = pl_vulkan_sem_create(vulkan_->gpu, nullptr);\n', '''        pl_vulkan_sem_params sem_params = {};\n        sem_params.type = VK_SEMAPHORE_TYPE_BINARY;\n        VkSemaphore semaphore = pl_vulkan_sem_create(vulkan_->gpu, &sem_params);\n''')
p.write_text(s)

rep("src/render/backend/sokol/sokol_backend_ext.inl", "desc.usage.storage_attachment", "desc.usage.storage_image")

p = Path("xmake.lua")
s = p.read_text()
s = s.replace('add_requires("pkgconfig::libplacebo", {alias = "libplacebo", system = true})\n',
              'add_requires("pkgconfig::libplacebo", {alias = "libplacebo", system = true})\nadd_requires("pkgconfig::libdrm", {alias = "libdrm", system = true})\n')
s = s.replace('"gst_core", "gst_app", "gst_video", "gst_vulkan", "gst_allocators", "libplacebo")',
              '"gst_core", "gst_app", "gst_video", "gst_vulkan", "gst_allocators", "libplacebo", "libdrm")')
p.write_text(s)
