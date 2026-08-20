#define SOKOL_IMPL
#define SOKOL_VULKAN
#define SOKOL_LOG_IMPL
#define SOKOL_ARGS_IMPL
#define SOKOL_IMGUI_IMPL

#include "imgui.h"
// sokol_imgui implementation needs to be in a C++ file to use the C++ ImGui API
#include "../../libs/sokol/sokol_app.h"
#include "../../libs/sokol/sokol_args.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../../libs/sokol/sokol_glue.h"

// Dear ImGui 1.92.9 has a regression where the legacy CmdListsCount field is
// always zero. sokol_imgui still uses that obsolete field, causing it to
// early-return without rendering any UI. Use the canonical CmdLists.Size while
// compiling the vendored backend. This remains correct with fixed ImGui versions.
#define CmdListsCount CmdLists.Size
#include "../../libs/sokol/sokol_imgui.h"
#undef CmdListsCount

#include "../../libs/sokol/sokol_log.h"
#include "../../libs/sokol/sokol_time.h"
