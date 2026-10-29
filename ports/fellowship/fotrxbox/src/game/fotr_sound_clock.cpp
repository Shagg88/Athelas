// FOTR Xbox: the sound clock DirectMusic keeps time by (#30: breaking things in Bag End goes quiet after a while).
//
// The game's sound effects are DirectMusic notes. DirectMusic's master clock (vtable 0x1C2EBC, GetTime 0x2CC4A)
// counts the audio chip's samples through the DSOUND library's DirectSoundGetSampleTime (0x16ED2B), which reads the
// APU's sample counter (0xFE80200C) once the library has set the chip up (0x18DFC0), else gives 0. The port's
// DirectSound is the host's, so the chip is never set up and the clock stood at 0: no note ever reached its end, so
// no note-off was sent and no voice was given back. The synth has 64 voices (the voice list at 0x2EAE30); once
// they were all held, new sounds had none and stayed silent (measured: 1 voice taken per sound, none freed; time
// 0 at every synth update).
//
// So the sample time is given here: 48 kHz samples of the game's own clock (it stops while the game is paused,
// as the chip's counter would), 32 bits, wrapping like the register.
#include <cstdint>

#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

X86_FUNC(sub_0016ED2B) {
    (void)base;
    static const int64_t start = kernel::GuestNs();
    uint64_t ns = uint64_t(kernel::GuestNs() - start);
    ::kernel::Return(ctx, uint32_t(ns / 1000000000ull * 48000ull + ns % 1000000000ull * 48000ull / 1000000000ull), 0);
}
