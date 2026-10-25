**seconds 60:70** from present 3552: 4126 presents, frame p50 16.7 / p90 16.9 / p99 18.8 / max 143.3 ms, GPU busy 8.6 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 5fa2520ad95e, dropped: yes

```json
{"commit":"5fa2520ad95e","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3552,"ms":70003.173},"presents":4126,"frame_ms":{"p50":16.683,"p90":16.914,"p99":18.763,"max":143.273},"gpu_busy_ms_per_present":8.634,"worker_busy_pct":{"queue worker 0x0":14.159},"top_phases_ms_per_frame":{"VideoOut.Flip":12.819,"VideoOut.Flip.vblank_wait":11.509,"Driver.Packet":2.397,"Graphics.Draw":1.905,"Driver.Packet.draw":1.696,"Driver.Draw":1.663,"Graphics.Draw.draw_and_resource_release":1.549,"Driver.Draw.draw_enqueued":1.001},"trace_dropped":true,"trace_dropped_threads":{"queue worker 0x0":{"events_full_after_s":42.045,"events_lost":162856,"totals_full":false}},"trace":"aps5-trace-5fa2520ad95e-7364.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 4099: 143.3 ms at 9159 ms: Driver.Packet (queue worker 0x0) 99.2, Graphics.Draw (draw committer) 98.9, Graphics.Draw.draw_and_resource_release (draw committer) 98.7, Driver.Packet.pipeline_drain (queue worker 0x0) 98.5
- present 4504: 137.4 ms at 16100 ms: Driver.WorkerWait (queue worker 0x0) 90.6, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 90.6, Driver.Packet (queue worker 0x0) 45.7, Graphics.Draw (draw committer) 45.4
- present 5322: 132.5 ms at 30038 ms: Driver.WorkerWait (queue worker 0x0) 83.5, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 83.5, Driver.Packet (queue worker 0x0) 48.2, Graphics.Draw (draw committer) 47.8
- present 6554: 112.8 ms at 51034 ms: Graphics.Draw (draw committer) 43.1, Graphics.Draw.draw_and_resource_release (draw committer) 42.9, batch (gpu) 10.8, VideoOut.Flip (presenter) 0.9
- present 6141: 110.7 ms at 43960 ms: Graphics.Draw (draw committer) 42.4, Graphics.Draw.draw_and_resource_release (draw committer) 42.1, batch (gpu) 12.1, VideoOut.Flip (presenter) 1.6

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 16.9 | 0.6 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| queue worker 0x0 | 0.9 (6 %) | 8.6 (51 %) | 7.4 | 2.1 | Driver.Packet 0.9, Driver.Completion 0.0 |
| draw committer | 1.4 (8 %) | 0.0 (0 %) | 15.6 | 1.7 | Graphics.Draw 1.2, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.7 (4 %) | 12.1 (71 %) | 4.1 | 0.0 | VideoOut.Flip 12.8, Driver.Present 0.0 |
| gpu | 8.6 (51 %) | 0.0 (0 %) | 8.3 | 0.0 | batch 8.6 |

**Largest slices** (ms per frame, inclusive, all threads): VideoOut.Flip 12.8, VideoOut.Flip.vblank_wait 11.5, Driver.WorkerWait 8.6, Driver.WorkerWait.idle_and_completion_poll 8.6, batch 8.6, Graphics.Draw 1.2, VideoOut.Flip.present 1.0, Driver.Present 1.0, Driver.Packet 0.9, Graphics.Draw.draw_and_resource_release 0.9

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 101.5 ms at 16089 ms, present 4503
- Driver.WorkerWait (queue worker 0x0) 83.5 ms at 30038 ms, present 5321
- Driver.WorkerWait (queue worker 0x0) 75.9 ms at 36992 ms, present 5730
- Driver.WorkerWait (queue worker 0x0) 71.5 ms at 22961 ms, present 4906
- Driver.WorkerWait (queue worker 0x0) 54.6 ms at 9147 ms, present 4098
- Driver.WorkerWait (queue worker 0x0) 38.8 ms at 30171 ms, present 5322
- Driver.WorkerWait (queue worker 0x0) 38.6 ms at 9302 ms, present 4100
- Driver.WorkerWait (queue worker 0x0) 38.0 ms at 37114 ms, present 5732
- Driver.WorkerWait (queue worker 0x0) 37.9 ms at 16237 ms, present 4505
- Driver.WorkerWait (queue worker 0x0) 37.3 ms at 23083 ms, present 4908

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 6013: 36.9 ms at 51147 ms, present 6555: GPU no ranges; recorded by Graphics.Draw (draw committer) 42.7, Graphics.Draw.draw_and_resource_release (draw committer) 42.6, Resources.Prepare (draw committer) 0.0, Resources.Complete (draw committer) 0.0
- batch 3546: 36.6 ms at 30171 ms, present 5322: GPU no ranges; recorded by Graphics.Draw (draw committer) 46.5, Graphics.Draw.draw_and_resource_release (draw committer) 46.2, Resources.Prepare (draw committer) 0.1, Graphics.Draw.validate (draw committer) 0.0
- batch 6826: 36.2 ms at 58032 ms, present 6961: GPU no ranges; recorded by Graphics.Draw (draw committer) 41.7, Graphics.Draw.draw_and_resource_release (draw committer) 41.6, Resources.Prepare (draw committer) 0.0, Resources.Complete (draw committer) 0.0
- batch 1096: 35.8 ms at 9302 ms, present 4099: GPU no ranges; recorded by Graphics.Draw (draw committer) 98.4, Graphics.Draw.draw_and_resource_release (draw committer) 98.4, Resources.Complete (draw committer) 26.0, Resources.Prepare (draw committer) 1.3
- batch 4365: 35.8 ms at 37114 ms, present 5732: GPU no ranges; recorded by Graphics.Draw (draw committer) 43.8, Graphics.Draw.draw_and_resource_release (draw committer) 43.6, Resources.Prepare (draw committer) 0.0, Resources.Complete (draw committer) 0.0
