; Same as movie.asm, then doubled: for HUD pictures drawn with MODULATE2X.
; Movie texture is bound on stages 0-4. c0-c3 = offsets of one movie pixel
; (+x, -x, +y, -y), c4 = -0.25, c5 = strength. v0 = the game's colour (fades).
ps_1_4
texcrd r0.rgb, t0
add r1.rgb, r0, c0
add r2.rgb, r0, c1
add r3.rgb, r0, c2
add r4.rgb, r0, c3
phase
texld r0, r0
texld r1, r1
texld r2, r2
texld r3, r3
texld r4, r4
add r1, r1, r2
add r3, r3, r4
add r1, r1, r3
mad r1, r1, c4, r0
mad r0, r1, c5, r0
mul r0, r0, v0
add r0, r0, r0
