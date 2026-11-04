# Frame capture reader bench

Two standalone programs that read a frame capture directory (`calls.bin`, `memory.bin`, `capture.json`) through `FrameCaptureReader`. They are not part of the build and are not proposed for merge.

- `ReaderBench.cpp` times the reader on the capture's steady-state delta chunks: the delta groups under 512 MiB, so a capture's one-time large groups are left out and reported as `large_mib`. It prints one JSON line:
  - `read_first_ms`, `read_span_ms`: two passes of `FrameCaptureReader::Read(chunk, span)` into one reused buffer (the first pass opens the streams);
  - `read_vector_ms`: one pass of `Read(chunk)`, which returns a new vector;
  - `decode_ms`, `decode_fresh_context_ms`: the same chunks decompressed from memory with one reused `ZSTD_DCtx`, and with `ZSTD_decompress` (a new context per chunk), after reading their compressed bytes into memory once: the decode cost without file reads;
  - `copy_ms`, `copy_watched_ms` (Windows only): copying the decoded bytes into a 64 MiB region, without and with `MEM_WRITE_WATCH`;
  - `errors`: chunks whose decompressed size was wrong (exit 1 if any).
- `ReaderVerify.cpp` decodes every delta and snapshot chunk independently with `ZSTD_decompress` and compares an FNV-1a hash of each chunk with what `FrameCaptureReader::Read` returns: first serially, then from `hardware_concurrency()` threads that alternate the span and vector overloads. It prints the chunk count and both mismatch counts (exit 1 if any).

Build each one against the `FrameCapture.cpp` you want to measure. With the reader of this branch (`c72645700a`) and a configured build tree, from the repository root:

```
g++ -O2 -DNDEBUG -std=c++20 -I core/libs -I 3rdparty/zstd/lib tools/bench/frame-capture-reader/ReaderBench.cpp core/libs/prx/libSceAgcDriver/Execution/src/FrameCapture.cpp build/3rdparty/zstd/lib/libzstd.a -o reader-bench
g++ -O2 -DNDEBUG -std=c++20 -I core/libs -I 3rdparty/zstd/lib tools/bench/frame-capture-reader/ReaderVerify.cpp core/libs/prx/libSceAgcDriver/Execution/src/FrameCapture.cpp build/3rdparty/zstd/lib/libzstd.a -o reader-verify
./reader-bench <capture directory>
./reader-verify <capture directory>
```

For the comparison with `main`'s reader, build `reader-bench` a second time with `main`'s `FrameCapture.cpp` and its header (same zstd), and run the two binaries alternately on the same capture with a warm file cache.
