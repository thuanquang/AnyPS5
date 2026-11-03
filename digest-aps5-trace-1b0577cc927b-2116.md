**seconds 60:70** from present 3574: 4157 presents, frame p50 16.7 / p90 16.9 / p99 17.6 / max 92.3 ms, GPU busy 6.8 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 1b0577cc927b, dropped: no

```json
{"commit":"1b0577cc927b","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3574,"ms":70003.066},"presents":4157,"frame_ms":{"p50":16.683,"p90":16.921,"p99":17.572,"max":92.258},"gpu_busy_ms_per_present":6.829,"worker_busy_pct":{"queue worker 0x0":10.067},"top_phases_ms_per_frame":{"VideoOut.Flip":13.663,"VideoOut.Flip.vblank_wait":12.616,"Driver.Packet":1.692,"Graphics.Draw":1.347,"Graphics.Draw.draw_and_resource_release":1.196,"Driver.Packet.draw":1.184,"Driver.Draw":1.164,"VideoOut.Flip.present":0.965},"trace_dropped":false,"trace":"aps5-trace-1b0577cc927b-2116.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 4141: 92.3 ms at 9459 ms: Driver.Packet (queue worker 0x0) 55.1, Graphics.Draw (draw committer) 54.8, Graphics.Draw.draw_and_resource_release (draw committer) 54.7, Driver.Packet.pipeline_drain (queue worker 0x0) 54.4
- present 5789: 85.6 ms at 37304 ms: Driver.WorkerWait (queue worker 0x0) 67.8, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 67.8, Driver.Packet (queue worker 0x0) 16.9, Graphics.Draw (draw committer) 16.5
- present 7030: 79.8 ms at 58225 ms: Driver.WorkerWait (queue worker 0x0) 61.3, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 61.3, Driver.Packet (queue worker 0x0) 17.4, Graphics.Draw (draw committer) 17.2
- present 5372: 76.2 ms at 30263 ms: Driver.WorkerWait (queue worker 0x0) 58.2, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 58.2, Driver.Packet (queue worker 0x0) 17.1, Graphics.Draw (draw committer) 16.9
- present 4549: 72.0 ms at 16383 ms: Driver.WorkerWait (queue worker 0x0) 54.0, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 54.0, Driver.Packet (queue worker 0x0) 16.9, Graphics.Draw (draw committer) 16.5

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 16.8 | 0.6 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| queue worker 0x0 | 0.9 (5 %) | 15.1 (90 %) | 0.9 | 2.8 | Driver.Packet 0.9, Driver.Completion 0.0 |
| draw committer | 0.8 (5 %) | 0.0 (0 %) | 16.1 | 1.5 | Graphics.Draw 0.7, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.8 (5 %) | 12.9 (76 %) | 3.2 | 0.1 | VideoOut.Flip 13.7, Driver.Present 0.0 |
| gpu | 6.8 (41 %) | 0.0 (0 %) | 10.0 | 0.0 | batch 6.8 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.WorkerWait 15.1, Driver.WorkerWait.idle_and_completion_poll 15.1, VideoOut.Flip 13.7, VideoOut.Flip.vblank_wait 12.6, batch 6.8, VideoOut.Flip.present 1.0, Driver.Present 1.0, Driver.Packet 0.9, Graphics.Draw 0.7, Driver.Packet.draw 0.6

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 81.0 ms at 37291 ms, present 5788
- Driver.WorkerWait (queue worker 0x0) 73.2 ms at 58213 ms, present 7029
- Driver.WorkerWait (queue worker 0x0) 70.8 ms at 30251 ms, present 5371
- Driver.WorkerWait (queue worker 0x0) 66.5 ms at 16371 ms, present 4548
- Driver.WorkerWait (queue worker 0x0) 65.6 ms at 51339 ms, present 6621
- Driver.WorkerWait (queue worker 0x0) 64.4 ms at 44265 ms, present 6201
- Driver.WorkerWait (queue worker 0x0) 63.6 ms at 23293 ms, present 4958
- Driver.WorkerWait (queue worker 0x0) 48.9 ms at 9447 ms, present 4140
- Driver.WorkerWait (queue worker 0x0) 33.3 ms at 9552 ms, present 4142
- Vulkan.Acquire.acquire_fence_wait (presenter) 30.6 ms at 9552 ms, present 4142

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 1136: 31.3 ms at 9552 ms, present 4142: GPU no ranges; recorded by Graphics.Draw (draw committer) 54.6, Graphics.Draw.draw_and_resource_release (draw committer) 54.5, Resources.Complete (draw committer) 10.0, Resources.Prepare (draw committer) 1.2
- batch 5258: 28.4 ms at 44348 ms, present 6203: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.0, Graphics.Draw.draw_and_resource_release (draw committer) 15.9
- batch 2772: 28.3 ms at 23375 ms, present 4960: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.0, Graphics.Draw.draw_and_resource_release (draw committer) 16.0
- batch 1952: 28.3 ms at 16455 ms, present 4550: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.2, Graphics.Draw.draw_and_resource_release (draw committer) 16.1
- batch 6098: 28.2 ms at 51422 ms, present 6623: GPU no ranges; recorded by Graphics.Draw (draw committer) 15.9, Graphics.Draw.draw_and_resource_release (draw committer) 15.9
