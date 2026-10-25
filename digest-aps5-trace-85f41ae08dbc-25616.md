**seconds 60:70** from present 3570: 4158 presents, frame p50 16.7 / p90 16.9 / p99 17.2 / max 103.3 ms, GPU busy 8.3 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 85f41ae08dbc, dropped: yes

```json
{"commit":"85f41ae08dbc","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3570,"ms":70003.271},"presents":4158,"frame_ms":{"p50":16.683,"p90":16.897,"p99":17.241,"max":103.336},"gpu_busy_ms_per_present":8.311,"worker_busy_pct":{"queue worker 0x0":13.588},"top_phases_ms_per_frame":{"VideoOut.Flip":12.836,"VideoOut.Flip.vblank_wait":11.768,"Driver.Packet":2.283,"Graphics.Draw":1.844,"Driver.Packet.draw":1.653,"Driver.Draw":1.617,"Graphics.Draw.draw_and_resource_release":1.479,"Driver.Draw.draw_enqueued":0.907},"trace_dropped":true,"trace_dropped_threads":{"queue worker 0x0":{"events_full_after_s":40.193,"events_lost":168926,"totals_full":false}},"trace":"aps5-trace-85f41ae08dbc-25616.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 4134: 103.3 ms at 9409 ms: Driver.Packet (queue worker 0x0) 57.7, Graphics.Draw (draw committer) 57.5, Graphics.Draw.draw_and_resource_release (draw committer) 56.6, Driver.Packet.pipeline_drain (queue worker 0x0) 54.7
- present 4534: 96.2 ms at 16216 ms: Driver.WorkerWait (queue worker 0x0) 83.4, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 83.4, batch (gpu) 16.2, Driver.Packet (queue worker 0x0) 11.7
- present 6605: 79.5 ms at 51135 ms: batch (gpu) 16.3, Graphics.Draw (draw committer) 9.6, Graphics.Draw.draw_and_resource_release (draw committer) 9.4, VideoOut.Flip (presenter) 0.9
- present 5775: 77.0 ms at 37154 ms: Driver.WorkerWait (queue worker 0x0) 65.0, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 65.0, batch (gpu) 16.2, Driver.Packet (queue worker 0x0) 11.1
- present 4135: 75.3 ms at 9513 ms: Driver.Packet (queue worker 0x0) 37.9, Graphics.Draw (draw committer) 37.6, Graphics.Draw.draw_and_resource_release (draw committer) 36.6, Driver.WorkerWait (queue worker 0x0) 36.5

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 16.8 | 0.6 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| queue worker 0x0 | 0.9 (5 %) | 8.2 (49 %) | 7.7 | 1.9 | Driver.Packet 0.9, Driver.Completion 0.0 |
| draw committer | 1.3 (8 %) | 0.0 (0 %) | 15.5 | 1.7 | Graphics.Draw 1.2, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.6 (4 %) | 12.2 (73 %) | 4.0 | 0.0 | VideoOut.Flip 12.8, Driver.Present 0.0 |
| gpu | 8.3 (49 %) | 0.0 (0 %) | 8.5 | 0.0 | batch 8.3 |

**Largest slices** (ms per frame, inclusive, all threads): VideoOut.Flip 12.8, VideoOut.Flip.vblank_wait 11.8, batch 8.3, Driver.WorkerWait 8.2, Driver.WorkerWait.idle_and_completion_poll 8.2, Graphics.Draw 1.2, Driver.Packet 0.9, Graphics.Draw.draw_and_resource_release 0.8, VideoOut.Flip.present 0.8, Driver.Present 0.8

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 94.0 ms at 16206 ms, present 4533
- Driver.WorkerWait (queue worker 0x0) 77.2 ms at 37142 ms, present 5774
- Driver.WorkerWait (queue worker 0x0) 74.3 ms at 23112 ms, present 4941
- Driver.WorkerWait (queue worker 0x0) 64.0 ms at 30168 ms, present 5360
- Driver.WorkerWait (queue worker 0x0) 55.1 ms at 9399 ms, present 4133
- Driver.WorkerWait (queue worker 0x0) 36.5 ms at 9513 ms, present 4135
- Driver.WorkerWait (queue worker 0x0) 34.6 ms at 30244 ms, present 5362
- Vulkan.Acquire.acquire_fence_wait (presenter) 33.0 ms at 9513 ms, present 4135
- Vulkan.Acquire.acquire_fence_wait (presenter) 32.8 ms at 44315 ms, present 6197
- Driver.WorkerWait (queue worker 0x0) 32.7 ms at 23198 ms, present 4943

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 1130: 34.0 ms at 9512 ms, present 4134: GPU no ranges; recorded by Graphics.Draw (draw committer) 54.9, Graphics.Draw.draw_and_resource_release (draw committer) 54.6, Resources.Complete (draw committer) 9.9, Resources.Prepare (draw committer) 2.5
- batch 5258: 33.6 ms at 44315 ms, present 6197: GPU no ranges; recorded by Graphics.Draw (draw committer) 10.4, Graphics.Draw.draw_and_resource_release (draw committer) 10.3, Resources.Prepare (draw committer) 0.0, Resources.Complete (draw committer) 0.0
- batch 3587: 32.8 ms at 30244 ms, present 5362: GPU no ranges; recorded by Graphics.Draw (draw committer) 10.2, Graphics.Draw.draw_and_resource_release (draw committer) 10.1, Graphics.Draw.validate (draw committer) 0.1, Resources.Prepare (draw committer) 0.0
- batch 6912: 31.8 ms at 58233 ms, present 7023: GPU no ranges; recorded by Graphics.Draw (draw committer) 9.6, Graphics.Draw.draw_and_resource_release (draw committer) 9.4, Resources.Prepare (draw committer) 0.0, Resources.Complete (draw committer) 0.0
- batch 2749: 30.5 ms at 23198 ms, present 4943: GPU no ranges; recorded by Graphics.Draw (draw committer) 10.1, Graphics.Draw.draw_and_resource_release (draw committer) 10.1, Resources.Complete (draw committer) 0.0, Resources.Prepare (draw committer) 0.0
