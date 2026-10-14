// ROTK Fix Launcher - drawn ornaments in the spirit of the architecture of the Free Peoples
// (all original, drawn with lines and shapes; no artwork):
//   men (Gondor, Gondolin) - a dark granite border with a bronze bead, Gothic quatrefoil
//                            medallions and a pointed arch gable
//   elves (Rivendell, Lothlorien, the Noldor) - slender bronze lines with notched corners,
//                            a small pointed arch, scrolled heading rules
#pragma once
#include "imgui.h"

void orn_set_scale(float s);

// The whole window: a granite border with quatrefoils and a Gothic gable (men)
void orn_window_frame(ImDrawList *dl, ImVec2 size);
// The settings panel: slender double lines with notched corners, a pointed arch crest (elves)
void orn_panel_frame(ImDrawList *dl, ImVec2 a, ImVec2 b);
// The panel's background, matching the frame's shape
void orn_panel_fill(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill);
// A small panel with a slender double edge (elves); lit = hovered
void orn_plate(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill, bool lit);
// A heading's rule: a tapering line from x0 to x1 at y, ending in a scroll (elves)
void orn_elf_rule(ImDrawList *dl, float x0, float x1, float y);
// An arched bronze plaque button with a boss at the top (men); returns true when clicked
bool orn_arch_button(const char *label, ImVec2 size, ImFont *font, bool enabled);
// A slender-edged button (elves); returns true when clicked
bool orn_plate_button(const char *label, ImVec2 size, bool enabled);
