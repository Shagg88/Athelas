; Post-processing pass: sharpening, then brightness.
; DXVK only accepts pixel shader 1.x on a Direct3D 8 game, so this is ps_1_4.
;
; Stage 0 = the finished frame (point), texcoord 0 = this pixel.
; Stages 1-4 = the same frame (bilinear), texcoords 1-4 = half a pixel
;   diagonally away; each bilinear sample averages a 2x2 block, so the four
;   together are a soft 3x3 blur around the pixel.
; Stage 5 = 3D colour curve (brightness), indexed by the sharpened colour.
;
; c0 = LUT scale ((N-1)/N), c1 = LUT offset (0.5/N), c3 = -0.25, c4 = amount
ps_1_4
texld r0, t0
texld r1, t1
texld r2, t2
texld r3, t3
texld r4, t4
add r1, r1, r2
add r3, r3, r4
add r1, r1, r3
mad r1, r1, c3, r0        ; detail = pixel - blur
mad r0, r1, c4, r0        ; pixel + amount * detail
mad r0, r0, c0, c1        ; colour -> curve coordinates (clamped by the sampler)
phase
texld r5, r0
mov r0, r5
