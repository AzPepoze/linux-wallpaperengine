#include "render_observer.h"

#include "shared/core/build_config.h"

#if DEBUG_BUILD
#include "render_diagnostics.h"

IRenderObserver& renderObserver() {
    return RenderDiagnostics::instance();
}
#else
IRenderObserver& renderObserver() {
    static IRenderObserver noop;
    return noop;
}
#endif
