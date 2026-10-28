// The scripts' symbol tables (80:D0..D4): tables of named values with a fixed value size, kept in insertion order.
// Analysis: analysis/子opcode语义表.md and the handlers FUN_00499180..00499240 / FUN_0045d800 family.
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "bindings/system_subops.hpp"

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

constexpr Word kNoTable = 0x80000002u;  // the table id does not exist
constexpr Word kNoSymbol = 0x80000003u; // the symbol (or index) does not exist
constexpr Word kBadSize = 0x80000001u;  // value size below 2

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

} // namespace

void register_table_subops(vm::SubopRegistry& r, SystemState& state) {
    SystemState* s = &state;

    // (out id address, value size): creates a table; ids count up from 1. Pushes 0, or an error code for a size below 2.
    add(r, 0x80D0, "create table", 2, 1, [s](Ctx& c) {
        const Word size = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        if (size < 2) {
            c.push(kBadSize);
            return 0;
        }
        const Word id = ++s->last_table_id;
        s->tables[id].value_size = size;
        if (out != 0) c.write(out, id, 4);
        c.push(0);
        return 0;
    });
    // (table id): deletes a table. 0 or "no such table"
    add(r, 0x80D1, "delete table", 1, 1, [s](Ctx& c) {
        const Word id = c.pop();
        if (!c.ok()) return 0;
        c.push(s->tables.erase(id) ? 0 : kNoTable);
        return 0;
    });
    // (table id, name, value): sets a symbol's value (value_size bytes copied from the address)
    add(r, 0x80D2, "set symbol", 3, 1, [s](Ctx& c) {
        const Word value = c.pop(), name_addr = c.pop(), id = c.pop();
        std::string name = c.cstring(name_addr);
        if (!c.ok()) return 0;
        auto it = s->tables.find(id);
        if (it == s->tables.end()) {
            c.push(kNoTable);
            return 0;
        }
        SystemState::Table& t = it->second;
        std::vector<std::uint8_t> bytes;
        c.read_bytes(value, t.value_size, bytes);
        if (!c.ok()) return 0;
        auto e = std::find_if(t.entries.begin(), t.entries.end(), [&](const auto& x) { return x.first == name; });
        if (e == t.entries.end()) t.entries.emplace_back(std::move(name), std::move(bytes));
        else e->second = std::move(bytes);
        c.push(0);
        return 0;
    });
    // (table id, name): removes a symbol
    add(r, 0x80D3, "remove symbol", 2, 1, [s](Ctx& c) {
        const Word name_addr = c.pop(), id = c.pop();
        std::string name = c.cstring(name_addr);
        if (!c.ok()) return 0;
        auto it = s->tables.find(id);
        if (it == s->tables.end()) {
            c.push(kNoTable);
            return 0;
        }
        auto& entries = it->second.entries;
        auto e = std::find_if(entries.begin(), entries.end(), [&](const auto& x) { return x.first == name; });
        if (e == entries.end()) {
            c.push(kNoSymbol);
        } else {
            entries.erase(e);
            c.push(0);
        }
        return 0;
    });
    // (destination, table id, name, index): copies a value out, looked up by name, or by position when the name
    // address is 0. Pushes 0, "no such table" or "no such symbol".
    add(r, 0x80D4, "get symbol", 4, 1, [s](Ctx& c) {
        const Word index = c.pop(), name_addr = c.pop(), id = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        auto it = s->tables.find(id);
        if (it == s->tables.end()) {
            c.push(kNoTable);
            return 0;
        }
        const auto& entries = it->second.entries;
        const std::vector<std::uint8_t>* value = nullptr;
        if (name_addr == 0) {
            if (index < entries.size()) value = &entries[index].second;
        } else {
            std::string name = c.cstring(name_addr);
            if (!c.ok()) return 0;
            auto e = std::find_if(entries.begin(), entries.end(), [&](const auto& x) { return x.first == name; });
            if (e != entries.end()) value = &e->second;
        }
        if (!value) {
            c.push(kNoSymbol);
            return 0;
        }
        if (dst != 0 && !value->empty()) c.write_bytes(dst, value->data(), value->size());
        c.push(0);
        return 0;
    });

    // ---- named bit tables (80:88..8B), the tables saved in BGI.gdb. Bit i is bit 7 - (i & 7) of byte (i >> 3) (verified in the original). ----
    auto find_bits = [s](const std::string& name) -> GdbData::BitTable* {
        for (auto& t : s->gdb.tables)
            if (t.name == name) return &t;
        return nullptr;
    };
    // (name, bits): creates a table, or resizes it keeping the old bits. Pushes 1, or 0 for a size of 0.
    add(r, 0x8088, "define bit table", 2, 1, [s, find_bits](Ctx& c) {
        const Word bits = c.pop();
        std::string name = c.cstring(c.pop());
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_DBG34")) std::fprintf(stderr, "80:88 [%s] bits=%u\n", name.c_str(), bits);
        if (bits == 0 || bits > 0x7FFFFFF8u) {
            c.push(0);
            return 0;
        }
        GdbData::BitTable* t = find_bits(name);
        if (!t) t = &s->gdb.tables.emplace_back(GdbData::BitTable{name, 0, {}});
        t->bits = bits;
        t->data.resize((std::size_t(bits) + 7) / 8, 0);
        c.push(1);
        return 0;
    });
    // (name, index, value): sets one bit. Pushes 0, 1 (no such table) or 2 (index out of range)
    add(r, 0x8089, "set bit", 3, 1, [find_bits](Ctx& c) {
        const Word value = c.pop(), index = c.pop();
        std::string name = c.cstring(c.pop());
        if (!c.ok()) return 0;
        GdbData::BitTable* t = find_bits(name);
        if (!t) c.push(1);
        else if (index >= t->bits) c.push(2);
        else {
            std::uint8_t& b = t->data[index >> 3];
            const auto mask = static_cast<std::uint8_t>(0x80u >> (index & 7));
            b = value ? (b | mask) : (b & ~mask);
            c.push(0);
        }
        return 0;
    });
    // (name, index, value, count): sets a run of bits. Pushes 0, 1 (no table), 2 (index out of range), 3 (bad count)
    add(r, 0x808A, "set bit range", 4, 1, [s, find_bits](Ctx& c) {
        const Word count = c.pop(), value = c.pop(), index = c.pop();
        std::string name = c.cstring(c.pop());
        if (!c.ok()) return 0;
        GdbData::BitTable* t = find_bits(name);
        if (!t && std::getenv("OBGI_DBG34")) {
            std::fprintf(stderr, "tables:");
            for (auto& x : s->gdb.tables) std::fprintf(stderr, " %s(%u)", x.name.c_str(), x.bits);
            std::fprintf(stderr, "\n");
        }
        if (std::getenv("OBGI_DBG34")) std::fprintf(stderr, "80:8A [%s] index=%u value=%u count=%u -> %s\n", name.c_str(), index, value, count, t ? "found" : "NO TABLE");
        if (!t) c.push(1);
        else if (index >= t->bits) c.push(2);
        else if (count - 1u > 0xFFFFu || std::uint64_t(index) + count > t->bits) c.push(3);
        else {
            for (Word i = index; i < index + count; ++i) {
                std::uint8_t& b = t->data[i >> 3];
                const auto mask = static_cast<std::uint8_t>(0x80u >> (i & 7));
                b = value ? (b | mask) : (b & ~mask);
            }
            c.push(0);
        }
        return 0;
    });
    // (destination, name, index): reads one bit (0 or 1) to the address. Pushes 0, 1 (no table) or 2 (out of range)
    add(r, 0x808B, "get bit", 3, 1, [find_bits](Ctx& c) {
        const Word index = c.pop();
        std::string name = c.cstring(c.pop());
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        GdbData::BitTable* t = find_bits(name);
        if (!t) c.push(1);
        else if (index >= t->bits) c.push(2);
        else {
            c.write(dst, (t->data[index >> 3] >> (7 - (index & 7))) & 1u, 4);
            c.push(0);
        }
        return 0;
    });
}

} // namespace obgi
