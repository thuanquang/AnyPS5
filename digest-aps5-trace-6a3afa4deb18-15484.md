**seconds 60:70** from present 3560: 4126 presents, frame p50 16.7 / p90 16.9 / p99 17.5 / max 255.3 ms, GPU busy 6.9 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 6a3afa4deb18, dropped: no

```json
{"commit":"6a3afa4deb18","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3560,"ms":70003.448},"presents":4126,"frame_ms":{"p50":16.683,"p90":16.905,"p99":17.47,"max":255.302},"gpu_busy_ms_per_present":6.934,"worker_busy_pct":{"queue worker 0x0":11.084},"top_phases_ms_per_frame":{"VideoOut.Flip":13.681,"VideoOut.Flip.vblank_wait":12.641,"Driver.Packet":1.877,"Graphics.Draw":1.494,"Graphics.Draw.draw_and_resource_release":1.326,"Driver.Packet.draw":1.283,"Driver.Draw":1.261,"VideoOut.Flip.present":0.932},"trace_dropped":false,"trace":"aps5-trace-6a3afa4deb18-15484.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 5758: 255.3 ms at 37187 ms: Driver.WorkerWait (queue worker 0x0) 208.0, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 208.0, Driver.Packet (queue worker 0x0) 46.3, Graphics.Draw (draw committer) 45.9
- present 6581: 150.5 ms at 51285 ms: Driver.WorkerWait (queue worker 0x0) 108.1, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 108.1, Driver.Packet (queue worker 0x0) 41.2, Graphics.Draw (draw committer) 40.6
- present 4128: 125.2 ms at 9476 ms: Driver.Packet (queue worker 0x0) 87.8, Graphics.Draw (draw committer) 87.3, Graphics.Draw.draw_and_resource_release (draw committer) 87.2, Driver.Packet.pipeline_drain (queue worker 0x0) 86.9
- present 6988: 101.7 ms at 58225 ms: Driver.WorkerWait (queue worker 0x0) 56.5, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 56.5, Driver.Packet (queue worker 0x0) 44.1, Graphics.Draw (draw committer) 43.8
- present 4527: 97.1 ms at 16333 ms: Driver.WorkerWait (queue worker 0x0) 54.7, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 54.7, Driver.Packet (queue worker 0x0) 41.4, Graphics.Draw (draw committer) 41.0

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 17.0 | 0.5 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| queue worker 0x0 | 1.0 (6 %) | 15.0 (88 %) | 0.9 | 3.1 | Driver.Packet 1.0, Recorder.Reap 0.0 |
| draw committer | 0.9 (5 %) | 0.0 (0 %) | 16.1 | 1.6 | Graphics.Draw 0.7, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.8 (5 %) | 12.9 (76 %) | 3.3 | 0.0 | VideoOut.Flip 13.7, Driver.Present 0.0 |
| gpu | 6.9 (41 %) | 0.0 (0 %) | 10.0 | 0.0 | batch 6.9 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.WorkerWait 15.0, Driver.WorkerWait.idle_and_completion_poll 15.0, VideoOut.Flip 13.7, VideoOut.Flip.vblank_wait 12.6, batch 6.9, Driver.Packet 1.0, VideoOut.Flip.present 0.9, Driver.Present 0.9, Graphics.Draw 0.7, Driver.Packet.draw 0.7

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 219.9 ms at 37175 ms, present 5757
- Driver.WorkerWait (queue worker 0x0) 120.7 ms at 51272 ms, present 6580
- Driver.WorkerWait (queue worker 0x0) 69.4 ms at 58212 ms, present 6987
- Driver.WorkerWait (queue worker 0x0) 67.6 ms at 16320 ms, present 4526
- Driver.WorkerWait (queue worker 0x0) 65.4 ms at 30135 ms, present 5342
- Driver.WorkerWait (queue worker 0x0) 64.1 ms at 44215 ms, present 6164
- Driver.WorkerWait (queue worker 0x0) 62.9 ms at 23261 ms, present 4935
- Driver.WorkerWait (queue worker 0x0) 61.2 ms at 9666 ms, present 4130
- Driver.WorkerWait (queue worker 0x0) 54.7 ms at 58361 ms, present 6990
- Driver.WorkerWait (queue worker 0x0) 49.5 ms at 30274 ms, present 5344

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 1138: 33.2 ms at 9601 ms, present 4128: GPU no ranges; recorded by Graphics.Draw (draw committer) 86.9, Graphics.Draw.draw_and_resource_release (draw committer) 86.8, Resources.Complete (draw committer) 20.3, Resources.Prepare (draw committer) 1.3
- batch 6861: 29.4 ms at 58327 ms, present 6989: GPU no ranges; recorded by Graphics.Draw (draw committer) 43.1, Graphics.Draw.draw_and_resource_release (draw committer) 42.9, Resources.Prepare (draw committer) 0.0
- batch 4400: 29.3 ms at 37442 ms, present 5759: GPU no ranges; recorded by Graphics.Draw (draw committer) 43.6, Graphics.Draw.draw_and_resource_release (draw committer) 43.5, Resources.Prepare (draw committer) 0.0
- batch 5214: 29.1 ms at 44320 ms, present 6166: GPU no ranges; recorded by Graphics.Draw (draw committer) 39.5, Graphics.Draw.draw_and_resource_release (draw committer) 39.5, Resources.Prepare (draw committer) 0.0
- batch 6047: 29.1 ms at 51435 ms, present 6582: GPU no ranges; recorded by Graphics.Draw (draw committer) 40.1, Graphics.Draw.draw_and_resource_release (draw committer) 40.1, Resources.Prepare (draw committer) 0.0
