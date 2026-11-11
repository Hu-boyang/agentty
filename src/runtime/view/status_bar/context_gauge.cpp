#include "agentty/runtime/view/status_bar/context_gauge.hpp"

namespace agentty::ui {

maya::ContextGauge::Config context_gauge_config(const Model& m) {
    maya::ContextGauge::Config cfg;
    cfg.used     = m.s.tokens_in;
    cfg.max      = m.s.context_max;
    cfg.cells    = 10;
    cfg.show_bar = true;     // StatusBar 以宽度为基础,每-frame 覆盖
    return cfg;
}

} // namespace agentty::ui
