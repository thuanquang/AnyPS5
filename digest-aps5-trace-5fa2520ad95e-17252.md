**seconds 60:70** from present 3567: 4128 presents, frame p50 16.7 / p90 16.9 / p99 17.4 / max 157.8 ms, GPU busy 8.3 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 5fa2520ad95e, dropped: yes

```json
{"commit":"5fa2520ad95e","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3567,"ms":70003.27},"presents":4128,"frame_ms":{"p50":16.683,"p90":16.897,"p99":17.352,"max":157.814},"gpu_busy_ms_per_present":8.307,"worker_busy_pct":{"queue worker 0x0":15.679},"top_phases_ms_per_frame":{"VideoOut.Flip":12.392,"VideoOut.Flip.vblank_wait":11.311,"Driver.Packet":2.653,"Graphics.Draw":2.171,"Driver.Packet.draw":1.875,"Driver.Draw":1.833,"Graphics.Draw.draw_and_resource_release":1.749,"Driver.Draw.draw_enqueued":1.026},"trace_dropped":true,"trace_dropped_threads":{"queue worker 0x0":{"events_full_after_s":37.977,"events_lost":236477,"totals_full":false},"draw committer":{"events_full_after_s":61.383,"events_lost":43497,"totals_full":false}},"trace":"aps5-trace-5fa2520ad95e-17252.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 4540: 157.8 ms at 16400 ms: Driver.WorkerWait (queue worker 0x0) 95.3, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 95.3, Driver.Packet (queue worker 0x0) 61.7, Graphics.Draw (draw committer) 61.4
- present 5764: 147.9 ms at 37287 ms: Driver.WorkerWait (queue worker 0x0) 82.4, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 82.4, Driver.Packet (queue worker 0x0) 64.4, Graphics.Draw (draw committer) 63.0
- present 4136: 132.0 ms at 9493 ms: Driver.Packet (queue worker 0x0) 90.6, Graphics.Draw (draw committer) 90.3, Graphics.Draw.draw_and_resource_release (draw committer) 90.1, Driver.Packet.pipeline_drain (queue worker 0x0) 89.7
- present 4936: 124.2 ms at 23207 ms: Driver.WorkerWait (queue worker 0x0) 73.5, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 73.5, Driver.Packet (queue worker 0x0) 49.4, Graphics.Draw (draw committer) 49.2
- present 6995: 122.8 ms at 58208 ms: Graphics.Draw (draw committer) 51.4, Graphics.Draw.draw_and_resource_release (draw committer) 50.7, batch (gpu) 13.1, VideoOut.Flip (presenter) 1.0

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 16.9 | 0.6 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| queue worker 0x0 | 1.0 (6 %) | 7.7 (45 %) | 8.3 | 1.8 | Driver.Packet 1.0, Driver.Completion 0.0 |
| draw committer | 1.5 (9 %) | 0.0 (0 %) | 15.5 | 1.4 | Graphics.Draw 1.4, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.6 (3 %) | 11.8 (70 %) | 4.6 | 0.0 | VideoOut.Flip 12.4, Driver.Present 0.0 |
| gpu | 8.3 (49 %) | 0.0 (0 %) | 8.7 | 0.0 | batch 8.3 |

**Largest slices** (ms per frame, inclusive, all threads): VideoOut.Flip 12.4, VideoOut.Flip.vblank_wait 11.3, batch 8.3, Driver.WorkerWait 7.7, Driver.WorkerWait.idle_and_completion_poll 7.7, Graphics.Draw 1.4, Graphics.Draw.draw_and_resource_release 1.0, Driver.Packet 1.0, VideoOut.Flip.present 0.9, Driver.Present 0.9

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 105.2 ms at 16390 ms, present 4539
- Driver.WorkerWait (queue worker 0x0) 91.7 ms at 37278 ms, present 5763
- Driver.WorkerWait (queue worker 0x0) 83.2 ms at 23197 ms, present 4935
- Driver.WorkerWait (queue worker 0x0) 79.1 ms at 30238 ms, present 5349
- Driver.WorkerWait (queue worker 0x0) 52.8 ms at 9480 ms, present 4135
- Driver.WorkerWait (queue worker 0x0) 41.4 ms at 37435 ms, present 5764
- Driver.WorkerWait (queue worker 0x0) 41.1 ms at 16558 ms, present 4541
- Driver.WorkerWait (queue worker 0x0) 40.7 ms at 30369 ms, present 5351
- Driver.WorkerWait (queue worker 0x0) 40.1 ms at 23331 ms, present 4937
- Vulkan.Acquire.acquire_fence_wait (presenter) 37.8 ms at 37435 ms, present 5765

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 4401: 37.9 ms at 37435 ms, present 5764: GPU no ranges; recorded by Graphics.Draw (draw committer) 60.2, Graphics.Draw.draw_and_resource_release (draw committer) 59.9, Resources.Prepare (draw committer) 0.3, Resources.Complete (draw committer) 0.1
- batch 1950: 37.7 ms at 16558 ms, present 4541: GPU no ranges; recorded by Graphics.Draw (draw committer) 59.1, Graphics.Draw.draw_and_resource_release (draw committer) 58.8, Graphics.Draw.validate (draw committer) 0.0, Resources.Prepare (draw committer) 0.0
- batch 5220: 37.6 ms at 44368 ms, present 6174: GPU no ranges; recorded by Graphics.Draw (draw committer) 44.5, Graphics.Draw.draw_and_resource_release (draw committer) 44.2, Graphics.Draw.validate (draw committer) 0.0, Resources.Prepare (draw committer) 0.0
- batch 3572: 37.5 ms at 30370 ms, present 5351: GPU no ranges; recorded by Graphics.Draw (draw committer) 49.9, Graphics.Draw.draw_and_resource_release (draw committer) 49.7, Resources.Prepare (draw committer) 0.0, Vulkan.DrawFromRecipe (draw committer) 0.0
- batch 2743: 37.1 ms at 23331 ms, present 4937: GPU no ranges; recorded by Graphics.Draw (draw committer) 48.6, Graphics.Draw.draw_and_resource_release (draw committer) 48.5, Resources.Prepare (draw committer) 0.1, Resources.Complete (draw committer) 0.0
