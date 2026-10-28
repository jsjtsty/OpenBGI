#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {

// Decoded audio: interleaved signed 16-bit samples, 1 or 2 channels.
struct Pcm {
    std::uint32_t sample_rate = 44100;
    std::uint32_t channels = 2;
    std::vector<std::int16_t> samples;
    bool loop = false;             // the file's own loop flag
    std::uint32_t loop_start = 0;  // frame the loop jumps back to

    std::uint32_t frames() const { return channels ? static_cast<std::uint32_t>(samples.size() / channels) : 0; }
};

// Decodes any audio file FFmpeg was built to read (see cmake/FFmpeg.cmake for the enabled formats): the
// container and codec are detected from the bytes. More than two channels are mixed down to stereo.
// `name` is the member name when known; its extension helps FFmpeg pick the container (".mpg").
Result<std::shared_ptr<Pcm>> decode_audio(ByteView file, const std::string& name = {});

// Decodes a "bw  " sound file: its 0x40-byte header (loop flag and start, analysis/BW格式.md) followed by a
// stream decode_audio understands (always Ogg Vorbis in the shipped game).
Result<std::shared_ptr<Pcm>> decode_bw(ByteView file);

// "Intro + loop" playback of two files: the result plays `intro`, then repeats `body` from its start.
Result<std::shared_ptr<Pcm>> join_intro_loop(const Pcm& intro, const Pcm& body);

// The licence FFmpeg was built under, e.g. "LGPL version 2.1 or later".
const char* ffmpeg_license();

} // namespace obgi
