; Brightness: look each colour channel up in a 256-entry curve (the game's own
; gamma ramp, adjusted by the Brightness setting).
; DXVK only accepts pixel shader 1.x on a Direct3D 8 game, so this is ps_1_4.
; Stage 0 = the finished frame, stages 1-3 = the 256x1 curve (RGB = R/G/B ramps).
; c0 = 255/256, c1 = 0.5/256 (all components): maps 0..1 onto texel centres
; c2 = 0.5 (all components)
ps_1_4
texld r0, t0
mad r0, r0, c0, c1
mov r1, c2
mov r1.x, r0.x
mov r2, c2
mov r2.x, r0.y
mov r3, c2
mov r3.x, r0.z
phase
texld r1, r1
texld r2, r2
texld r3, r3
mov r0, c2
mov r0.x, r1.x
mov r0.y, r2.y
mov r0.z, r3.z
