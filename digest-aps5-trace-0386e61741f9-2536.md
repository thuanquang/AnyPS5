**seconds 60:70** from present 3573: 4139 presents, frame p50 16.7 / p90 16.9 / p99 17.4 / max 228.8 ms, GPU busy 6.9 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 0386e61741f9, dropped: no

```json
{"commit":"0386e61741f9","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3573,"ms":70003.273},"presents":4139,"frame_ms":{"p50":16.683,"p90":16.896,"p99":17.379,"max":228.83},"gpu_busy_ms_per_present":6.939,"worker_busy_pct":{"queue worker 0x0":11.263},"top_phases_ms_per_frame":{"VideoOut.Flip":13.508,"VideoOut.Flip.vblank_wait":12.474,"Driver.Packet":1.902,"Graphics.Draw":1.513,"Driver.Packet.draw":1.336,"Graphics.Draw.draw_and_resource_release":1.335,"Driver.Draw":1.313,"VideoOut.Flip.present":0.924},"trace_dropped":false,"trace":"aps5-trace-0386e61741f9-2536.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 4946: 228.8 ms at 23106 ms: Driver.WorkerWait (queue worker 0x0) 205.3, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 205.3, Driver.Packet (queue worker 0x0) 22.5, Graphics.Draw (draw committer) 21.7
- present 7015: 132.5 ms at 58242 ms: Driver.WorkerWait (queue worker 0x0) 113.7, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 113.7, Driver.Packet (queue worker 0x0) 17.7, Graphics.Draw (draw committer) 17.2
- present 5766: 104.5 ms at 37121 ms: Driver.WorkerWait (queue worker 0x0) 84.9, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 84.9, Driver.Packet (queue worker 0x0) 18.5, Graphics.Draw (draw committer) 18.0
- present 6188: 103.3 ms at 44261 ms: Driver.WorkerWait (queue worker 0x0) 84.6, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 84.6, Driver.Packet (queue worker 0x0) 17.7, Graphics.Draw (draw committer) 17.4
- present 5359: 101.8 ms at 30214 ms: Driver.WorkerWait (queue worker 0x0) 83.3, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 83.3, Driver.Packet (queue worker 0x0) 17.4, Graphics.Draw (draw committer) 16.9

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 16.9 | 0.5 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| queue worker 0x0 | 1.0 (6 %) | 14.9 (88 %) | 0.9 | 3.1 | Driver.Packet 1.0, Recorder.Reap 0.0 |
| draw committer | 0.9 (5 %) | 0.0 (0 %) | 16.0 | 1.6 | Graphics.Draw 0.8, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.8 (5 %) | 12.7 (75 %) | 3.4 | 0.0 | VideoOut.Flip 13.5, Driver.Present 0.0 |
| gpu | 6.9 (41 %) | 0.0 (0 %) | 10.0 | 0.0 | batch 6.9 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.WorkerWait 14.9, Driver.WorkerWait.idle_and_completion_poll 14.9, VideoOut.Flip 13.5, VideoOut.Flip.vblank_wait 12.5, batch 6.9, Driver.Packet 1.0, VideoOut.Flip.present 0.9, Driver.Present 0.9, Graphics.Draw 0.8, Driver.Packet.draw 0.7

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 217.8 ms at 23094 ms, present 4945
- Driver.WorkerWait (queue worker 0x0) 126.1 ms at 58229 ms, present 7014
- Driver.WorkerWait (queue worker 0x0) 97.2 ms at 37108 ms, present 5765
- Driver.WorkerWait (queue worker 0x0) 96.8 ms at 44249 ms, present 6187
- Driver.WorkerWait (queue worker 0x0) 95.3 ms at 30202 ms, present 5358
- Driver.WorkerWait (queue worker 0x0) 67.3 ms at 16220 ms, present 4538
- Driver.WorkerWait (queue worker 0x0) 67.3 ms at 51239 ms, present 6600
- Driver.WorkerWait (queue worker 0x0) 51.6 ms at 9330 ms, present 4132
- Driver.WorkerWait (queue worker 0x0) 36.7 ms at 58374 ms, present 7016
- Driver.WorkerWait (queue worker 0x0) 36.0 ms at 30315 ms, present 5360

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 6892: 34.7 ms at 58374 ms, present 7016: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.7, Graphics.Draw.draw_and_resource_release (draw committer) 16.7, Graphics.Draw.validate (draw committer) 0.0, Resources.Prepare (draw committer) 0.0
- batch 3577: 33.6 ms at 30316 ms, present 5360: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.5, Graphics.Draw.draw_and_resource_release (draw committer) 16.4, Resources.Prepare (draw committer) 0.0
- batch 1122: 31.4 ms at 9438 ms, present 4133: GPU no ranges; recorded by Graphics.Draw (draw committer) 54.6, Graphics.Draw.draw_and_resource_release (draw committer) 54.5, Resources.Complete (draw committer) 9.9, Resources.Prepare (draw committer) 1.2
- batch 6063: 29.7 ms at 51325 ms, present 6602: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.6, Graphics.Draw.draw_and_resource_release (draw committer) 16.4, Resources.Prepare (draw committer) 0.0, Vulkan.DrawFromRecipe (draw committer) 0.0
- batch 2750: 29.6 ms at 23335 ms, present 4947: GPU no ranges; recorded by Graphics.Draw (draw committer) 19.4, Graphics.Draw.draw_and_resource_release (draw committer) 19.2, Resources.Prepare (draw committer) 0.0
