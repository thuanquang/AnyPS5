**seconds 60:70** from present 3575: 4159 presents, frame p50 16.7 / p90 16.9 / p99 17.4 / max 92.5 ms, GPU busy 6.8 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 1b0577cc927b, dropped: no

```json
{"commit":"1b0577cc927b","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"60:70"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 60:70","first_present":3575,"ms":70003.079},"presents":4159,"frame_ms":{"p50":16.683,"p90":16.91,"p99":17.433,"max":92.502},"gpu_busy_ms_per_present":6.831,"worker_busy_pct":{"queue worker 0x0":10.011},"top_phases_ms_per_frame":{"VideoOut.Flip":13.686,"VideoOut.Flip.vblank_wait":12.65,"Driver.Packet":1.682,"Graphics.Draw":1.342,"Graphics.Draw.draw_and_resource_release":1.191,"Driver.Packet.draw":1.167,"Driver.Draw":1.148,"VideoOut.Flip.present":0.929},"trace_dropped":false,"trace":"aps5-trace-1b0577cc927b-8828.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 4144: 92.5 ms at 9493 ms: Driver.Packet (queue worker 0x0) 55.2, Graphics.Draw (draw committer) 55.1, Graphics.Draw.draw_and_resource_release (draw committer) 55.0, Driver.Packet.pipeline_drain (queue worker 0x0) 54.5
- present 5373: 81.8 ms at 30263 ms: Driver.WorkerWait (queue worker 0x0) 64.2, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 64.2, Driver.Packet (queue worker 0x0) 16.7, Graphics.Draw (draw committer) 16.3
- present 6203: 78.7 ms at 44261 ms: Driver.WorkerWait (queue worker 0x0) 60.4, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 60.4, Driver.Packet (queue worker 0x0) 17.4, Graphics.Draw (draw committer) 16.9
- present 5786: 78.3 ms at 37237 ms: Driver.WorkerWait (queue worker 0x0) 60.2, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 60.2, Driver.Packet (queue worker 0x0) 17.0, Graphics.Draw (draw committer) 16.8
- present 7036: 77.6 ms at 58291 ms: Driver.WorkerWait (queue worker 0x0) 60.0, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 60.0, Driver.Packet (queue worker 0x0) 16.7, Graphics.Draw (draw committer) 16.4

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 16.8 | 0.6 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| queue worker 0x0 | 0.9 (5 %) | 15.1 (90 %) | 0.9 | 2.8 | Driver.Packet 0.9, Driver.Completion 0.0 |
| draw committer | 0.8 (5 %) | 0.0 (0 %) | 16.0 | 1.5 | Graphics.Draw 0.7, Vulkan.DrawFromRecipe 0.1 |
| presenter | 0.8 (5 %) | 12.9 (77 %) | 3.1 | 0.0 | VideoOut.Flip 13.7, Driver.Present 0.0 |
| gpu | 6.8 (41 %) | 0.0 (0 %) | 10.0 | 0.0 | batch 6.8 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.WorkerWait 15.1, Driver.WorkerWait.idle_and_completion_poll 15.1, VideoOut.Flip 13.7, VideoOut.Flip.vblank_wait 12.7, batch 6.8, VideoOut.Flip.present 0.9, Driver.Present 0.9, Driver.Packet 0.9, Graphics.Draw 0.7, Driver.Packet.draw 0.6

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 77.0 ms at 30251 ms, present 5372
- Driver.WorkerWait (queue worker 0x0) 73.5 ms at 37224 ms, present 5785
- Driver.WorkerWait (queue worker 0x0) 73.0 ms at 58278 ms, present 7035
- Driver.WorkerWait (queue worker 0x0) 72.8 ms at 44248 ms, present 6202
- Driver.WorkerWait (queue worker 0x0) 71.2 ms at 16371 ms, present 4550
- Driver.WorkerWait (queue worker 0x0) 69.8 ms at 23310 ms, present 4961
- Driver.WorkerWait (queue worker 0x0) 68.1 ms at 51339 ms, present 6623
- Driver.WorkerWait (queue worker 0x0) 49.6 ms at 9479 ms, present 4143
- Driver.WorkerWait (queue worker 0x0) 36.5 ms at 23399 ms, present 4963
- Vulkan.Acquire.acquire_fence_wait (presenter) 34.0 ms at 23399 ms, present 4963

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 2776: 34.7 ms at 23399 ms, present 4963: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.7, Graphics.Draw.draw_and_resource_release (draw committer) 16.7
- batch 1140: 31.8 ms at 9585 ms, present 4145: GPU no ranges; recorded by Graphics.Draw (draw committer) 54.6, Graphics.Draw.draw_and_resource_release (draw committer) 54.6, Resources.Complete (draw committer) 10.1, Resources.Prepare (draw committer) 1.3
- batch 6100: 28.2 ms at 51426 ms, present 6625: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.4, Graphics.Draw.draw_and_resource_release (draw committer) 16.3
- batch 4424: 28.0 ms at 37315 ms, present 5787: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.2, Graphics.Draw.draw_and_resource_release (draw committer) 16.1
- batch 5258: 27.9 ms at 44340 ms, present 6204: GPU no ranges; recorded by Graphics.Draw (draw committer) 16.5, Graphics.Draw.draw_and_resource_release (draw committer) 16.4
