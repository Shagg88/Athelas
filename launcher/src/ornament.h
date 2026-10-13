// ROTK Fix Launcher - drawn ornaments: frames, dividers and buttons in the spirit of the
// three peoples' architecture (all original, drawn with lines and shapes, no artwork):
//   dwarf  - heavy bronze bands, chamfered stepped corners, rivets and lozenge studs
//   elf    - tapering lines that end in leaf scrolls
//   man    - carved stone borders, square corner blocks, arches and keystones
#pragma once
#include "imgui.h"

void orn_set_scale(float s);

// The whole window: carved stone border with corner blocks and a keystone (man)
void orn_window_frame(ImDrawList *dl, ImVec2 size);
// A heavy bronze frame with stepped corners and rivets around a panel (dwarf)
void orn_dwarf_frame(ImDrawList *dl, ImVec2 a, ImVec2 b, float thick);
// The chamfered fill behind a dwarf frame (same corner cut as orn_dwarf_frame)
void orn_dwarf_fill(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill);
// A small chamfered bronze plate (dwarf); fill = the plate's own colour
void orn_dwarf_plate(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill, bool lit);
// A heading's rule: tapering lines from x0 to x1 at y, ending in a leaf scroll (elf)
void orn_elf_rule(ImDrawList *dl, float x0, float x1, float y);
// An arched stone plaque button with a keystone (man); returns true when clicked
bool orn_arch_button(const char *label, ImVec2 size, ImFont *font, bool enabled);
// A chamfered bronze button (dwarf); returns true when clicked
bool orn_plate_button(const char *label, ImVec2 size, bool enabled);
