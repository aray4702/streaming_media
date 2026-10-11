#!/usr/bin/env python3
"""Generates the per-algorithm documentation from the code and from the
results produced by tools/run_all.sh.

  docs/algorithms/README.md            index + coverage of sections 3-8
  docs/algorithms/<s>-<area>/<id>-*.md one document per algorithm
  docs/results/README.md               cross-platform verification summary

Design text comes from the header comments (so it cannot drift from the
code), the API skeleton from the public declarations, the metric list
from ExportMetrics(), events from SM_EVENT() calls, and results from the
recorded test/benchmark output. Hand-written parts live in catalog.py.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from catalog import ALGORITHMS, NOT_IMPLEMENTED, SECTIONS  # noqa: E402
from issues import ISSUES  # noqa: E402

PLATFORMS = [
    ("native", "macOS arm64 (native)"),
    ("wasm", "WebAssembly (Node)"),
    ("asan", "ASan+UBSan (Debug)"),
    ("noexc", "-fno-exceptions -fno-rtti"),
]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


# ----------------------------------------------------------------------------
# Code extraction


def header_comment(text):
    """Leading // comment block of a header, as paragraphs."""
    lines = []
    for line in text.splitlines():
        if line.startswith("//"):
            lines.append(line[2:].removeprefix(" "))
        elif line.strip() == "" and not lines:
            continue
        else:
            break
    # Drop the "N.M Title" line: the document title already says it.
    if lines and re.match(r"^\d+\.\d+", lines[0]):
        lines = lines[1:]
    return "\n".join(lines).strip()


def comment_to_markdown(c):
    """Indented lines become code blocks; prose lines are joined."""
    out, para, code = [], [], []

    def flush_para():
        if para:
            text = ""
            for s in para:
                s = s.strip()
                # Re-join words hyphenated across a line break ("non-\nlinear").
                if text.endswith("-") and len(text) > 1 and text[-2].isalpha():
                    text += s
                else:
                    text += (" " if text else "") + s
            out.append(text)
            out.append("")
            para.clear()

    def flush_code():
        if code:
            out.append("```")
            out.extend(code)
            out.append("```")
            out.append("")
            code.clear()

    for line in c.splitlines():
        if line.strip() == "":
            flush_para()
            flush_code()
        elif line.startswith("  "):
            flush_para()
            code.append(line)
        else:
            flush_code()
            para.append(line)
    flush_para()
    flush_code()
    return "\n".join(out).strip()


def public_api(text):
    """Header without the leading comment, private sections and includes."""
    body = text
    lines = body.splitlines()
    i = 0
    while i < len(lines) and (lines[i].startswith("//") or not lines[i].strip()):
        i += 1
    lines = lines[i:]
    out, skipping = [], False
    for line in lines:
        s = line.strip()
        if s.startswith("#pragma") or s.startswith("#include"):
            continue
        if s == "private:":
            skipping = True
            continue
        if skipping:
            if s.startswith("};") and not line.startswith("    "):
                skipping = False
                out.append(line)
            continue
        out.append(line)
    text = "\n".join(out)
    text = re.sub(r"\n{3,}", "\n\n", text).strip()
    return text


def exported_metrics(src_text):
    kinds = {"Counter": "counter", "Gauge": "gauge", "Hist": "histogram"}
    found = []
    for m in re.finditer(r'w\.(Counter|Gauge|Hist)\(\s*(?:std::string\()?"([^"]+)"', src_text):
        item = (m.group(2), kinds[m.group(1)])
        if item not in found:
            found.append(item)
    # Dynamic names like "sent_packets_" + n.
    for m in re.finditer(r'w\.(Counter|Gauge|Hist)\(\s*"([^"]+)"\s*\+', src_text):
        item = (m.group(2) + "<name>", kinds[m.group(1)])
        if item not in found:
            found.append(item)
    return found


def emitted_events(src_text):
    ev = []
    for m in re.finditer(r'SM_EVENT\([^,]+,[^,]+,\s*"([^"]+)",\s*([^,]+?),', src_text):
        name = m.group(2).strip()
        if name.startswith('"'):
            name = name.strip('"')
        else:
            name = "<" + name + ">"
        item = f"{m.group(1)}/{name}"
        if item not in ev:
            ev.append(item)
    return ev


