#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>

namespace obgi {

struct MessageVisual {
    bool rich = false;
    std::size_t total = 0;
    std::function<void(std::size_t)> draw;
    int pre_frames = 0;
    std::function<void(int, int)> pre;
    std::function<void()> pre_done;
};

struct MessagePacing {
    int character_ms = 32;
    std::int64_t auto_ms = 0, delay_ms = 0;
    bool automatic = false, instant = false, allow_skip = false, wait_click = false;
};

struct MessageProgress {
    std::int64_t started_ms = 0, page_ms = 0, completed_ms = -1;
    std::uint32_t input_base = 0;
    std::size_t shown = 0;
    bool page_done = false;
};

// Logical message progress is explicit. Visual callbacks only paint a requested position.
class MessageService {
  public:
    struct Cursor { std::int32_t x = 0, y = 0; bool placed = false; };
    std::array<Cursor, 16> cursors{};
    struct Operation {
        MessageVisual visual;
        MessagePacing pacing;
        MessageProgress progress;
        // Returns the opcode result when complete; no VM state is accessed here.
        std::optional<std::uint32_t> update(std::int64_t now_ms, std::uint32_t input);
    };
    Operation start(MessageVisual visual, MessagePacing pacing, std::int64_t now_ms, std::uint32_t input);
};

} // namespace obgi
