// Guard.cpp - code that may read memory which is no longer there, run so that the game never closes because of it
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// ROTKPlus reads the game's objects (characters, sound streams, textures, menu elements) that may have been freed in
// the meantime, and calls some of the game's functions on them. Those reads used to sit in __try/__except blocks; they
// now go through Guard() (ROTKPlus.h), which calls GuardedRun.
//
// MSVC: GuardedRun is a plain __try/__except around the call.
// g++ has no __try, so GuardedRun does by hand what MSVC's code does: it puts a frame of Windows' structured exception
// handling on the stack (the list at fs:[0]), calls the function and takes the frame off again. If the function, or the
// game's code it calls, raises an exception, Windows offers it to the frames from the newest: the game's own first (as
// with __try), then ours. GuardHandler then has Windows unwind the frames above it (RtlUnwind: the game's frames in
// between run their cleanup, and fs:[0] points at our frame again), puts the stack back where GuardedRun had it and
// goes on at GuardedRun_fault, which returns 0. This is the mechanism __try uses, so the code behaves the same in the
// two builds, on Windows and under Wine/Proton. Two other ways were set aside: checking every address with VirtualQuery
// before reading it does not cover the calls into the game, Direct3D and DirectSound that some of these blocks make (and
// costs a system call per read); a vectored exception handler runs before the game's own handlers and cannot unwind
// the game's frames that lie in between.
#include "ROTKPlus.h"

#if defined(_MSC_VER)

int __cdecl GuardedRun(GuardFn fn, void* ctx)
{
    __try { fn(ctx); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

#else

extern "C" EXCEPTION_DISPOSITION __cdecl GuardHandler(EXCEPTION_RECORD* er, void* frame, CONTEXT* ctx, void* dc)
    ASM_NAME(GuardHandler);
extern "C" void __cdecl GuardResume(void* frame) ASM_NAME(GuardResume);

// The frame on the stack, from esp up: [record: next, GuardHandler] edi esi ebx ebp, return address, fn, ctx. The
// record is 0x14 bytes below ebp.
extern "C" NAKED int __cdecl GuardedRun(GuardFn, void*)
{
    ASM_BEGIN
    ASM(push ebp)
    ASM(mov  ebp, esp)
    ASM(push ebx)
    ASM(push esi)
    ASM(push edi)
    ASM(push offset GuardHandler)
    ASM(push dword ptr fs:[0])
    ASM(mov  dword ptr fs:[0], esp)
    ASM(push dword ptr [ebp + 12])
    ASM(call dword ptr [ebp + 8])
    ASM(add  esp, 4)
    ASM(mov  eax, 1)
ASM(GuardedRun_out:)
    ASM(pop  dword ptr fs:[0])
    ASM(add  esp, 4)
    ASM(pop  edi)
    ASM(pop  esi)
    ASM(pop  ebx)
    ASM(pop  ebp)
    ASM(ret)
ASM(GuardedRun_fault:)
    ASM(xor  eax, eax)
    ASM(jmp  GuardedRun_out)
    ASM_END
}

// back into GuardedRun's frame, whose record is at frame (esp at the record, ebp 0x14 above it)
extern "C" NAKED void __cdecl GuardResume(void*)
{
    ASM_BEGIN
    ASM(mov  eax, dword ptr [esp + 4])
    ASM(mov  esp, eax)
    ASM(lea  ebp, [eax + 0x14])
    ASM(jmp  GuardedRun_fault)
    ASM_END
}

extern "C" EXCEPTION_DISPOSITION __cdecl GuardHandler(EXCEPTION_RECORD* er, void* frame, CONTEXT*, void*)
{
    if (er->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) return ExceptionContinueSearch;
    RtlUnwind(frame, nullptr, er, nullptr);
    GuardResume(frame);
    return ExceptionContinueSearch;   // (never reached)
}

#endif
