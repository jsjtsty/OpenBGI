#pragma once
#include <set>
#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "bindings/arc_resource_finder.hpp"
#include "formats/gdb.hpp"
#include "render/display.hpp"
#include "vm/vm.hpp"
#include "services/input.hpp"

namespace obgi {

// Engine-wide settings the scripts write at start-up (analysis/子opcode语义表.md, 80:xx / 81:xx / 90:xx / 91:xx
// "set a global" ops). Most of them only record a value for other subsystems to read later.
struct SystemState : InputState {
    std::function<std::array<std::uint16_t, 8>()> calendar_time;
    std::function<std::int64_t()> frame_elapsed_ms; // real time spent in the current interactive scheduler frame
    bool launcher = false;                 // 80:FD: started with "Execute as a launcher."
    std::string game_name = "AmairoChocolate2ML"; // 80:E8
    std::uint32_t frame_ms = 4;            // 90:02: present interval in ms; the original scheduler starts at 250 fps
    bool display_enabled = true;           // 90:01: whether the runner presents the composed display
    std::uint32_t code_page = 932;         // 81:00: 932 (CP932), 936 (GBK), or 65001 (UTF-8)
    bool code_page_locked = false;          // an identified translated script set has a fixed source encoding
    // 90:20..24, 90:28, 90:29: timed changes of a display object (position along an ease curve, level along another),
    // advanced by advance_motions() once per scheduler frame
    struct Motion {
        std::uint32_t object = 0;
        std::int64_t start_ms = 0, duration_ms = 0;
        std::int32_t from_x = 0, from_y = 0, to_x = 0, to_y = 0;
        std::int32_t curve = 0;                  // ease curve of the position
        std::int32_t from_level = 0, to_level = 0; // the object's alpha (90:32)
        std::int32_t level_curve = 0;
        std::int32_t from_progress = 0, to_progress = 0; // 90:28/29: the object's progress (0..0x100), linearly; equal = untouched
        std::int32_t from_z = 0, to_z = 0;       // the depth of a projected plane (16.16)
        std::vector<std::array<double, 3>> path;  // 90:29: the start and the control points (x, y pixels, depth in 16.16 / 65536); more than two follow a cubic spline
        // The frame-stepped kinds (wait_tween.c: WaitMove2, WaitObjQuake): time counts in frames of `frame_ms`, `frames` in all.
        enum class Kind : std::uint8_t { Tween, Via, Quake } kind = Kind::Tween;
        std::int32_t frames = 0, frame_ms = 0;
        std::vector<std::array<std::int32_t, 2>> via_points; // 90:24: one point of the curve per frame plus one
        std::int32_t quake_pattern = 0, cycle_frames = 1;    // 90:2C: 0, 1, 4 vertical, 2, 3, 5 horizontal
        std::vector<std::int32_t> quake_amps, quake_wave;    // the amplitude of every cycle, one cycle of the wave (16.16 of the amplitude)
    };
    std::vector<Motion> motions;
    // 90:F8-FD (sysobj/targets.c): sprites registered as click targets, newest first; each gets the running number of its registration
    // (never reused until the list is cleared) and the left-button state sampled on it every frame
    struct ClickTarget { std::uint32_t index = 0, handle = 0; bool hit = false; };
    std::vector<ClickTarget> targets;
    std::uint32_t target_count = 0;
    std::uint32_t last_manager_object = 0;
    std::uint32_t rand_seed = 0;           // 80:00
    std::size_t scratch_size = 0;     // 80:70: zeroed buffer of 0x1000 << v bytes; saved in BGI.gdb (first 0x400)
    GdbData gdb;                           // global data (80:80 loads BGI.gdb, 80:81 saves it, 80:82-85 use it)
    // Default resolution table from the engine; 81:60 can replace entries.
    std::array<std::array<std::uint32_t, 2>, 16> pair_table{{
        {320, 240}, {640, 480}, {800, 600}, {1024, 768},
        {1024, 576}, {1280, 960}, {1280, 720}, {1920, 1440},
        {1920, 1080}, {2560, 1920}, {2560, 1440}, {3840, 2880},
        {3840, 2160}, {1600, 900}, {1366, 768}, {960, 540}
    }};
    std::vector<std::string> search_paths; // 80:37: extra resource search paths, newest first
    // 80:3D hands scripts these paths (CP932 bytes, with a trailing separator). The game directory is read-only for
    // the engine, so 80:28 / 80:2A map anything under it into `data_dir`, where scripts may create folders.
    std::string game_path;                 // selector 0
    std::string alt_path;                  // selector 1; empty = none
    std::filesystem::path game_dir;
    std::filesystem::path data_dir;
    // Ops whose only job is to hand their arguments to a subsystem keep them here, in the order the script pushed them
    // (window, input, typesetting and display settings); the subsystem reads them when it exists.
    std::map<std::uint16_t, std::vector<std::uint32_t>> records;
    std::vector<std::uint32_t> window_numbers;                      // 80:62: zero-terminated list kept with `window_id`
    std::uint32_t window_id = 0;
    struct ArchiveGroup {
        std::string name;
        std::vector<std::uint32_t> numbers;
    };
    std::vector<ArchiveGroup> archive_groups;                       // 80:38; the handle scripts get is the index + 1
    std::uint32_t display_width = 1920, display_height = 1080;      // 81:0E reports a desktop of this size
    std::string window_title;                                       // 80:66 (CP932)
    bool transition_running = false;                                // B0:05
    std::int64_t transition_end_ms = 0;
    Display* display = nullptr;                      // set by the subsystem that draws (may stay null)
    std::uint32_t last_icon_control = 0;
    struct PanelButton {
        bool enabled = false, shown = false;
        std::int32_t x = 0, y = 0;            // top-left of the picture, relative to the window object
        std::int32_t bmp_normal = -1, bmp_focus = -1, bmp_selected = -1, bmp_selected_focus = -1;
        std::int32_t hit_mask = -1;             // hit-test bitmap (-1: whole picture, -2: never hit)
        std::uint32_t flags = 0;
        std::uint32_t sprite = 0;             // display object id of the drawn button, 0 = not drawn
        std::uint32_t hotkey = 0;             // plain panel: virtual key that decides the button (bit 31: also on repeat)
        bool no_hit = false;                  // hit mask -2: the pointer cannot hit it
    };
    struct PanelGroup {
        std::int32_t columns = 1, selected = -1;
        bool selectable = true, hover_selects = false, click_decides = false;
        std::int32_t link_id = -1;            // groups sharing an id (not -1) are mutually exclusive
        std::vector<PanelButton> buttons;
    };
    struct IconControl {
        std::uint32_t window = 0;                        // 91:B8: the window object the buttons sit on
        bool basic = false;                              // made by 90:B8: the plain panel with the smaller descriptor
        bool paint_only = false;                         // 90:B6 / 90:B7: buttons painted over a window, no input
        std::vector<PanelGroup> groups;                  // 91:BA: the started panel
        int current_group = -1;
        int hover_group = -1, hover_index = -1;          // the button under the pointer
        std::uint32_t seen_clicks = 0;                   // left-button presses already looked at
        bool modal = false, mouse = false, select_on_decide = false; // descriptor: modal input, window mouse layer, select on mouse decision
        std::uint32_t input_mask = 0;                    // descriptor: input bits the panel ignores
        std::int32_t key_map = 0;                        // descriptor: built-in key map 0..3 (user maps 4..7 act like 0)
        int last_mouse_x = -1, last_mouse_y = -1;        // pointer of the last hover selection
        int reads = 0;                                 // 90:BF reads answered so far (the first two give the initial event)
        std::deque<std::array<std::uint32_t, 3>> events; // pending 90:BF records
        std::array<std::uint32_t, 6> info{1, 0x00370038, 0x002E0037, 0x006E0069, 0, 0}; // 90:BC record
    };
    std::map<std::uint32_t, IconControl> icon_controls; // 91:B8 registered control objects
    bool quit_requested = false; // 80:69 (post WM_CLOSE to the main window): the host ends the run
    std::vector<std::function<void(std::int64_t)>> per_frame; // run once per scheduler frame with the virtual time (particle screens, ...)
    std::vector<std::pair<std::string, std::string>> ruby; // 91:94 / 91:96: ruby dictionary (word, reading), longest word first
    std::map<std::uint16_t, std::uint32_t> values; // plain settings by sub-opcode id (80:68, 80:52, ...)
    // Named lists of strings (80:84, 80:85, 80:D8..DC). List 0x80000000 is the global key list kept in BGI.gdb
    // (`gdb.keys`); the others live only for the session.
    static constexpr std::uint32_t kGlobalSet = 0x80000000u;
    std::map<std::uint32_t, std::vector<std::string>> string_sets;
    std::vector<std::string>* string_set(std::uint32_t id, bool create) {
        if (id == kGlobalSet) return &gdb.keys;
        auto it = string_sets.find(id);
        if (it != string_sets.end()) return &it->second;
        return create ? &string_sets[id] : nullptr;
    }
    // 81:D8: a table of word lists: name -> (key, value) pairs, loaded from a script block (analysis/子opcode语义表.md)
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> dictionary;
    // 80:D0..D4: symbol tables of named values of one fixed size, in insertion order
    struct Table {
        std::uint32_t value_size = 0;
        std::vector<std::pair<std::string, std::vector<std::uint8_t>>> entries;
    };
    std::map<std::uint32_t, Table> tables;
    std::uint32_t last_table_id = 0;
    // 90:80..89: script window planes, at most 16, handle 0xB0000000 + slot. Nothing draws them yet; the state a
    // later display object needs is kept.
    struct Plane {
        std::uint32_t width = 0, height = 0; // after the original's rounding (see 90:80)
        std::uint32_t order = 0;             // 90:82, one of the 6 layer orders
        std::uint32_t image = 0xFFFFFFFFu;   // 90:86: bitmap id, -1 = none
        std::uint32_t flag = 0, mode = 0;    // 90:84, 90:87
        std::int32_t x = 0, y = 0;           // 90:85
        bool frame_visible = false, text_visible = true;
        std::array<std::int32_t, 4> area{};  // 90:88: left, top, right, bottom (inclusive)
        std::vector<std::uint32_t> extra;    // 90:85 / 90:86 arguments not interpreted yet
    std::map<std::uint16_t, std::vector<std::uint32_t>> props; // 91:88.. / 92:88.. text settings, by sub-opcode id
    };
    std::array<std::optional<Plane>, 16> planes;
    // 90:84 records the last explicit show request; animation can also change the display object's visibility.
    std::array<bool, 16> plane_shown{};
    std::array<std::uint32_t, 16> plane_level{}, plane_effect{};
    std::array<std::uint32_t, 16> plane_text_level{}; // transparency of a window's text layer while a page fades out (0 = opaque)
    // B0:80: the script shows a message box (UTF-8 text). The host decides what to do with it; the script goes on.
    std::function<void(const std::string&)> message_box;
    // B0:81, B0:82: a question box (UTF-8 text); true for "yes" / "OK". Without a host the default button answers.
    std::function<bool(const std::string&, bool ok_cancel, bool default_yes)> confirm_box;
    // 80:98: registered items (number, two script values, flag 1), newest first in the original list
    struct Item {
        std::uint32_t id = 0, a = 0, b = 0, flag = 1;
        std::uint32_t value = 0;                       // 80:A8
        std::deque<std::vector<std::uint8_t>> entries; // 80:9C records, newest first; a = most kept, b = bytes each
    };
    std::vector<Item> items;
    std::uint32_t last_item_id = 0;
    std::deque<std::array<std::uint32_t, 3>> event_queue; // 80:A1 / 80:A0: global FIFO of three-word messages
    std::optional<std::uint32_t> shutdown_message_thread;
    std::array<bool, 8> filter_used{}; // 90:60: the eight filter object slots
    // 90:D0..DF: knob controls (sliders), handle 0xF0000000 + index, at most 32. A knob drives a display object (the thumb)
    // along a track: the object sits at the knob's base position plus the value (pixels, or a discrete step index).
    struct Knob {
        std::uint32_t handle = 0;                 // the knob is a display object itself (at the track's origin); the thumb follows it (91:3E)
        std::uint32_t target = 0;                 // the thumb object
        std::int32_t value_x = 0, value_y = 0;    // 90:D6 / 90:D7
        std::int32_t steps_x = 0, steps_y = 0;    // 90:D8: 0 = pixels, 1 = one cell, n = n cells (16.16 values)
        std::int32_t track_w = 0, track_h = 0;    // 90:D9: size of the track
        std::int32_t grab_x = 0, grab_y = 0;      // pointer offset inside the thumb while dragging
        bool enabled = true;                      // 90:DC
        bool flagged = false;                     // set by a right click on the thumb, read and cleared by 90:DB
        std::int32_t wheel_step = 0;              // the last wheel scroll (+1 / -1), read and cleared by 90:DA
        std::uint32_t priority = 0;
    };
    std::map<std::uint32_t, Knob> knobs;          // by handle
    std::vector<std::uint32_t> knob_wheel;        // 90:DE / 90:DF: knobs the mouse wheel scrolls, newest first
    std::uint32_t knob_focus = 0;                // the knob being dragged
    std::uint32_t knob_seen_left = 0, knob_seen_right = 0; // presses already looked at
    std::map<std::uint32_t, std::uint32_t> typesetting; // 91:9A: key -> value (analysis/排版引擎.md)
};

// Registers the setters above and the few system queries (local time, memory size, resource existence).
void register_system_subops(vm::SubopRegistry& registry, SystemState& state, ArcResourceFinder& resources);

// Window, display-mode, input-map and typesetting settings (window_subops.cpp); they record their arguments in
// SystemState for the subsystems that will use them.
void register_window_subops(vm::SubopRegistry& registry, SystemState& state, Display* display = nullptr);

// Symbol tables 80:D0..D4 (table_subops.cpp).
void register_table_subops(vm::SubopRegistry& registry, SystemState& state);

// Script window planes 90:80..89 (plane_subops.cpp).
// `display` (may be null) gives each plane an image slot (0x10000 + slot) and a sprite under its handle, so the 90:3x object ops reach it.
void register_plane_subops(vm::SubopRegistry& registry, SystemState& state, Display* display = nullptr);
// Knob controls 90:D0..DF (knob_subops.cpp); knob_input() follows the pointer once per scheduler frame.
void register_knob_subops(vm::SubopRegistry& registry, SystemState& state);
void knob_input(SystemState& state);
// Moves the display objects of the running 90:28 motions to where they are at `now_ms`; finished motions are dropped.
void advance_motions(SystemState& state, Display& display, std::int64_t now_ms);


} // namespace obgi
