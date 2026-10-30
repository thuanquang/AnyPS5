# One-slice storage image: P5R trace pairs (nos7)

Persona 5 Royal (PPSA05110), casino gameplay, RX 6700 XT (driver 32.0.21045.16010), Windows 11 26200, Ryzen 5 5600X.
Base `main` aaccd0ba39 against 047be9a2ce (the PR's patch on that `main`; `git range-diff` shows the same patch as the PR head c6f5b175e2).
Two clean builds in two run folders, legs alternating main/change (legs 3-10), exclusive PC, same pad route to gameplay in every leg.
`APS5_TRACE_SECONDS=560:4`; the summaries also carry `APS5_TRACE_DIR` and `APS5_DUMP_FRAMES` 1300 / every 20, so 3 frame dumps fall inside each 4 s window on both sides.

- `aps5-trace-*.json.gz`: one per leg (file name = commit-pid); `nos7-<leg>-<arm>-summary.jsonl`: its summary line.
- `nos7-*-digest.txt`: `tools/trace_digest.py` per leg; `compare-<a>-main-vs-<b>-fix.txt`: `tools/performance_compare.py` per neighbouring pair.
- Scanned before upload: 89 distinct slice names, all emulator stages; string args are thread names only; paths scrubbed; `trace_dropped` false.