# ----------------------------------------------------------------------------
# Results parsing


def parse_test_output(path):
    """{suite: [ {name, status, ms, metrics:[(k,v)]} ]}"""
    res = {}
    if not os.path.exists(path):
        return res
    cur = None
    for line in read(path).splitlines():
        m = re.match(r"\[ RUN  \] (\w+)\.(\w+)", line)
        if m:
            cur = {"suite": m.group(1), "name": m.group(2), "status": "?", "ms": None, "metrics": []}
            res.setdefault(cur["suite"], []).append(cur)
            continue
        m = re.match(r"\[metric\] (\S+)=(\S+)", line)
        if m and cur:
            cur["metrics"].append((m.group(1), m.group(2)))
            continue
        m = re.match(r"\[ (OK|FAIL) *\] (\w+)\.(\w+) \(([\d.]+) ms\)", line.replace("[  OK  ]", "[ OK ]"))
        if m and cur:
            cur["status"] = "pass" if m.group(1) == "OK" else "FAIL"
            cur["ms"] = float(m.group(4))
    return res


def load_platform(name):
    d = os.path.join(ROOT, "docs", "results", name)
    if not os.path.isdir(d):
        return None
    tests = {}
    raw = os.path.join(d, "raw")
    for f in sorted(os.listdir(raw)) if os.path.isdir(raw) else []:
        if f.startswith("sm_unit_") or f.startswith("sm_e2e_"):
            for suite, cases in parse_test_output(os.path.join(raw, f)).items():
                tests.setdefault(suite, []).extend(cases)
    bench = {}
    bj = os.path.join(d, "bench.json")
    if os.path.exists(bj):
        for b in json.load(open(bj))["benchmarks"]:
            bench[b["name"]] = b
    env = {}
    ej = os.path.join(d, "environment.json")
    if os.path.exists(ej):
        env = json.load(open(ej))
    return {"tests": tests, "bench": bench, "env": env}


def suite_status(results, suite):
    cases = results["tests"].get(suite, []) if results else []
    if not cases:
        return "-", 0, 0
    passed = sum(c["status"] == "pass" for c in cases)
    return ("pass" if passed == len(cases) else "FAIL"), passed, len(cases)


def fmt(v):
    try:
        x = float(v)
    except (TypeError, ValueError):
        return str(v)
    if x == int(x) and abs(x) < 1e9:
        return str(int(x))
    if abs(x) >= 100:
        return f"{x:.1f}"
    if abs(x) >= 1:
        return f"{x:.3g}" if abs(x) < 10 else f"{x:.3g}"
    return f"{x:.3g}"


def fmt_ns(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:.2f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.2f} us"
    return f"{ns:.1f} ns"


# ----------------------------------------------------------------------------
# Rendering


def section_dir(sec):
    title, area = SECTIONS[sec]
    return f"{sec}-{area}"


def algo_path(a):
    return os.path.join("docs", "algorithms", section_dir(a["id"].split(".")[0]), f"{a['id']}-{a['slug']}.md")


