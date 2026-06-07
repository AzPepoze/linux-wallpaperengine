#define SOKOL_IMPL
#define SOKOL_GLCORE
#define SOKOL_LOG_IMPL
#define SOKOL_ARGS_IMPL
#define SOKOL_IMGUI_IMPL

#include "imgui.h"
// sokol_imgui implementation needs to be in a C++ file to use the C++ ImGui API
#include "../../libs/sokol/sokol_app.h"
#include "../../libs/sokol/sokol_args.h"
#include "../../libs/sokol/sokol_gfx.h"
#include "../../libs/sokol/sokol_glue.h"
#include "../../libs/sokol/sokol_imgui.h"
#include "../../libs/sokol/sokol_log.h"
#include "../../libs/sokol/sokol_time.h"
