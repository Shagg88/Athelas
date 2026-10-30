#pragma once
// An I3DL2 environmental reverb: the room effect the console's audio DSP ran on the I3DL2 mix bin. Takes the mono
// send of every voice, gives stereo early reflections and late reverb, set by the I3DL2 listener values a title passes
// to IDirectSound_SetI3DL2Listener.
#include <cstdint>
#include <vector>

namespace audio {

// DSI3DL2LISTENER as the title lays it out: levels in hundredths of a dB, times in seconds
struct I3dl2 {
    int32_t room = -10000, room_hf = 0;
    float room_rolloff = 0, decay_time = 1.49f, decay_hf_ratio = 0.83f;
    int32_t reflections = -2602;
    float reflections_delay = 0.007f;
    int32_t reverb = 200;
    float reverb_delay = 0.011f, diffusion = 100, density = 100, hf_reference = 5000;
};

class Reverb {
public:
    explicit Reverb(uint32_t rate);
    void Set(const I3dl2& p);
    const I3dl2& Params() const { return p_; }
    bool Silent() const { return room_ <= 0 && idle_ > tail_; }
    // in: mono send, frames long; out: interleaved stereo, added to
    void Process(const float* in, float* out, uint32_t frames);

private:
    static constexpr int kLines = 8, kTaps = 6, kAllpasses = 4;
    struct Delay {
        std::vector<float> b;
        uint32_t at = 0;
        void Size(uint32_t n) { b.assign(n ? n : 1, 0.0f); at = 0; }
        // back is at most the size (the lengths are sized for their reads): a subtraction, not two divisions
        float Read(uint32_t back) const {
            uint32_t n = uint32_t(b.size());
            if (back > n) back %= n;
            return b[at >= back ? at - back : at + n - back];
        }
        void Write(float x) { b[at] = x; at = at + 1 == b.size() ? 0 : at + 1; }
    };
    uint32_t rate_;
    I3dl2 p_;
    // levels from the listener values
    float room_ = 0, room_hf_ = 1, refl_ = 0, late_ = 0, diff_ = 0;
    float hf_in_ = 0, hf_in_state_ = 0;  // RoomHF: a one-pole low pass on the send
    uint32_t refl_delay_ = 0, late_delay_ = 0, tail_ = 0, idle_ = 0;
    Delay pre_;  // the send, read at the reflections' taps and the late reverb's delay
    uint32_t tap_[kTaps] = {};
    float tap_gain_[kTaps][2] = {};
    Delay ap_[kAllpasses];
    uint32_t ap_len_[kAllpasses] = {};
    Delay line_[kLines];
    uint32_t len_[kLines] = {};
    float gain_[kLines] = {}, damp_[kLines] = {}, lp_[kLines] = {};
};

}  // namespace audio
