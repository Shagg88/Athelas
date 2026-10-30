# D3D12: draws recorded on several cores (plan, 2026-10-30)

Why: The Third Age with second_core (job 111, XBE_PERF): 16,000-18,000 draws a frame; the render thread spends
15-17 ms in D3D12Backend::Draw (about 0.9 us a draw), the title thread waits 7 ms for it. GPU about 10% busy.
95% of draws take the repeat path. Job 112 adds the prep thread (Assemble on a third core).

## Stage 1 (next)
- Split Draw into ResolveDraw (serial: BindTargets, Transition, Ensure* caches, AllocSrvSlots + views, samplers,
  constant upload and last_cb_ dedupe, ext_ hooks, motion keys, Upload() reservations) -> a DrawPacket (PSO, root
  CBVs, table handles, viewport/scissor/blend factor/stencil ref, RTV/DSV, topology, vertex/index source pointers +
  reserved upload slots, counts) and EmitPacket(cl, packet, local dyn): memcpys + IASet* + Draw*.
- First check: EmitPacket(cl_) on the serial thread only (XBE_D3D12_WORKERS=1): same picture, same speed.
- Workers (N = 2-4): per-frame allocators + lists; packets cut into chunks of 256-512 draws between sync points;
  each chunk re-emits the full binding set at its start (a new list inherits nothing). Executed in order
  [glue cl_, chunk, chunk, glue, ...] at Submit.
- Sync points (flush pending chunks, glue list closed and reopened): Clear, Copy, Present, Flush, Sync,
  RecordLate, SetGamma, any draw whose resolve recorded commands (res_gen_ or dyn_.gen changed), ext_/capture,
  queries, ring overflow Submit(true), SRV ring wrap.
- Lifetime: packets own their Assembled; the render thread's done_ and Batch recycling wait for the chunk; Submit /
  ResetFrame join the workers before resetting allocators.
- Switches: XBE_D3D12_WORKERS=N (0 today's path), XBE_D3D12_CHUNK. Counters: chunks, sync points by cause,
  serial resolve ms, worker emit ms, join wait.
- Expected: about 5-6 of the 15-17 ms off the render thread. Check: screenshots equal at WORKERS=0 and 4.

## Stage 2
Pure parts of the non-repeat path (ClassifyShaders, VertexKey, MakePixelKey, hashing) ahead on the prep thread;
BindTargets skipped on repeats when rt/zs and their render states equal the last draw's.
