**seconds 50:15** from present 2989: 812 presents, frame p50 16.7 / p90 26.1 / p99 29.2 / max 321.6 ms, GPU busy 4.7 ms per present
AMD Radeon RX 6700 XT, AMD proprietary driver 26.10.1 (AMD proprietary shader compiler), Windows 10.0.26200, 1920x1080, commit 2742004ed28f, dropped: no

```json
{"commit":"2742004ed28f","dirty":false,"env":{"APS5_NO_PAD_INPUT":"1","APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_SECONDS":"50:15"},"gpu":"AMD Radeon RX 6700 XT","driver":"AMD proprietary driver 26.10.1 (AMD proprietary shader compiler)","os":"Windows 10.0.26200","cpu":"AMD Ryzen 5 5600X 6-Core Processor","cpu_threads":12,"memory_mib":32690,"title_id":"PPSA14363","title":"METAL GEAR SOLID - Master Collection Version","content_version":"03.000.000","resolution":"1920x1080","window":{"spec":"seconds 50:15","first_present":2989,"ms":15015.062},"presents":812,"frame_ms":{"p50":16.687,"p90":26.12,"p99":29.24,"max":321.622},"gpu_busy_ms_per_present":4.681,"worker_busy_pct":{"queue worker 0x0":20.081},"top_phases_ms_per_frame":{"VideoOut.Flip":11.359,"VideoOut.Flip.vblank_wait":9.828,"Driver.Packet":3.71,"Graphics.Draw":3.164,"Driver.Packet.pipeline_drain":2.867,"VideoOut.Flip.present":1.406,"Driver.Present":1.404,"Driver.Present.acquire_image":0.911},"trace_dropped":false,"trace":"aps5-trace-2742004ed28f-17072.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 3519: 321.6 ms at 8960 ms: Driver.WorkerWait (queue worker 0x0) 321.4, Driver.WorkerWait.idle_and_completion_poll (queue worker 0x0) 321.4, batch (gpu) 4.2, Recorder.Sync (queue worker 0x0) 4.1
- present 3489: 98.0 ms at 8385 ms: Driver.Packet (queue worker 0x0) 60.4, Driver.Packet.draw (queue worker 0x0) 60.3, Driver.Draw (queue worker 0x0) 60.3, Graphics.Draw (draw committer) 59.8
- present 3485: 57.6 ms at 8275 ms: Driver.Packet (queue worker 0x0) 31.4, Driver.Packet.draw (queue worker 0x0) 31.3, Driver.Draw (queue worker 0x0) 31.3, Graphics.Draw (draw committer) 31.1
- present 3532: 45.1 ms at 9476 ms: Driver.Packet (queue worker 0x0) 33.0, Graphics.Draw (draw committer) 32.3, Driver.Packet.pipeline_drain (queue worker 0x0) 31.8, Graphics.Draw.draw_and_resource_release (draw committer) 14.5
- present 3732: 39.3 ms at 13847 ms: Driver.Packet (queue worker 0x0) 21.7, Driver.Packet.pipeline_drain (queue worker 0x0) 21.6, Graphics.Draw (draw committer) 19.0, Driver.WorkerWait (queue worker 0x0) 17.5

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.0 (0 %) | 18.5 | 0.2 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 18.5 | 0.0 | Shader.PrepareArtifact 0.0 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 18.5 | 0.0 | Vulkan.GraphicsPipelinePrebuild 0.0 |
| queue worker 0x0 | 3.3 (18 %) | 14.7 (80 %) | 0.5 | 1.7 | Driver.Packet 3.3, Driver.Completion 0.0 |
| draw committer | 2.4 (13 %) | 0.7 (4 %) | 15.3 | 0.8 | Graphics.Draw 2.8, Vulkan.DrawFromRecipe 0.4 |
| presenter | 0.4 (2 %) | 10.9 (59 %) | 7.1 | 0.0 | VideoOut.Flip 11.4, Driver.Present 0.0 |
| gpu | 4.7 (25 %) | 0.0 (0 %) | 13.8 | 0.0 | batch 4.7 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.WorkerWait 14.7, Driver.WorkerWait.idle_and_completion_poll 14.7, VideoOut.Flip 11.4, VideoOut.Flip.vblank_wait 9.8, batch 4.7, Driver.Packet 3.3, Driver.Packet.pipeline_drain 2.9, Graphics.Draw 2.8, VideoOut.Flip.present 1.4, Driver.Present 1.4

**Longest waits**

- Driver.WorkerWait (queue worker 0x0) 321.4 ms at 8960 ms, present 3518
- Driver.WorkerWait (queue worker 0x0) 36.6 ms at 8385 ms, present 3488
- Driver.WorkerWait (queue worker 0x0) 34.9 ms at 8266 ms, present 3484
- Driver.WorkerWait (queue worker 0x0) 27.9 ms at 8144 ms, present 3477
- Driver.WorkerWait (queue worker 0x0) 26.8 ms at 9461 ms, present 3531
- Driver.WorkerWait (queue worker 0x0) 26.8 ms at 8502 ms, present 3491
- Driver.WorkerWait (queue worker 0x0) 25.4 ms at 8342 ms, present 3486
- Driver.WorkerWait (queue worker 0x0) 25.3 ms at 8229 ms, present 3482
- Driver.WorkerWait (queue worker 0x0) 23.5 ms at 12207 ms, present 3669
- Driver.WorkerWait (queue worker 0x0) 23.0 ms at 12257 ms, present 3671

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 762: 8.6 ms at 8384 ms, present 3488: GPU no ranges; recorded by Graphics.Draw (draw committer) 5.3, Graphics.Draw.draw_and_resource_release (draw committer) 5.2, Recorder.Submit (draw committer) 0.0
- batch 296: 7.1 ms at 4441 ms, present 3255: GPU no ranges; recorded by Graphics.Draw (draw committer) 0.2, Graphics.Draw.draw_and_resource_release (draw committer) 0.1, Vulkan.DrawFromRecipe (draw committer) 0.1, Graphics.DrawWithRecipe (draw committer) 0.1
- batch 290: 7.0 ms at 4391 ms, present 3252: GPU no ranges; recorded by Graphics.Draw (draw committer) 0.3, Graphics.Draw.draw_and_resource_release (draw committer) 0.2, Recorder.Submit (draw committer) 0.0
- batch 21: 7.0 ms at 170 ms, present 2999: GPU no ranges; recorded by Graphics.Draw (draw committer) 0.2, Graphics.Draw.draw_and_resource_release (draw committer) 0.1, Vulkan.DrawFromRecipe (draw committer) 0.1, Graphics.DrawWithRecipe (draw committer) 0.1
- batch 286: 6.9 ms at 4358 ms, present 3250: GPU no ranges; recorded by Graphics.Draw (draw committer) 0.3, Graphics.Draw.draw_and_resource_release (draw committer) 0.2, Recorder.Submit (draw committer) 0.0
