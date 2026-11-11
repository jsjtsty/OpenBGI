#include "services/input.hpp"
#include <algorithm>

namespace obgi {
std::vector<std::uint32_t> InputState::input_numbers(std::uint32_t bit) const {
    if (auto it = input_maps.find(bit); it != input_maps.end()) return it->second;
    switch (bit) { // the original's initial table
    case 0x1: return {1};
    case 0x2: return {2};
    case 0x4: return {4};
    case 0x10: return {5};
    case 0x20: return {6};
    case 0x40: return {0x0E};
    case 0x80: return {0x0F};
    case 0x100: return {0x0D};
    case 0x200: return {0x20};
    case 0x1000: return {0x26};
    case 0x2000: return {0x28};
    case 0x4000: return {0x25};
    case 0x8000: return {0x27};
    case 0x2000000: return {0x30, 0x60};
    case 0x40000000: return {0x09};
    case 0x80000000: return {0x11};
    default:
        if (bit >= 0x10000 && bit <= 0x1000000) { // digits 1..9
            std::uint32_t k = 0;
            while ((0x10000u << k) != bit) ++k;
            return {0x31 + k, 0x61 + k};
        }
        return {};
    }
}

namespace {

std::uint32_t input_layer_key(std::uint32_t priority) { return (priority << 20) | 0xFFFFFu; }

void insert_input_layer(std::vector<std::uint32_t>& layers, std::uint32_t key) {
    auto at = std::find_if(layers.begin(), layers.end(), [key](std::uint32_t old) { return old <= key; });
    layers.insert(at, key); // a newly opened layer is above older layers at the same priority
}

bool take_input_trigger(InputState& state, std::uint32_t vk) {
    if (vk >= state.input_pending.size()) return false;
    const bool pressed = state.input_pending[vk] != 0 ||
                         (state.input_tracked_down[vk] && state.input_held[vk] && !state.input_consumed[vk]);
    state.input_pending[vk] = 0;
    if (vk == 1) state.click_position_valid = false;
    if (state.input_held[vk]) state.input_consumed[vk] = true;
    return pressed;
}

} // namespace

bool consume_input_trigger(InputState& state, std::uint32_t virtual_key) {
    return take_input_trigger(state, virtual_key);
}

namespace { constexpr std::array<std::uint32_t, 3> kMessageKeys{0x0D, 0x20, 0x01}; }

MessageInput::MessageInput(const InputState& state) {
    for (std::size_t i = 0; i < kMessageKeys.size(); ++i) seen_[i] = state.input_counts[kMessageKeys[i]];
}

std::uint32_t MessageInput::poll(InputState& state) {
    for (std::size_t i = 0; i < kMessageKeys.size(); ++i) {
        const auto count = state.input_counts[kMessageKeys[i]];
        if (count != seen_[i] && state.input_enabled && consume_input_trigger(state, kMessageKeys[i])) ++delivered_;
        seen_[i] = count;
    }
    return delivered_;
}

std::uint32_t input_skip_state(InputState& state) {
    if (!state.input_enabled || !state.skip_enabled) return 0;
    const auto keys = state.input_numbers(0x80000000);
    bool any_held = false;
    for (std::uint32_t vk : keys) {
        if (vk < state.input_held.size() && state.input_held[vk]) any_held = true;
        if (state.skip_flushed && take_input_trigger(state, vk)) state.skip_latch = true;
        else if (!state.skip_flushed && vk < state.input_held.size() && state.input_held[vk]) return 1;
    }
    if (state.skip_flushed && !any_held) state.skip_flushed = false;
    return state.skip_latch ? 1u : 0u;
}

std::uint32_t poll_input(InputState& state, std::uint32_t priority) {
    const std::uint32_t key = input_layer_key(priority);
    std::uint32_t mask = 0;

    // The highest key layer receives keyboard groups. Each physical trigger is consumed
    // at its first read, so a lower layer cannot observe the same press afterward.
    if (state.input_enabled && !state.key_layers.empty() && key >= state.key_layers.front()) {
        static constexpr std::uint32_t kGroups[] = {0x40, 0x80, 0x100, 0x200, 0x1000, 0x2000, 0x4000,
                                                     0x8000, 0x10000, 0x20000, 0x40000, 0x80000,
                                                     0x100000, 0x200000, 0x400000, 0x800000,
                                                     0x1000000, 0x2000000, 0x40000000};
        for (std::uint32_t bit : kGroups) {
            for (std::uint32_t vk : state.input_numbers(bit)) {
                if (take_input_trigger(state, vk)) {
                    mask |= bit;
                    break; // as in the engine, later aliases retain their pending trigger
                }
            }
        }
        if (input_skip_state(state)) mask |= 0x80000000u;
    }

    // The mouse layer is an unbounded rectangle here; display-object hit layers are
    // maintained by the display subsystem separately.
    if (state.input_enabled && !state.mouse_layers.empty() && key == state.mouse_layers.front()) {
        static constexpr std::pair<std::uint32_t, std::uint32_t> kButtons[] = {{1, 0x1}, {2, 0x2}, {4, 0x4}, {5, 0x10}, {6, 0x20}};
        for (const auto& [vk, bit] : kButtons)
            if (take_input_trigger(state, vk)) mask |= bit;
    }
    return mask;
}

void push_input_priority(InputState& state, std::uint32_t priority) {
    const std::uint32_t key = input_layer_key(priority);
    insert_input_layer(state.key_layers, key);
    insert_input_layer(state.mouse_layers, key);
    (void)poll_input(state, priority); // opening a layer swallows input already pending for it
}

void pop_input_priority(InputState& state, std::uint32_t priority) {
    const std::uint32_t key = input_layer_key(priority);
    (void)poll_input(state, priority); // swallow before exposing the layer below
    auto remove = [key](std::vector<std::uint32_t>& layers) {
        auto at = std::find(layers.begin(), layers.end(), key);
        if (at != layers.end()) layers.erase(at);
    };
    remove(state.key_layers);
    remove(state.mouse_layers);
}


} // namespace obgi
