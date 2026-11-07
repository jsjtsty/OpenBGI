#include "services/sequence.hpp"
#include <chrono>
#include <cstring>

namespace obgi {

Result<SequenceService::Info> SequenceService::load(std::vector<std::uint8_t> bytes) {
    auto file = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    auto decoder = BmDecoder::open(*file);
    if (!decoder) return std::unexpected(decoder.error());
    const auto id = ++counter_;
    Info info{id, decoder->width(), decoder->height(), load_u32le(file->data() + 0x20), decoder->rate(), decoder->frame_count()};
    sequences_.emplace(id, Sequence{std::move(file), std::move(*decoder)});
    return info;
}

bool SequenceService::release(std::uint32_t id) {
    auto it = sequences_.find(id);
    if (it == sequences_.end()) return false;
    if (auto parent = sequences_.find(it->second.original); parent != sequences_.end()) parent->second.clone = 0;
    if (auto child = sequences_.find(it->second.clone); child != sequences_.end()) child->second.original = 0;
    sequences_.erase(it); // queued jobs hold file data, never a pointer into this table
    return true;
}

SequenceService::CloneResult SequenceService::clone(std::uint32_t id) {
    auto it = sequences_.find(id);
    if (it == sequences_.end()) return {3};
    if (it->second.clone) return {7};
    auto decoder = BmDecoder::open(*it->second.file);
    if (!decoder) return {8};
    auto no = ++counter_;
    sequences_.emplace(no, Sequence{it->second.file, std::move(*decoder), 0, id});
    it->second.clone = no;
    return {0, no};
}

Result<SequenceService::DecodedFrame> SequenceService::decode_frame(std::shared_ptr<const std::vector<std::uint8_t>> file,
                                                                  BmDecoder decoder, std::uint32_t next, std::uint32_t frame) {
    if (frame + 1 < next) {
        auto reset = BmDecoder::open(*file);
        if (!reset) return std::unexpected(reset.error());
        decoder = std::move(*reset);
        next = 0;
    }
    while (next <= frame) {
        if (auto decoded = decoder.decode_frame(next); !decoded) return std::unexpected(decoded.error());
        ++next;
    }
    const auto canvas = decoder.canvas();
    std::vector<std::uint32_t> pixels(canvas.size() / 4);
    std::memcpy(pixels.data(), canvas.data(), canvas.size());
    return DecodedFrame{std::move(decoder), next, std::move(pixels)};
}

SequenceService::Request SequenceService::decode(std::uint32_t bitmap, std::uint32_t id, std::uint32_t frame, bool deferred) {
    auto it = sequences_.find(id);
    if (it == sequences_.end()) return {3};
    auto& s = it->second;
    if (frame >= s.decoder.frame_count()) return {4};
    const auto slot = display_.slot(bitmap);
    const auto format = load_u32le(s.file->data() + 0x20) == 2 ? SlotFormat::Bgra : SlotFormat::Bgrx;
    if (!slot || slot->width != s.decoder.width() || slot->height != s.decoder.height() || slot->format != format) return {5};
    auto op = std::make_shared<Operation>();
    op->sequence = id;
    op->bitmap = bitmap;
    op->bitmap_generation = display_.slot_generation(bitmap);
    op->request = ++request_;
    auto job = [file = s.file, decoder = s.decoder, next = s.next, frame]() mutable {
        return decode_frame(std::move(file), std::move(decoder), next, frame);
    };
    if (deferred) op->task = worker_.submit(std::move(job));
    else {
        std::promise<Result<DecodedFrame>> promise;
        op->task = promise.get_future();
        promise.set_value(job());
    }
    if (!deferred) return {*poll(*op)};
    return {0, std::move(op)};
}

std::optional<std::uint32_t> SequenceService::poll(Operation& op) {
    if (op.result) return op.result;
    auto it = sequences_.find(op.sequence);
    if (it == sequences_.end()) return op.result = 3;
    if (op.task.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
    auto decoded = op.task.get();
    if (!decoded) return op.result = 8;
    const auto slot = display_.slot(op.bitmap);
    if (!slot || display_.slot_generation(op.bitmap) != op.bitmap_generation) return op.result = 5;
    if (auto upload = display_.upload_slot(op.bitmap, slot->width, slot->height, slot->format, decoded->pixels); !upload)
        return op.result = 8;
    if (op.request > it->second.applied) {
        it->second.decoder = std::move(decoded->decoder);
        it->second.next = decoded->next;
        it->second.applied = op.request;
    }
    return op.result = 0;
}

} // namespace obgi
