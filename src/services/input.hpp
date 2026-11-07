#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>
#include "vm/vm.hpp"

namespace obgi {
struct InputState {
    std::map<std::uint32_t, std::vector<std::uint32_t>> input_maps; // 80:1B: input mask -> input numbers
    // Physical input numbers (Windows virtual-key codes: 1/2/4 mouse buttons, 0x0D enter, 0x20 space, ...): press counters and
    // current held state. 80:12 / 80:1C sum the counters; 80:11 reads held state. The host updates these from input events.
    std::array<std::uint32_t, 256> input_counts{};
    std::array<std::uint32_t, 256> input_pending{};
    std::array<bool, 256> input_physical_held{};
    std::array<bool, 256> input_held{};
    std::array<bool, 256> input_tracked_down{};
    std::array<bool, 256> input_consumed{};
    bool swap_mouse_buttons = false;
    bool input_enabled = true, skip_enabled = true, skip_latch = false, skip_flushed = false;
    std::uint32_t input_serial = 0; // 80:13: increases on each newly accepted key/button press and wheel event
    std::vector<std::uint32_t> key_layers{1}, mouse_layers{1}; // highest priority first; 80:18/19 add/remove both
    int mouse_x = 0, mouse_y = 0;                       // pointer in screen coordinates, set by the host
    int click_x = 0, click_y = 0;
    bool click_position_valid = false;
    void press_input(std::uint32_t number, bool bump_serial = true) {
        if (swap_mouse_buttons && number == 2) number = 1; // the original maps right clicks to left when enabled
        if (number < input_counts.size()) {
            ++input_counts[number], ++input_pending[number], input_consumed[number] = false, input_tracked_down[number] = true;
            if (bump_serial) ++input_serial;
        }
    }
    void set_input_held(const std::array<bool, 256>& state) {
        input_physical_held = state;
        refresh_input_held();
    }
    bool set_mouse_button_swap(std::uint32_t value) {
        if (value > 1) return false;
        swap_mouse_buttons = value != 0;
        refresh_input_held();
        return true;
    }
    void set_input_enabled(bool enabled) {
        input_enabled = enabled;
        input_pending.fill(0);
        click_position_valid = false;
        input_consumed.fill(false);
        input_tracked_down.fill(false);
    }
    void refresh_input_held() {
        auto next = input_physical_held;
        if (swap_mouse_buttons) next[1] = input_physical_held[1] || input_physical_held[2], next[2] = false;
        for (std::size_t i = 0; i < next.size(); ++i) {
            if (input_held[i] && !next[i]) input_consumed[i] = false;
            if (!next[i]) input_tracked_down[i] = false;
        }
        input_held = next;
    }
    // The input numbers behind one logical input mask bit: a script's 80:1B list, else the original's default.
    std::vector<std::uint32_t> input_numbers(std::uint32_t bit) const;
    bool left_down = false;
    std::deque<int> wheel_events;
};
std::uint32_t poll_input(InputState& state, std::uint32_t priority);
bool consume_input_trigger(InputState& state, std::uint32_t key);
// A text wait observes only new triggers that remain after higher-priority controls have polled them.
class MessageInput {
  public:
    explicit MessageInput(const InputState& state);
    std::uint32_t poll(InputState& state);
  private:
    std::array<std::uint32_t, 3> seen_{};
    std::uint32_t delivered_ = 0;
};
std::uint32_t input_skip_state(InputState& state);
void push_input_priority(InputState& state, std::uint32_t priority);
void pop_input_priority(InputState& state, std::uint32_t priority);

class InputWaitPort final : public vm::WaitInput {
  public:
    explicit InputWaitPort(InputState& state) : state_(state) {}
    void begin(vm::Word priority) override { push_input_priority(state_, priority); }
    bool triggered(vm::Word priority) override { return (poll_input(state_, priority) & 0x80000181u) != 0; }
    void end(vm::Word priority) override { pop_input_priority(state_, priority); }
  private:
    InputState& state_;
};
} // namespace obgi
