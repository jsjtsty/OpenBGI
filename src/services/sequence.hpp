#pragma once
#include <future>
#include <map>
#include <optional>
#include "core/worker.hpp"
#include "formats/bm.hpp"
#include "render/display.hpp"

namespace obgi {

class SequenceService {
  public:
    struct Info { std::uint32_t id, width, height, mode, rate, frames; };
    struct DecodedFrame { BmDecoder decoder; std::uint32_t next; std::vector<std::uint32_t> pixels; };
    struct Operation {
        std::uint32_t sequence = 0, bitmap = 0;
        std::uint64_t request = 0, bitmap_generation = 0;
        std::future<Result<DecodedFrame>> task;
        std::optional<std::uint32_t> result;
    };
    struct Request { std::uint32_t status = 0; std::shared_ptr<Operation> operation; };
    struct CloneResult { std::uint32_t status = 0, id = 0; };
    explicit SequenceService(Display& display, bool inline_jobs = false) : display_(display), worker_(inline_jobs) {}
    Result<Info> load(std::vector<std::uint8_t> bytes);
    bool release(std::uint32_t id);
    CloneResult clone(std::uint32_t id);
    Request decode(std::uint32_t bitmap, std::uint32_t id, std::uint32_t frame, bool deferred);
    std::optional<std::uint32_t> poll(Operation& operation);
    bool defer_next_decode = false;
  private:
    struct Sequence {
        std::shared_ptr<const std::vector<std::uint8_t>> file;
        BmDecoder decoder;
        std::uint32_t next = 0, original = 0, clone = 0;
        std::uint64_t applied = 0;
    };
    static Result<DecodedFrame> decode_frame(std::shared_ptr<const std::vector<std::uint8_t>> file,
                                            BmDecoder decoder, std::uint32_t next, std::uint32_t frame);
    Display& display_;
    std::map<std::uint32_t, Sequence> sequences_;
    std::uint32_t counter_ = 0;
    std::uint64_t request_ = 0;
    Worker worker_;
};

} // namespace obgi
