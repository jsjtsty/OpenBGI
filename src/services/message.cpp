#include "services/message.hpp"
#include <algorithm>

namespace obgi {

MessageService::Operation MessageService::start(MessageVisual visual, MessagePacing pacing, std::int64_t now,
                                               std::uint32_t input) {
    Operation op{std::move(visual), pacing, {now, now, -1, input}};
    return op;
}

std::optional<std::uint32_t> MessageService::Operation::update(std::int64_t now, std::uint32_t input) {
    auto& p = progress;
    if (!p.page_done) {
        const auto age = std::max<std::int64_t>(0, now - p.page_ms);
        const auto frame = age / 16;
        const bool hurry = pacing.allow_skip && input != p.input_base;
        if (visual.pre_frames > 0 && frame < visual.pre_frames && !hurry) {
            if (visual.pre) visual.pre(static_cast<int>(frame), visual.pre_frames);
            return std::nullopt;
        }
        if (hurry) p.input_base = input;
        if (visual.pre_done) visual.pre_done();
        p.page_done = true;
        if (visual.pre_frames > 0) p.started_ms = hurry ? now : p.page_ms + std::int64_t{visual.pre_frames} * 16;
    }
    const auto age = std::max<std::int64_t>(0, now - p.started_ms);
    if (p.shown < visual.total) {
        std::size_t next = p.shown;
        if (pacing.instant || pacing.character_ms <= 0 || (pacing.allow_skip && input != p.input_base)) {
            next = visual.total;
            p.input_base = input; // this press reveals text; it does not also dismiss it
            p.completed_ms = now;
        } else {
            next = std::min(visual.total, static_cast<std::size_t>(visual.rich ? age : age / pacing.character_ms));
        }
        if (next != p.shown) {
            p.shown = next;
            if (visual.draw) visual.draw(next);
        }
    }
    if (p.shown < visual.total) return std::nullopt;
    if (p.completed_ms < 0)
        p.completed_ms = p.started_ms + static_cast<std::int64_t>(visual.total) * (visual.rich ? 1 : pacing.character_ms);
    if (!pacing.wait_click) return 0;
    if (age < pacing.delay_ms) { p.input_base = input; return std::nullopt; }
    if (input != p.input_base || (pacing.automatic && now - p.completed_ms >= pacing.auto_ms)) return 1;
    return std::nullopt;
}

} // namespace obgi