def render_algorithm(a, plats):
    sec = a["id"].split(".")[0]
    out = [f"# {a['id']} {a['title']}", ""]
    out.append(f"Section {sec}, *{SECTIONS[sec][0]}*, of [Problems WebRTC solves](../../02-webrtc-problems-solved.md). "
               f"Problems addressed:")
    out.append("")
    for p in a["problems"]:
        out.append(f"- {p}")
    out.append("")

    # The issue: what goes wrong without the algorithm (tools/issues.py).
    out.append("## The issue")
    out.append("")
    for para in ISSUES[a["id"]].strip().split("\n\n"):
        out.append(" ".join(line.strip() for line in para.splitlines()))
        out.append("")

    # Design.
    out.append("## Design")
    out.append("")
    for h in a["headers"]:
        text = read(os.path.join(ROOT, "include", "sm", h))
        out.append(comment_to_markdown(header_comment(text)))
        out.append("")
    if a.get("notes"):
        out.append(a["notes"])
        out.append("")

    # Implementation skeleton.
    out.append("## Implementation skeleton")
    out.append("")
    files = []
    for h in a["headers"]:
        src = h.replace(".h", ".cc")
        files.append(f"[include/sm/{h}](../../../include/sm/{h})")
        if os.path.exists(os.path.join(ROOT, "src", src)):
            files.append(f"[src/{src}](../../../src/{src})")
    out.append("Files: " + ", ".join(files) + ".")
    out.append("")
    out.append("Public API (generated from the header; private members omitted):")
    out.append("")
    for h in a["headers"]:
        out.append("```cpp")
        out.append(public_api(read(os.path.join(ROOT, "include", "sm", h))))
        out.append("```")
        out.append("")

    # Metrics.
    metrics, events = [], []
    for h in a["headers"]:
        src = os.path.join(ROOT, "src", h.replace(".h", ".cc"))
        if os.path.exists(src):
            st = read(src)
            metrics += [m for m in exported_metrics(st) if m not in metrics]
            events += [e for e in emitted_events(st) if e not in events]
    out.append("## Metrics and observability")
    out.append("")
    out.append("Every class implements `MetricsSource::ExportMetrics`, so it can be registered in a "
               "`MetricsRegistry` and exported as JSON or Prometheus text. Histograms expand to "
               "`.count/.mean/.p50/.p95/.p99/.max`.")
    out.append("")
    if metrics:
        out.append("| Metric | Type |")
        out.append("|---|---|")
        for name, kind in metrics:
            out.append(f"| `{name}` | {kind} |")
        out.append("")
    if events:
        out.append("Structured events (`EventSink`, `module/name`): " + ", ".join(f"`{e}`" for e in events) + ".")
        out.append("")

    # Test methods.
    native = plats.get("native")
    out.append("## Test methods")
    out.append("")
    out.append("**Unit tests** (deterministic, no network):")
    out.append("")
    for s in a["unit"]:
        cases = native["tests"].get(s, []) if native else []
        names = ", ".join(f"`{c['name']}`" for c in cases) or "(not run)"
        out.append(f"- `{s}`: {names}")
    out.append("")
    out.append("**End-to-end test** (sender -> simulated network -> receiver, `tests/e2e/`):")
    out.append("")
    for s in a["e2e"]:
        cases = native["tests"].get(s, []) if native else []
        names = ", ".join(f"`{c['name']}`" for c in cases) or "(not run)"
        out.append(f"- `{s}`: {names}")
    out.append("")
    out.append(a["scenario"])
    out.append("")

    # Benchmarks.
    out.append("## Benchmarks")
    out.append("")
    out.append("Throughput benchmarks have a speed budget (ns per item, or a minimum real-time factor for "
               "media processing); quality benchmarks rerun an end-to-end scenario and gate its outcome. A "
               "benchmark passes only if every requirement holds.")
    out.append("")
    out.append("| Benchmark | Requirement | " + " | ".join(lbl for k, lbl in PLATFORMS if plats.get(k) and plats[k]["bench"]) + " |")
    out.append("|---|---|" + "---|" * sum(1 for k, _ in PLATFORMS if plats.get(k) and plats[k]["bench"]))
    for bname in a["bench"]:
        req_txt = ""
        cells = []
        for k, _ in PLATFORMS:
            p = plats.get(k)
            if not p or not p["bench"]:
                continue
            b = p["bench"].get(bname)
            if not b:
                cells.append("-")
                continue
            if not req_txt:
                req_txt = "; ".join(f"{r['name']} {r['op']} {fmt(r['threshold'])}" for r in b["requirements"])
            if b["quality"]:
                val = ", ".join(f"{k2}={fmt(v)}" for k2, v in b["counters"].items())
            elif b["realtime_factor"] > 0:
                val = f"{fmt_ns(b['ns_per_item'])}/frame, {b['realtime_factor']:.0f}x real time"
            else:
                val = f"{fmt_ns(b['ns_per_item'])}/item"
            cells.append(f"{'PASS' if b['pass'] else '**FAIL**'}: {val}")
        out.append(f"| `{bname}` | {req_txt} | " + " | ".join(cells) + " |")
    out.append("")

    # Results.
    out.append("## Results")
    out.append("")
    out.append("| Suite | " + " | ".join(lbl for k, lbl in PLATFORMS if plats.get(k)) + " |")
    out.append("|---|" + "---|" * sum(1 for k, _ in PLATFORMS if plats.get(k)))
    for s in a["unit"] + a["e2e"]:
        cells = []
        for k, _ in PLATFORMS:
            p = plats.get(k)
            if not p:
                continue
            st, ok, n = suite_status(p, s)
            cells.append(f"{st} ({ok}/{n})" if n else "-")
        out.append(f"| `{s}` | " + " | ".join(cells) + " |")
    out.append("")
    # Recorded measurements.
    rows = []
    for s in a["unit"] + a["e2e"]:
        for c in (native["tests"].get(s, []) if native else []):
            for k, v in c["metrics"]:
                wasm_v = "-"
                w = plats.get("wasm")
                if w:
                    for wc in w["tests"].get(s, []):
                        if wc["name"] == c["name"]:
                            for wk, wv in wc["metrics"]:
                                if wk == k:
                                    wasm_v = fmt(wv)
                rows.append((f"{s}.{c['name']}", k, fmt(v), wasm_v))
    if rows:
        out.append("Measured values recorded by the tests (`SM_RECORD`):")
        out.append("")
        out.append("| Test | Measurement | Native | Wasm |")
        out.append("|---|---|---|---|")
        for r in rows:
            out.append(f"| `{r[0]}` | `{r[1]}` | {r[2]} | {r[3]} |")
        out.append("")
        out.append("The simulations are deterministic (seeded xoshiro PRNG, no `<random>`), so native and Wasm "
                   "values should match exactly; any difference would point to a floating-point portability issue.")
        out.append("")

    # Verification notes.
    out.append("## Verification notes")
    out.append("")
    out.append(a["verification"])
    out.append("")
    return "\n".join(out)


