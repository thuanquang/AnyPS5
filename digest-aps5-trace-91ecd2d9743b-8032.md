**seconds 60:70** from present 3572: 4140 presents, frame p50 16.7 / p90 16.9 / p99 17.5 / max 126.9 ms, GPU busy 6.8 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 91ecd2d9743b, dropped: no

```json
{"commit":"91ecd2d9743b","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3572,"ms":70003.31},"presents":4140,"frame_ms":{"p50":16.683,"p90":16.909,"p99":17.505,"max":126.907},"gpu_busy_ms_per_present":6.833,"worker_busy_pct":{"queue worker 0x0":10.44},"top_phases_ms_per_frame":{"VideoOut.Flip":13.644,"VideoOut.Flip.vblank_wait":12.612,"Driver.Packet":1.762,"Graphics.Draw":1.413,"Graphics.Draw.draw_and_resource_release":1.26,"Driver.Packet.draw":1.195,"Driver.Draw":1.175,"VideoOut.Flip.present":0.939},"trace_dropped":false,"trace":"aps5-trace-91ecd2d9743b-8032.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 4138: 126.9 ms at 9443 ms: Driver.Packet (queue worker 0x0) 89.5, Graphics.Draw (draw committer) 89.4, Graphics.Draw.draw_and_resource_release (draw committer) 89.3, Driver.Packet.pipeline_drain (queue worker 0x0) 88.7
- present 5780: 115.1 ms at 37304 ms: Driver.WorkerWait (queue worker 0x0) 68.8, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 68.8, Driver.Packet (queue worker 0x0) 45.3, Graphics.Draw (draw committer) 44.9
- present 4541: 106.3 ms at 16316 ms: Driver.WorkerWait (queue worker 0x0) 58.5, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 58.5, Driver.Packet (queue worker 0x0) 46.8, Graphics.Draw (draw committer) 46.0
- present 7013: 101.7 ms at 58242 ms: Driver.WorkerWait (queue worker 0x0) 56.9, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 56.9, Driver.Packet (queue worker 0x0) 43.9, Graphics.Draw (draw committer) 43.3
- present 4949: 99.3 ms at 23240 ms: Driver.WorkerWait (queue worker 0x0) 53.3, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 53.3, Driver.Packet (queue worker 0x0) 45.0, Graphics.Draw (draw committer) 44.4

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 16.9 | 0.6 | Shader.ResolveAbi 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| queue worker 0x0 | 1.0 (6 %) | 15.1 (89 %) | 0.9 | 2.8 | Driver.Packet 1.0, Driver.Completion 0.0 |
| draw committer | 0.9 (5 %) | 0.0 (0 %) | 16.1 | 1.5 | Graphics.Draw 0.7, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.8 (5 %) | 12.9 (76 %) | 3.3 | 0.0 | VideoOut.Flip 13.6, Driver.Present 0.0 |
| gpu | 6.8 (40 %) | 0.0 (0 %) | 10.1 | 0.0 | batch 6.8 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.WorkerWait 15.1, Driver.WorkerWait.idle_and_completion_poll 15.1, VideoOut.Flip 13.6, VideoOut.Flip.vblank_wait 12.6, batch 6.8, Driver.Packet 1.0, VideoOut.Flip.present 0.9, Driver.Present 0.9, Graphics.Draw 0.7, Graphics.Draw.draw_and_resource_release 0.6

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 81.9 ms at 37291 ms, present 5779
- Driver.WorkerWait (queue worker 0x0) 71.0 ms at 16304 ms, present 4540
- Driver.WorkerWait (queue worker 0x0) 69.7 ms at 58229 ms, present 7012
- Driver.WorkerWait (queue worker 0x0) 66.3 ms at 51272 ms, present 6603
- Driver.WorkerWait (queue worker 0x0) 66.1 ms at 23227 ms, present 4948
- Driver.WorkerWait (queue worker 0x0) 65.8 ms at 44315 ms, present 6193
- Driver.WorkerWait (queue worker 0x0) 65.5 ms at 30219 ms, present 5361
- Driver.WorkerWait (queue worker 0x0) 49.5 ms at 9430 ms, present 4137
- Driver.WorkerWait (queue worker 0x0) 45.1 ms at 51422 ms, present 6605
- Driver.WorkerWait (queue worker 0x0) 39.6 ms at 44467 ms, present 6196

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 1134: 33.5 ms at 9570 ms, present 4139: GPU no ranges; recorded by Graphics.Draw (draw committer) 88.9, Graphics.Draw.draw_and_resource_release (draw committer) 88.8, Resources.Complete (draw committer) 23.5, Resources.Prepare (draw committer) 1.1
- batch 6884: 27.8 ms at 58344 ms, present 7014: GPU no ranges; recorded by Graphics.Draw (draw committer) 43.0, Graphics.Draw.draw_and_resource_release (draw committer) 43.0
- batch 6066: 27.8 ms at 51384 ms, present 6605: GPU no ranges; recorded by Graphics.Draw (draw committer) 43.3, Graphics.Draw.draw_and_resource_release (draw committer) 43.2, Resources.Prepare (draw committer) 0.0
- batch 5246: 27.7 ms at 44426 ms, present 6195: GPU no ranges; recorded by Graphics.Draw (draw committer) 43.1, Graphics.Draw.draw_and_resource_release (draw committer) 43.1, Resources.Prepare (draw committer) 0.0
- batch 4418: 27.5 ms at 37419 ms, present 5781: GPU no ranges; recorded by Graphics.Draw (draw committer) 44.3, Graphics.Draw.draw_and_resource_release (draw committer) 44.2, Graphics.Draw.vertex_upload (draw committer) 0.0, Resources.Prepare (draw committer) 0.0
