**seconds 50:15** from present 2989: 814 presents, frame p50 16.7 / p90 25.9 / p99 37.6 / max 260.8 ms, GPU busy 4.6 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit a3314aea7dee, dropped: no

```json
{"commit":"a3314aea7dee","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"50:15"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 50:15","first_present":2989,"ms":15016.426},"presents":814,"frame_ms":{"p50":16.69,"p90":25.875,"p99":37.569,"max":260.792},"gpu_busy_ms_per_present":4.551,"worker_busy_pct":{"queue worker 0x0":19.858},"top_phases_ms_per_frame":{"VideoOut.Flip":11.307,"VideoOut.Flip.vblank_wait":9.845,"Driver.Packet":3.66,"Graphics.Draw":3.123,"Driver.Packet.pipeline_drain":2.82,"VideoOut.Flip.present":1.331,"Driver.Present":1.329,"Driver.Present.acquire_image":0.867},"trace_dropped":false,"trace":"aps5-trace-a3314aea7dee-9324.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 3515: 260.8 ms at 8892 ms: Driver.WorkerWait (queue worker 0x0) 260.6, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 260.5, Recorder.Sync (queue worker 0x0) 4.1, batch (gpu) 4.1
- present 3486: 95.1 ms at 8333 ms: Driver.Packet (queue worker 0x0) 62.8, Driver.Packet.draw (queue worker 0x0) 62.6, Driver.Draw (queue worker 0x0) 62.6, Graphics.Draw (draw committer) 62.2
- present 3482: 65.3 ms at 8225 ms: Driver.WorkerWait (queue worker 0x0) 34.9, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 34.9, Driver.Packet (queue worker 0x0) 29.5, Graphics.Draw (draw committer) 29.4
- present 3527: 59.7 ms at 9327 ms: Driver.Packet (queue worker 0x0) 33.1, Graphics.Draw (draw committer) 32.4, Driver.Packet.pipeline_drain (queue worker 0x0) 31.9, Driver.WorkerWait (queue worker 0x0) 26.5
- present 3667: 41.7 ms at 12145 ms: Driver.Packet (queue worker 0x0) 23.6, Driver.Packet.pipeline_drain (queue worker 0x0) 23.5, Graphics.Draw (draw committer) 18.8, Driver.WorkerWait (queue worker 0x0) 17.9

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 18.4 | 0.3 | Shader.ResolveAbi 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 18.4 | 0.0 | Shader.PrepareArtifact 0.0 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 18.4 | 0.0 | Vulkan.GraphicsPipelinePrebuild 0.0 |
| queue worker 0x0 | 3.2 (17 %) | 14.7 (80 %) | 0.5 | 1.7 | Driver.Packet 3.2, Driver.Completion 0.0 |
| draw committer | 2.4 (13 %) | 0.7 (4 %) | 15.3 | 0.9 | Graphics.Draw 2.7, Vulkan.DrawFromRecipe 0.4 |
| presenter | 0.4 (2 %) | 10.9 (59 %) | 7.1 | 0.0 | VideoOut.Flip 11.3, Driver.Present 0.0 |
| gpu | 4.5 (25 %) | 0.0 (0 %) | 13.9 | 0.0 | batch 4.5 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.WorkerWait 14.7, Driver.WorkerWait.idle_and_completion_poll 14.7, VideoOut.Flip 11.3, VideoOut.Flip.vblank_wait 9.8, batch 4.5, Driver.Packet 3.2, Driver.Packet.pipeline_drain 2.8, Graphics.Draw 2.7, VideoOut.Flip.present 1.3, Driver.Present 1.3

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 260.6 ms at 8892 ms, present 3514
- Driver.WorkerWait (queue worker 0x0) 48.1 ms at 8212 ms, present 3481
- Driver.WorkerWait (queue worker 0x0) 31.5 ms at 8333 ms, present 3485
- Driver.WorkerWait (queue worker 0x0) 28.4 ms at 8095 ms, present 3474
- Driver.WorkerWait (queue worker 0x0) 26.5 ms at 9327 ms, present 3526
- Driver.WorkerWait (queue worker 0x0) 24.3 ms at 12090 ms, present 3664
- Driver.WorkerWait (queue worker 0x0) 24.0 ms at 13774 ms, present 3728
- Driver.WorkerWait (queue worker 0x0) 22.6 ms at 11957 ms, present 3658
- Driver.WorkerWait (queue worker 0x0) 22.4 ms at 12025 ms, present 3661
- Driver.WorkerWait (queue worker 0x0) 22.2 ms at 12141 ms, present 3666

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 765: 9.3 ms at 8428 ms, present 3486: GPU no ranges; recorded by Graphics.Draw (draw committer) 4.8, Graphics.Draw.draw_and_resource_release (draw committer) 4.7, Recorder.Submit (draw committer) 0.0
- batch 504: 6.9 ms at 6127 ms, present 3356: GPU no ranges; recorded by Graphics.Draw (draw committer) 0.5, Graphics.Draw.draw_and_resource_release (draw committer) 0.3, Recorder.Submit (draw committer) 0.0, Vulkan.DrawFromRecipe (draw committer) 0.0
- batch 726: 6.7 ms at 7978 ms, present 3467: GPU no ranges; recorded by Graphics.Draw (draw committer) 0.3, Graphics.Draw.draw_and_resource_release (draw committer) 0.2, Vulkan.DrawFromRecipe (draw committer) 0.1, Graphics.DrawWithRecipe (draw committer) 0.1
- batch 732: 6.7 ms at 8028 ms, present 3470: GPU no ranges; recorded by Graphics.Draw (draw committer) 0.4, Graphics.Draw.draw_and_resource_release (draw committer) 0.3, Recorder.Submit (draw committer) 0.0
- batch 666: 6.7 ms at 7477 ms, present 3437: GPU no ranges; recorded by Graphics.Draw (draw committer) 0.2, Graphics.Draw.draw_and_resource_release (draw committer) 0.1, Vulkan.DrawFromRecipe (draw committer) 0.1, Graphics.DrawWithRecipe (draw committer) 0.1