def lib_sizes():
    sizes = {}
    d = os.path.join(ROOT, "build", "src")
    if not os.path.isdir(d):
        return sizes
    for f in sorted(os.listdir(d)):
        if f.startswith("libsm_") and f.endswith(".a"):
            try:
                out = subprocess.run(["size", os.path.join(d, f)], capture_output=True, text=True).stdout
            except OSError:
                continue
            text = 0
            for line in out.splitlines()[1:]:
                parts = line.split()
                if parts and parts[0].isdigit():
                    text += int(parts[0])
            sizes[f[3:-2]] = (text, os.path.getsize(os.path.join(d, f)))
    return sizes


def render_index(plats):
    out = ["# Algorithms for sections 3-8", ""]
    out.append("One document per algorithm: problem, the issue it solves, design, implementation skeleton, metrics, test methods, "
               "benchmarks, results and verification notes. Generated by `tools/gen_docs.py` from the code and "
               "the latest `tools/run_all.sh` results; do not edit by hand (edit `tools/catalog.py`, "
               "`tools/issues.py` or the header comments instead).")
    out.append("")
    labels = [lbl for k, lbl in PLATFORMS if plats.get(k)]
    for sec, (title, _) in SECTIONS.items():
        out.append(f"## {sec}. {title}")
        out.append("")
        out.append("| # | Algorithm | Unit | E2E | Benchmarks | " + " | ".join(labels) + " |")
        out.append("|---|---|---|---|---|" + "---|" * len(labels))
        for a in [x for x in ALGORITHMS if x["id"].split(".")[0] == sec]:
            native = plats.get("native")
            u = sum(suite_status(native, s)[2] for s in a["unit"])
            e = sum(suite_status(native, s)[2] for s in a["e2e"])
            cells = []
            for k, _ in PLATFORMS:
                p = plats.get(k)
                if not p:
                    continue
                ok = all(suite_status(p, s)[0] == "pass" for s in a["unit"] + a["e2e"])
                bok = all(p["bench"].get(b, {}).get("pass", True) for b in a["bench"]) if p["bench"] else True
                cells.append("pass" if ok and bok else "**FAIL**")
            link = os.path.relpath(algo_path(a), os.path.join("docs", "algorithms"))
            out.append(f"| {a['id']} | [{a['title']}]({link}) | {u} | {e} | {len(a['bench'])} | " + " | ".join(cells) + " |")
        out.append("")
    out.append("## Not implemented as separate algorithms")
    out.append("")
    out.append("| Section | Mechanism | Why |")
    out.append("|---|---|---|")
    for sec, what, why in NOT_IMPLEMENTED:
        out.append(f"| {sec} | {what} | {why} |")
    out.append("")
    return "\n".join(out)


