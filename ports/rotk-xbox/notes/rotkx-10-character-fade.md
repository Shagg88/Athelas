# ROTK Xbox: enemies vanishing and coming back (constant-alpha blending)

2026-10-14. The user's report: in Road to Isengard (ise01), under the bridge and near large trees with shadows, orcs
vanish and come back, sometimes several times. They still attack while invisible. Sometimes they're drawn black, and
Gandalf can look ghostly. It happens at 60 fps too. 17 F3 captures were taken (captures 112927 to 112930 and 113242 to
113254).

## What the captures show

- The scene's draw order:
  1. Level geometry: vs B0060921/B00609C1, triangle strips.
  2. Character shadow volumes: vs B0060B61, 8 × 3456 triangles, colour writes off.
  3. The shadow-darkening full-screen quad: FVF 0x43, z ALWAYS.
  4. The characters: vs B0060B41, triangle lists. Skinned on the CPU; the shader only lights them, with c146..c155
     and c96.
  5. Foliage and particle quads: vs B0060A51.
- The orc mesh is 2838 / 1662 / 48 / 498 / 96 (/ 150) vertices and Gandalf's is 1524 / 3147 / 981 / 2790 / 84 / 360 /
  255. Most opaque characters use blend off.
- In 14 of the 17 captures, one or two orcs are drawn after the opaque characters with blend **0x8003 / 0x8004**
  (D3DBLEND_CONSTANTALPHA / INVCONSTANTALPHA), which is a character fade.

## The colour the game fades with

The F3 capture doesn't log the blend colour, so a test-build log printed it in the harness for these runs:

- Road to Isengard (`ROTK_LEVEL=Ise01`), walking and fighting.
- Path of the Wizard, for comparison.

| Level | Blend colours |
|---|---|
| Road to Isengard | Black with the fade in the alpha: 0xF4000000, 0xFA000000, 0xEE000000 … 0xCA000000 |
| Path of the Wizard | Only one 4-vertex menu fade, 0xF9FFFFFF down to 0x15FFFFFF |

The faded orcs mostly have alphas from 0.8 to 0.98, so on the console they're nearly opaque.

## The bug (SDK, D3D12 backend)

- `Blend()` drew 0x8003/0x8004 with D3D12_BLEND_BLEND_FACTOR / INV_BLEND_FACTOR, and the blend factor was the whole
  blend colour.
- So the RGB weight came from the colour's RGB rather than its alpha. With a black colour that's src × 0 + dst × 1: a
  fading orc wasn't drawn at all, while the game still had it fighting.
- With a white colour (the menu fade), a fade showed fully opaque and then popped out.
- The reference renderer in the harness has always done this right (`k[3]`), which is why the harness pictures
  never showed it.

## Fix (SDK patch v15)

- `SetBlendFactorStencilRef`: when the draw's source or destination blend is 0x8003/0x8004, the blend factor is the
  colour's alpha in all four channels.
- This is skipped if the same draw also uses the constant colour (0x8001/0x8002), since one factor can't serve both.
  ROTK never does this.
- Checked by a MinGW syntax check only: the harness has no D3D12. It needs a run on Windows in Road to Isengard.

## Left as they are (faithful)

- Gandalf grey or ghostly in shade: in the reference renderer (harness, Ise01) he comes out the same grey in the
  shadow of the trees. His lighting is the CPU's spherical-harmonic light (c146..c155) for where he stands.
- Orcs looking black in shadow: their armour is near black and the reference renderer shows them the same way.
- If either still looks wrong on Windows after this fix, the next step is an F3 capture that logs the blend colour
  and the character constants c96 and c146..c155.

## Harness

- `ROTK_LEVEL=Ise01` reaches the Road to Isengard fights with the `work/pad_ise.txt` script (walk and attack in
  turns).
- The run folder's settings go in the instance folder (`runs/X/1/settings.toml`). Without them the 60 fps setting
  and video skipping are off and the run stays on the level's opening film.
