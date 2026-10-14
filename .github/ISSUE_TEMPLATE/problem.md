---
name: Problem report
about: Something a user of the library would notice (gaps, freezes, delay, crashes)
labels: []
---

**Summary:** <one sentence: what the user experiences>

### Reproduce
Commit: <sha>
```
sm_testbench repro.scn
```
<details><summary>repro.scn</summary>

```
name repro
duration 30s
at 0s capacity=1500kbps delay=40ms loss=2%
expect video.freeze_count == 0
```
</details>

### Observed vs expected
| metric | observed | expected |
|---|---|---|

### Notes