def render_results(plats):
    out = ["# Verification results", ""]
    out.append("Produced by `tools/run_all.sh all`; regenerate with `tools/gen_docs.py`.")
    out.append("")
    out.append("## Platforms")
    out.append("")
    out.append("| Platform | Compiler | Runtime | Date | CTest |")
    out.append("|---|---|---|---|---|")
    for k, lbl in PLATFORMS:
        p = plats.get(k)
        if not p:
            continue
        e = p["env"]
        out.append(f"| {lbl} | {e.get('compiler', '')} | {e.get('runtime', '')} | {e.get('date', '')} | {e.get('ctest', '')} |")
    out.append("")
    out.append("CTest has one entry per test suite (one unit and one end-to-end suite per algorithm) plus one "
               "benchmark gate per section (`bench.<section>`, run with a short minimum time).")
    out.append("")
    for k, lbl in PLATFORMS:
        p = plats.get(k)
        if not p:
            continue
        total = sum(len(c) for c in p["tests"].values())
        passed = sum(1 for c in p["tests"].values() for x in c if x["status"] == "pass")
        nb = len(p["bench"])
        bp = sum(1 for b in p["bench"].values() if b["pass"])
        out.append(f"- **{lbl}**: {passed}/{total} test cases passed" + (f", {bp}/{nb} benchmarks passed" if nb else "") + ".")
    out.append("")
    sizes = lib_sizes()
    if sizes:
        out.append("## Footprint (native Release, code size per section)")
        out.append("")
        out.append("| Library | Code (`size` text) | Archive file |")
        out.append("|---|---|---|")
        for name, (text, file_size) in sizes.items():
            out.append(f"| `{name}` | {text / 1024:.0f} KiB | {file_size / 1024:.0f} KiB |")
        out.append("")
        out.append("Each section is a separate static library and can be turned off with `SM_WITH_<SECTION>=OFF`; "
                   "the linker keeps only what is used.")
        out.append("")
    # All benchmarks, all platforms.
    out.append("## Benchmarks")
    out.append("")
    labels = [lbl for k, lbl in PLATFORMS if plats.get(k) and plats[k]["bench"]]
    out.append("| Benchmark | " + " | ".join(labels) + " |")
    out.append("|---|" + "---|" * len(labels))
    names = []
    for k, _ in PLATFORMS:
        p = plats.get(k)
        if p:
            names += [n for n in p["bench"] if n not in names]
    for n in names:
        cells = []
        for k, _ in PLATFORMS:
            p = plats.get(k)
            if not p or not p["bench"]:
                continue
            b = p["bench"].get(n)
            if not b:
                cells.append("-")
            elif b["quality"]:
                cells.append(("PASS" if b["pass"] else "**FAIL**") + " (quality)")
            elif b["realtime_factor"] > 0:
                cells.append(f"{'PASS' if b['pass'] else '**FAIL**'} {b['realtime_factor']:.0f}x RT")
            else:
                cells.append(f"{'PASS' if b['pass'] else '**FAIL**'} {fmt_ns(b['ns_per_item'])}")
        out.append(f"| `{n}` | " + " | ".join(cells) + " |")
    out.append("")
    return "\n".join(out)


def main():
    plats = {}
    for k, _ in PLATFORMS:
        p = load_platform(k)
        if p:
            plats[k] = p
    for a in ALGORITHMS:
        path = os.path.join(ROOT, algo_path(a))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(render_algorithm(a, plats) + "\n")
    with open(os.path.join(ROOT, "docs", "algorithms", "README.md"), "w", encoding="utf-8") as f:
        f.write(render_index(plats) + "\n")
    os.makedirs(os.path.join(ROOT, "docs", "results"), exist_ok=True)
    with open(os.path.join(ROOT, "docs", "results", "README.md"), "w", encoding="utf-8") as f:
        f.write(render_results(plats) + "\n")
    print(f"generated {len(ALGORITHMS)} algorithm documents for platforms: {', '.join(plats)}")


if __name__ == "__main__":
    main()
