**seconds 60:70** from present 3572: 4139 presents, frame p50 16.7 / p90 16.9 / p99 17.4 / max 126.9 ms, GPU busy 6.8 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 91ecd2d9743b, dropped: no

```json
{"commit":"91ecd2d9743b","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3572,"ms":70003.123},"presents":4139,"frame_ms":{"p50":16.683,"p90":16.91,"p99":17.428,"max":126.883},"gpu_busy_ms_per_present":6.837,"worker_busy_pct":{"queue worker 0x0":10.513},"top_phases_ms_per_frame":{"VideoOut.Flip":13.645,"VideoOut.Flip.vblank_wait":12.619,"Driver.Packet":1.775,"Graphics.Draw":1.429,"Graphics.Draw.draw_and_resource_release":1.269,"Driver.Packet.draw":1.203,"Driver.Draw":1.183,"VideoOut.Flip.present":0.935},"trace_dropped":false,"trace":"aps5-trace-91ecd2d9743b-14216.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 4142: 126.9 ms at 9509 ms: Driver.Packet (queue worker 0x0) 90.0, Graphics.Draw (draw committer) 89.5, Graphics.Draw.draw_and_resource_release (draw committer) 89.4, Driver.Packet.pipeline_drain (queue worker 0x0) 89.0
- present 5362: 111.4 ms at 30213 ms: Driver.WorkerWait (queue worker 0x0) 67.3, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 67.3, Driver.Packet (queue worker 0x0) 43.1, Graphics.Draw (draw committer) 42.8
- present 4544: 105.3 ms at 16367 ms: Driver.WorkerWait (queue worker 0x0) 57.6, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 57.6, Driver.Packet (queue worker 0x0) 46.6, Graphics.Draw (draw committer) 46.2
- present 6190: 103.4 ms at 44261 ms: Driver.WorkerWait (queue worker 0x0) 59.5, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 59.5, Driver.Packet (queue worker 0x0) 42.9, Graphics.Draw (draw committer) 42.7
- present 7017: 103.3 ms at 58291 ms: Driver.WorkerWait (queue worker 0x0) 57.0, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 57.0, Driver.Packet (queue worker 0x0) 45.3, Graphics.Draw (draw committer) 44.6

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 16.9 | 0.6 | Shader.ResolveAbi 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| queue worker 0x0 | 1.0 (6 %) | 15.1 (89 %) | 0.9 | 2.9 | Driver.Packet 1.0, Driver.Completion 0.0 |
| draw committer | 0.8 (5 %) | 0.0 (0 %) | 16.1 | 1.5 | Graphics.Draw 0.7, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.8 (5 %) | 12.9 (76 %) | 3.3 | 0.0 | VideoOut.Flip 13.6, Driver.Present 0.0 |
| gpu | 6.8 (40 %) | 0.0 (0 %) | 10.1 | 0.0 | batch 6.8 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.WorkerWait 15.1, Driver.WorkerWait.idle_and_completion_poll 15.1, VideoOut.Flip 13.6, VideoOut.Flip.vblank_wait 12.6, batch 6.8, Driver.Packet 1.0, VideoOut.Flip.present 0.9, Driver.Present 0.9, Graphics.Draw 0.7, Graphics.Draw.draw_and_resource_release 0.6

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 78.7 ms at 30202 ms, present 5361
- Driver.WorkerWait (queue worker 0x0) 72.2 ms at 44248 ms, present 6189
- Driver.WorkerWait (queue worker 0x0) 70.4 ms at 37274 ms, present 5779
- Driver.WorkerWait (queue worker 0x0) 70.2 ms at 16354 ms, present 4543
- Driver.WorkerWait (queue worker 0x0) 70.0 ms at 58278 ms, present 7016
- Driver.WorkerWait (queue worker 0x0) 68.0 ms at 51322 ms, present 6605
- Driver.WorkerWait (queue worker 0x0) 64.4 ms at 23294 ms, present 4952
- Driver.WorkerWait (queue worker 0x0) 59.3 ms at 58427 ms, present 7018
- Driver.WorkerWait (queue worker 0x0) 51.5 ms at 37422 ms, present 5781
- Driver.WorkerWait (queue worker 0x0) 49.0 ms at 9496 ms, present 4141

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 1142: 33.4 ms at 9636 ms, present 4143: GPU no ranges; recorded by Graphics.Draw (draw committer) 89.1, Graphics.Draw.draw_and_resource_release (draw committer) 89.1, Resources.Complete (draw committer) 23.6, Resources.Prepare (draw committer) 1.2
- batch 6892: 28.2 ms at 58395 ms, present 7018: GPU no ranges; recorded by Graphics.Draw (draw committer) 44.3, Graphics.Draw.draw_and_resource_release (draw committer) 44.2, Resources.Prepare (draw committer) 0.0
- batch 4418: 27.9 ms at 37389 ms, present 5781: GPU no ranges; recorded by Graphics.Draw (draw committer) 42.7, Graphics.Draw.draw_and_resource_release (draw committer) 42.6, Resources.Prepare (draw committer) 0.0
- batch 3582: 27.7 ms at 30325 ms, present 5363: GPU no ranges; recorded by Graphics.Draw (draw committer) 42.2, Graphics.Draw.draw_and_resource_release (draw committer) 42.1, Graphics.Draw.vertex_upload (draw committer) 0.0
- batch 6070: 27.6 ms at 51435 ms, present 6607: GPU no ranges; recorded by Graphics.Draw (draw committer) 42.6, Graphics.Draw.draw_and_resource_release (draw committer) 42.5, Graphics.Draw.vertex_upload (draw committer) 0.0
