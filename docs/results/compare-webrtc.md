# streaming_media vs libwebrtc

This page used to hold a snapshot of the comparison, and went stale. The
latest results are produced by the `compare-webrtc` CI job on every push to
`main` (10 seeds, both libraries over the same emulated links, see
[compare/README.md](../../compare/README.md)) and published to the
`parity-data` branch:

| File | Contents |
|---|---|
| [parity.md](https://raw.githubusercontent.com/aray4702/streaming_media/parity-data/parity.md) | The parity report: every scenario and metric where this library and libwebrtc differ by more than the parity band, in either direction, with the issue that owns each gap |
| [compare-webrtc.txt](https://raw.githubusercontent.com/aray4702/streaming_media/parity-data/compare-webrtc.txt) | The side-by-side table, mean over seeds |
| [compare-webrtc.json](https://raw.githubusercontent.com/aray4702/streaming_media/parity-data/compare-webrtc.json) | Means, every seed's values and each metric's tie floor |

The band and the report format are documented in
[tools/parity_report.py](../../tools/parity_report.py); the owner of each
gap is in [tools/parity_owners.json](../../tools/parity_owners.json). A pull
request's job summary shows the same report for its head and the gaps it
closes and opens against `main`.

Regenerate locally:

```sh
compare/webrtc/fetch.sh
CXX=clang++-21 cmake -S compare/webrtc -B out/compare -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build out/compare -j 4
out/compare/sm_compare --seeds=10 --json=compare-webrtc.json testbench/scenarios/*.scn
python3 tools/parity_report.py compare-webrtc.json --owners tools/parity_owners.json
```
