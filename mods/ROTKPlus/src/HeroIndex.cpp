// HeroIndex.cpp - game functions that pick the hero with "index 0 -> player 1, otherwise -> player 2"
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// Many functions choose a player's hero like this: index 0 -> 0x47EF60 (hero of player 1), otherwise ->
// 0x47EF80 (hero of player 2). For the third player (index 2) they used the hero of player 2: kills by Gimli gave
// experience and statistics to Gandalf. Here the main functions are wrapped: if they concern an extra player
// (index 2 or 3), 0x47EF80 answers with that player's hero for the duration of the call (g_spawningP3 = index - 1,
// the same flag used when the extra players are created, see Coop.cpp).
//  - 0x40D110: experience and statistics of a kill; player index in EAX, two arguments on the stack.
//  - 0x417E70(character, x): called from 11 places; looks at the index at +0x690 of the character.
#include "ROTKPlus.h"

static void KillExpOrig() ASM_NAME(KillExpOrig);
static void HeroOfCharOrig() ASM_NAME(HeroOfCharOrig);

// trampolines: the first instructions copied, then the rest of the function
static NAKED void KillExpOrig()   // 0x40D110: sub esp,14h; mov edx,[573E48h]
{
    ASM_BEGIN
    ASM(sub  esp, 0x14)
    ASM(mov  edx, dword ptr ds:[0x573E48])
    ASM(push 0x40D119)
    ASM(ret)
    ASM_END
}

// The game's own credit of a kill (index in EAX, [esp+8] the victim): Players.cpp counts the kills its hits did not see
// (trolls and bosses are hit through calls without an attacker, 0x41FE56, 0x426CA5...: test of 01/11/2026, the trolls
// of the Palantirs were missing). All registers are kept.
static NAKED void KillExp()
{
    ASM_BEGIN
    ASM(pushad)
    ASM(push dword ptr [esp + 0x28]) // the victim (0x20 of pushad + 8)
    ASM(push eax)                    // the player's index
    ASM(call PlayersGameKill)
    ASM(add  esp, 8)
    ASM(popad)
    ASM(cmp  eax, 2)
    ASM(jb   KillExp_plain)
    ASM(cmp  eax, 3)
    ASM(ja   KillExp_plain)
    ASM(lea  edx, [eax - 1])
    ASM(mov  dword ptr [g_spawningP3], edx)
    ASM(push dword ptr [esp + 8])
    ASM(push dword ptr [esp + 8])
    ASM(call KillExpOrig)
    ASM(add  esp, 8)
    ASM(mov  dword ptr [g_spawningP3], 0)
    ASM(ret)
ASM(KillExp_plain:)
    ASM(jmp  KillExpOrig)
    ASM_END
}

static NAKED void HeroOfCharOrig()   // 0x417E70: push esi; push edi; mov edi,[esp+0Ch]
{
    ASM_BEGIN
    ASM(push esi)
    ASM(push edi)
    ASM(mov  edi, dword ptr [esp + 0x0C])
    ASM(push 0x417E76)
    ASM(ret)
    ASM_END
}

static NAKED void HeroOfChar()
{
    ASM_BEGIN
    ASM(mov  ecx, dword ptr [esp + 4])
    ASM(test ecx, ecx)
    ASM(jz   HeroOfChar_plain)
    ASM(mov  edx, dword ptr [ecx + 0x690])
    ASM(cmp  edx, 2)
    ASM(jb   HeroOfChar_plain)
    ASM(cmp  edx, 3)
    ASM(ja   HeroOfChar_plain)
    ASM(dec  edx)
    ASM(mov  dword ptr [g_spawningP3], edx)
    ASM(push dword ptr [esp + 8])
    ASM(push dword ptr [esp + 8])
    ASM(call HeroOfCharOrig)
    ASM(add  esp, 8)
    ASM(mov  dword ptr [g_spawningP3], 0)
    ASM(ret)
ASM(HeroOfChar_plain:)
    ASM(jmp  HeroOfCharOrig)
    ASM_END
}

bool HeroInstall()
{
    static const uint8_t killOld[] = { 0x83, 0xEC, 0x14, 0x8B, 0x15, 0x48, 0x3E, 0x57, 0x00 };
    static const uint8_t heroOld[] = { 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C };
    bool ok = true;
    ok &= PatchJmp(0x40D110, killOld, sizeof killOld, (const void*)&KillExp);
    ok &= PatchJmp(0x417E70, heroOld, sizeof heroOld, (const void*)&HeroOfChar);
    Log("hero of the extra players in index-based functions (experience, kills): %s", ok ? "active" : "NOT installed");
    return ok;
}
