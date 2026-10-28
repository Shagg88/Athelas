# AI upscale pack: the full run (decided with Shagg, 2026-10-23)

- Upscale ALL dumped textures of the three Xbox games (rxpack 2147, fxpack 1522, ttpack 1735 = 5404), including the
  628 that have library (CC0) replacements, so either can be used for those.
- Method as the sample (AI_UPSCALE_SAMPLE.html): Real-ESRGAN x4plus, 16 px padding (wrapped for kind=surface),
  colours under clear pixels bled, alpha upscaled separately, then colorfix.py (each 4x4 block back to the original
  pixel's average colour).
- Run on Shagg's RTX 3080 Ti (portable Real-ESRGAN, ~10-20 min); prep and finishing here.
- Output: its own pack folder per game (textures\replace\ai), separate from the library pack, with install /
  remove scripts, so the player can pick AI or library for the 628. Private: never in a release or on GitHub.
- Review page for the doubtful kinds (fonts / HUD text, fine regular weaves, lighting / bump textures).
- Model settled: Real-ESRGAN x4plus (Shagg, 2026-10-23); no comparison with other models wanted.
