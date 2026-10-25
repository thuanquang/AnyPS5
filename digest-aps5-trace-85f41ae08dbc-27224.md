**seconds 60:70** from present 3573: 4157 presents, frame p50 16.7 / p90 16.9 / p99 17.4 / max 97.2 ms, GPU busy 8.0 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 85f41ae08dbc, dropped: yes

```json
{"commit":"85f41ae08dbc","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3573,"ms":70003.273},"presents":4157,"frame_ms":{"p50":16.684,"p90":16.91,"p99":17.373,"max":97.168},"gpu_busy_ms_per_present":8.044,"worker_busy_pct":{"queue worker 0x0":11.491},"top_phases_ms_per_frame":{"VideoOut.Flip":13.362,"VideoOut.Flip.vblank_wait":12.38,"Driver.Packet":1.931,"Graphics.Draw":1.539,"Driver.Packet.draw":1.39,"Driver.Draw":1.363,"Graphics.Draw.draw_and_resource_release":1.239,"Driver.Draw.draw_enqueued":0.808},"trace_dropped":true,"trace_dropped_threads":{"queue worker 0x0":{"events_full_after_s":57.427,"events_lost":57660,"totals_full":false}},"trace":"aps5-trace-85f41ae08dbc-27224.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 4546: 97.2 ms at 16350 ms: Driver.WorkerWait (queue worker 0x0) 82.3, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 82.3, batch (gpu) 14.5, Driver.Packet (queue worker 0x0) 13.9
- present 4138: 89.0 ms at 9426 ms: Driver.Packet (queue worker 0x0) 50.6, Graphics.Draw (draw committer) 50.4, Graphics.Draw.draw_and_resource_release (draw committer) 50.2, Driver.Packet.pipeline_drain (queue worker 0x0) 49.9
- present 5781: 79.6 ms at 37221 ms: Driver.WorkerWait (queue worker 0x0) 67.7, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 67.7, batch (gpu) 17.1, Recorder.Sync (queue worker 0x0) 11.2
- present 7031: 75.7 ms at 58275 ms: batch (gpu) 15.6, Graphics.Draw (draw committer) 10.8, Graphics.Draw.draw_and_resource_release (draw committer) 10.5, VideoOut.Flip (presenter) 0.8
- present 6197: 73.7 ms at 44228 ms: Driver.WorkerWait (queue worker 0x0) 61.7, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 61.7, batch (gpu) 12.2, Driver.Packet (queue worker 0x0) 10.9

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 16.8 | 0.5 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| queue worker 0x0 | 0.8 (5 %) | 12.2 (72 %) | 3.9 | 2.9 | Driver.Packet 0.8, VideoOut.Readiness 0.0 |
| draw committer | 0.8 (5 %) | 0.0 (0 %) | 16.0 | 1.9 | Graphics.Draw 0.7, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.5 (3 %) | 12.8 (76 %) | 3.5 | 0.0 | VideoOut.Flip 13.4, Driver.Present 0.0 |
| gpu | 8.0 (48 %) | 0.0 (0 %) | 8.8 | 0.0 | batch 8.0 |

**Largest slices** (ms per frame, inclusive, all threads): VideoOut.Flip 13.4, VideoOut.Flip.vblank_wait 12.4, Driver.WorkerWait 12.2, Driver.WorkerWait.idle_and_completion_poll 12.2, batch 8.0, Driver.Packet 0.8, VideoOut.Flip.present 0.8, Driver.Present 0.8, Graphics.Draw 0.7, Driver.Packet.draw 0.6

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 94.6 ms at 16337 ms, present 4545
- Driver.WorkerWait (queue worker 0x0) 80.0 ms at 37208 ms, present 5780
- Driver.WorkerWait (queue worker 0x0) 74.0 ms at 44215 ms, present 6196
- Driver.WorkerWait (queue worker 0x0) 68.8 ms at 30301 ms, present 5370
- Driver.WorkerWait (queue worker 0x0) 67.5 ms at 51273 ms, present 6615
- Driver.WorkerWait (queue worker 0x0) 67.3 ms at 23227 ms, present 4952
- Driver.WorkerWait (queue worker 0x0) 49.4 ms at 9414 ms, present 4137
- Driver.WorkerWait (queue worker 0x0) 34.4 ms at 23306 ms, present 4954
- Driver.WorkerWait (queue worker 0x0) 33.6 ms at 9515 ms, present 4139
- Driver.WorkerWait (queue worker 0x0) 32.5 ms at 44301 ms, present 6198

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 2764: 32.4 ms at 23306 ms, present 4954: GPU no ranges; recorded by Graphics.Draw (draw committer) 10.1, Graphics.Draw.draw_and_resource_release (draw committer) 10.0, Resources.Prepare (draw committer) 0.0, Resources.Complete (draw committer) 0.0
- batch 1132: 32.0 ms at 9515 ms, present 4138: GPU no ranges; recorded by Graphics.Draw (draw committer) 49.9, Graphics.Draw.draw_and_resource_release (draw committer) 49.8, Resources.Complete (draw committer) 8.6, Resources.Prepare (draw committer) 1.2
- batch 5254: 30.6 ms at 44301 ms, present 6198: GPU no ranges; recorded by Graphics.Draw (draw committer) 10.3, Graphics.Draw.draw_and_resource_release (draw committer) 10.1, Resources.Prepare (draw committer) 0.0, Resources.Complete (draw committer) 0.0
- batch 3600: 28.6 ms at 30384 ms, present 5372: GPU no ranges; recorded by Graphics.Draw (draw committer) 11.7, Graphics.Draw.draw_and_resource_release (draw committer) 11.5, Resources.Prepare (draw committer) 0.0, Resources.Complete (draw committer) 0.0
- batch 6092: 28.4 ms at 51357 ms, present 6617: GPU no ranges; recorded by Graphics.Draw (draw committer) 13.5, Graphics.Draw.draw_and_resource_release (draw committer) 13.2, Resources.Prepare (draw committer) 0.0, Resources.Complete (draw committer) 0.0
