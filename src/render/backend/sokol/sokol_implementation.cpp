#include "core/build_config.h"

#if !DEBUG_BUILD
#define SOKOL_NO_ENTRY
#endif
#define SOKOL_IMPL
#define SOKOL_VULKAN
#define SOKOL_LOG_IMPL
#define SOKOL_ARGS_IMPL
#include "sokol_app.h"
#include "sokol_args.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"

#if DEBUG_BUILD
#define SOKOL_IMGUI_IMPL

#include "imgui.h"
// sokol_imgui implementation needs to be in a C++ file to use the C++ ImGui API

// Dear ImGui 1.92.9 exposes the canonical CmdLists.Size while the Sokol
// backend still reads the legacy CmdListsCount field.
#define CmdListsCount CmdLists.Size
#include "util/sokol_imgui.h"
#undef CmdListsCount
#endif

#include "sokol_log.h"
#include "sokol_time.h"

#undef SOKOL_IMPL
#undef SOKOL_GFX_IMPL
#undef SOKOL_APP_IMPL
#undef SOKOL_LOG_IMPL
#undef SOKOL_ARGS_IMPL
#if DEBUG_BUILD
#undef SOKOL_IMGUI_IMPL
#endif
#undef SOKOL_GLUE_IMPL
#undef SOKOL_TIME_IMPL
#if !DEBUG_BUILD
#undef SOKOL_NO_ENTRY
#endif

#if DEBUG_BUILD
#include "sokol_backend_ext.inl"
#endif
