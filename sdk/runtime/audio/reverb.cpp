#include "audio/reverb.h"

#include <algorithm>
#include <cmath>

namespace audio {

namespace {

constexpr float kPi = 3.14159265f;

float Db(int32_t hundredths) { return hundredths <= -10000 ? 0.0f : std::pow(10.0f, hundredths / 2000.0f); }

// the pole of a one-pole low pass y += (1 - d)(x - y) whose gain at frequency w (radians a sample) is r, r <= 1
float PoleFor(float r, float w) {
    r = std::clamp(r, 1e-4f, 1.0f);
    if (r > 0.9999f) return 0;
    float a = r * r - 1, b = 2 - 2 * r * r * std::cos(w);
    float s = std::sqrt(std::max(b * b - 4 * a * a, 0.0f));
    float d1 = (-b + s) / (2 * a), d2 = (-b - s) / (2 * a);  // the roots multiply to 1: one is inside
    float d = std::fabs(d1) < 1 ? d1 : d2;
    return std::clamp(d, 0.0f, 0.999f);
}

// the late reverb's lines and the diffusers' delays at full density, in ms: mutually prime at 48 kHz
constexpr float kLineMs[8] = {29.7f, 37.1f, 41.1f, 43.7f, 50.3f, 56.3f, 61.7f, 68.9f};
constexpr float kAllpassMs[4] = {4.76f, 3.59f, 12.73f, 9.30f};
// where the early reflections fall across their spread, and the side each favours
constexpr float kTapAt[6] = {0.0f, 0.13f, 0.29f, 0.47f, 0.71f, 1.0f};
constexpr float kTapPan[6] = {0.8f, -0.6f, 0.3f, -0.9f, 0.6f, -0.2f};

}  // namespace

Reverb::Reverb(uint32_t rate) : rate_(rate) {
    pre_.Size(uint32_t(0.45f * rate) + 2);  // reflections delay up to 0.3 s, reverb delay up to 0.1 s more
    Set(p_);
}

void Reverb::Set(const I3dl2& in) {
    I3dl2 p = in;
    // the I3DL2 ranges
    p.decay_time = std::clamp(p.decay_time, 0.1f, 20.0f);
    p.decay_hf_ratio = std::clamp(p.decay_hf_ratio, 0.1f, 2.0f);
    p.reflections_delay = std::clamp(p.reflections_delay, 0.0f, 0.3f);
    p.reverb_delay = std::clamp(p.reverb_delay, 0.0f, 0.1f);
    p.diffusion = std::clamp(p.diffusion, 0.0f, 100.0f);
    p.density = std::clamp(p.density, 0.0f, 100.0f);
    p.hf_reference = std::clamp(p.hf_reference, 20.0f, 20000.0f);
    bool resize = p.density != p_.density || len_[0] == 0;
    p_ = p;
    float fs = float(rate_), w = 2 * kPi * std::min(p.hf_reference, 0.45f * fs) / fs;

    room_ = Db(p.room);
    room_hf_ = Db(std::min(p.room_hf, 0));
    hf_in_ = 1 - PoleFor(0.5f, w);  // the shelf's corner: half way down at the reference
    refl_ = Db(p.reflections);
    late_ = Db(p.reverb);
    diff_ = 0.7f * p.diffusion / 100;
    refl_delay_ = uint32_t(p.reflections_delay * fs);
    late_delay_ = refl_delay_ + uint32_t(p.reverb_delay * fs);

    // reflections spread over the gap to the late reverb, at least 10 ms; their energy adds up to Reflections
    uint32_t spread = std::max(late_delay_ - refl_delay_, uint32_t(0.010f * fs));
    float sum = 0;
    for (int k = 0; k < kTaps; k++) sum += 1.0f / (1 + k);
    for (int k = 0; k < kTaps; k++) {
        tap_[k] = refl_delay_ + uint32_t(kTapAt[k] * spread);
        float g = std::sqrt(1.0f / (1 + k) / sum);  // later ones quieter
        float l = std::sqrt(0.5f * (1 - kTapPan[k])), r = std::sqrt(0.5f * (1 + kTapPan[k]));
        tap_gain_[k][0] = g * l;
        tap_gain_[k][1] = g * r;
    }

    // density sizes the room: the line lengths go from 40% to 100%
    float size = 0.4f + 0.6f * p.density / 100;
    if (resize) {
        for (int i = 0; i < kAllpasses; i++) ap_[i].Size(ap_len_[i] = std::max(uint32_t(kAllpassMs[i] * size * fs / 1000), 1u));
        for (int i = 0; i < kLines; i++) {
            len_[i] = std::max(uint32_t(kLineMs[i] * size * fs / 1000), 2u);
            line_[i].Size(len_[i] + 1);
            lp_[i] = 0;
        }
    }
    // each pass through a line loses 60 dB over the decay time, the high end over decay time * HF ratio
    float hf_time = p.decay_time * std::min(p.decay_hf_ratio, 1.0f);
    float mean = 0;
    for (int i = 0; i < kLines; i++) {
        gain_[i] = std::pow(10.0f, -3.0f * len_[i] / (p.decay_time * fs));
        float hf = std::pow(10.0f, -3.0f * len_[i] / (hf_time * fs));
        damp_[i] = PoleFor(hf / gain_[i], w);
        mean += float(len_[i]) / kLines;
    }
    // A unit impulse's energy, spread over the lines, decays by r a sample and leaves through the N line reads each
    // sample: mean length * (1 - r) of what is left. Over the whole tail that adds to 1 / (mean (1 - r)), half each
    // side; scaled so each side's impulse response carries Reverb's energy.
    float r = std::pow(10.0f, -6.0f / (p.decay_time * fs));
    late_ *= std::sqrt(2.0f * mean * (1 - r));
    tail_ = late_delay_ + uint32_t(p.decay_time * fs * 1.5f) + len_[kLines - 1];
}

void Reverb::Process(const float* in, float* out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; i++) {
        float x = in[i];
        idle_ = x != 0 ? 0 : idle_ + 1;
        // RoomHF: the high end down by room_hf_, then Room over all of it
        hf_in_state_ += (x - hf_in_state_) * hf_in_;
        x = (hf_in_state_ + room_hf_ * (x - hf_in_state_)) * room_;
        pre_.Write(x);

        float el = 0, er = 0;
        for (int k = 0; k < kTaps; k++) {
            float s = pre_.Read(tap_[k] + 1);
            el += s * tap_gain_[k][0];
            er += s * tap_gain_[k][1];
        }

        // the late reverb: diffused, into eight lines mixed by a Householder matrix
        float v = pre_.Read(late_delay_ + 1);
        for (int k = 0; k < kAllpasses; k++) {
            float d = ap_[k].Read(ap_len_[k]);
            float y = d - diff_ * v;
            ap_[k].Write(v + diff_ * y);
            v = y;
        }
        float o[kLines], sum = 0;
        for (int k = 0; k < kLines; k++) {
            float s = line_[k].Read(len_[k]) * gain_[k];
            lp_[k] += (1 - damp_[k]) * (s - lp_[k]);
            o[k] = lp_[k];
            sum += o[k];
        }
        sum *= 2.0f / kLines;
        constexpr float kIn = 0.35355339f;  // 1 / sqrt(8)
        float ll = 0, lr = 0;
        for (int k = 0; k < kLines; k++) {
            line_[k].Write(o[k] - sum + v * ((k & 2) ? -kIn : kIn));
            if (k & 1) lr += (k & 4) ? -o[k] : o[k];
            else ll += (k & 4) ? -o[k] : o[k];
        }
        out[i * 2] += refl_ * el + late_ * ll;
        out[i * 2 + 1] += refl_ * er + late_ * lr;
    }
}

}  // namespace audio
