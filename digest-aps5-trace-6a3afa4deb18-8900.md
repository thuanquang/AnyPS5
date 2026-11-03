**seconds 60:70** from present 3570: 4121 presents, frame p50 16.7 / p90 16.9 / p99 17.5 / max 262.5 ms, GPU busy 6.9 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 6a3afa4deb18, dropped: no

```json
{"commit":"6a3afa4deb18","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3570,"ms":70003.278},"presents":4121,"frame_ms":{"p50":16.683,"p90":16.906,"p99":17.482,"max":262.486},"gpu_busy_ms_per_present":6.921,"worker_busy_pct":{"queue worker 0x0":11.245},"top_phases_ms_per_frame":{"VideoOut.Flip":13.657,"VideoOut.Flip.vblank_wait":12.592,"Driver.Packet":1.907,"Graphics.Draw":1.515,"Graphics.Draw.draw_and_resource_release":1.345,"Driver.Packet.draw":1.301,"Driver.Draw":1.279,"VideoOut.Flip.present":0.946},"trace_dropped":false,"trace":"aps5-trace-6a3afa4deb18-8900.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 5368: 262.5 ms at 30397 ms: Driver.WorkerWait (queue worker 0x0) 207.4, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 207.4, Driver.Packet (queue worker 0x0) 54.2, Graphics.Draw (draw committer) 53.9
- present 6185: 153.3 ms at 44428 ms: Driver.WorkerWait (queue worker 0x0) 107.9, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 107.9, Driver.Packet (queue worker 0x0) 44.2, Graphics.Draw (draw committer) 43.7
- present 6593: 131.2 ms at 51385 ms: Driver.WorkerWait (queue worker 0x0) 85.6, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 85.6, Driver.Packet (queue worker 0x0) 44.5, Graphics.Draw (draw committer) 44.0
- present 4144: 130.8 ms at 9576 ms: Driver.Packet (queue worker 0x0) 92.0, Graphics.Draw (draw committer) 91.7, Graphics.Draw.draw_and_resource_release (draw committer) 91.6, Driver.Packet.pipeline_drain (queue worker 0x0) 91.1
- present 4548: 103.2 ms at 16467 ms: Driver.WorkerWait (queue worker 0x0) 53.3, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 53.3, Driver.Packet (queue worker 0x0) 48.7, Graphics.Draw (draw committer) 48.4

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 17.0 | 0.5 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| queue worker 0x0 | 1.0 (6 %) | 15.0 (88 %) | 0.9 | 3.1 | Driver.Packet 1.0, Driver.Completion 0.0 |
| draw committer | 0.9 (5 %) | 0.0 (0 %) | 16.1 | 1.6 | Graphics.Draw 0.8, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.8 (5 %) | 12.9 (76 %) | 3.3 | 0.0 | VideoOut.Flip 13.7, Driver.Present 0.0 |
| gpu | 6.9 (41 %) | 0.0 (0 %) | 10.1 | 0.0 | batch 6.9 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.WorkerWait 15.0, Driver.WorkerWait.idle_and_completion_poll 15.0, VideoOut.Flip 13.7, VideoOut.Flip.vblank_wait 12.6, batch 6.9, Driver.Packet 1.0, VideoOut.Flip.present 0.9, Driver.Present 0.9, Graphics.Draw 0.8, Driver.Packet.draw 0.7

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 220.3 ms at 30384 ms, present 5367
- Driver.WorkerWait (queue worker 0x0) 119.9 ms at 44416 ms, present 6184
- Driver.WorkerWait (queue worker 0x0) 98.1 ms at 51372 ms, present 6592
- Driver.WorkerWait (queue worker 0x0) 67.4 ms at 58380 ms, present 7002
- Driver.WorkerWait (queue worker 0x0) 66.9 ms at 16454 ms, present 4547
- Driver.WorkerWait (queue worker 0x0) 65.6 ms at 37426 ms, present 5773
- Driver.WorkerWait (queue worker 0x0) 63.7 ms at 23361 ms, present 4954
- Driver.WorkerWait (queue worker 0x0) 55.9 ms at 23506 ms, present 4956
- Driver.WorkerWait (queue worker 0x0) 55.7 ms at 58527 ms, present 7004
- Driver.WorkerWait (queue worker 0x0) 54.9 ms at 37571 ms, present 5775

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 1150: 33.2 ms at 9707 ms, present 4144: GPU no ranges; recorded by Graphics.Draw (draw committer) 91.0, Graphics.Draw.draw_and_resource_release (draw committer) 91.0, Resources.Complete (draw committer) 20.4, Resources.Prepare (draw committer) 1.3
- batch 4412: 29.4 ms at 37537 ms, present 5775: GPU no ranges; recorded by Graphics.Draw (draw committer) 43.4, Graphics.Draw.draw_and_resource_release (draw committer) 43.4, Resources.Prepare (draw committer) 0.0
- batch 6052: 29.4 ms at 51516 ms, present 6594: GPU no ranges; recorded by Graphics.Draw (draw committer) 43.5, Graphics.Draw.draw_and_resource_release (draw committer) 43.4, Resources.Prepare (draw committer) 0.0, Graphics.Draw.validate (draw committer) 0.0
- batch 5235: 29.2 ms at 44581 ms, present 6186: GPU no ranges; recorded by Graphics.Draw (draw committer) 43.2, Graphics.Draw.draw_and_resource_release (draw committer) 43.1, Resources.Prepare (draw committer) 0.0, Vulkan.DrawFromRecipe (draw committer) 0.0
- batch 6872: 29.2 ms at 58493 ms, present 7004: GPU no ranges; recorded by Graphics.Draw (draw committer) 44.1, Graphics.Draw.draw_and_resource_release (draw committer) 44.0, Resources.Prepare (draw committer) 0.0, Graphics.Draw.validate (draw committer) 0.0
