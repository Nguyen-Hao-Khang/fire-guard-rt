#!/usr/bin/env python3
"""Convert an "fg trace dump" capture into a Chrome/Perfetto trace.

Usage:
    python scripts/trace2perfetto.py capture.log trace.json

capture.log is the serial output containing the TR,... lines (other lines
are ignored). Open trace.json at https://ui.perfetto.dev or in
chrome://tracing. Each thread becomes a track with one slice per
execution interval; interrupts appear on a separate "isr" track.
"""
import json
import sys


def parse(lines):
    events = []
    for line in lines:
        line = line.strip()
        start = line.find("TR,")
        if start < 0:
            continue
        fields = line[start:].split(",")
        if len(fields) != 4 or not fields[1].isdigit():
            continue
        events.append((int(fields[1]), fields[2], fields[3]))
    return events


def convert(events):
    tids = {"isr": 0}
    out = []
    running = {}
    isr_start = None

    def tid(name):
        if name not in tids:
            tids[name] = len(tids)
        return tids[name]

    for ts, ev, name in events:
        if ev == "I":
            running[name] = ts
        elif ev == "O" and name in running:
            begin = running.pop(name)
            out.append({"name": name, "ph": "X", "ts": begin, "dur": max(ts - begin, 1),
                        "pid": 1, "tid": tid(name)})
        elif ev == "E":
            isr_start = ts
        elif ev == "X" and isr_start is not None:
            out.append({"name": "isr", "ph": "X", "ts": isr_start,
                        "dur": max(ts - isr_start, 1), "pid": 1, "tid": 0})
            isr_start = None

    for name, t in tids.items():
        out.append({"name": "thread_name", "ph": "M", "pid": 1, "tid": t,
                    "args": {"name": name}})
    out.append({"name": "process_name", "ph": "M", "pid": 1,
                "args": {"name": "FireGuard-RT"}})
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        events = parse(f)
    if not events:
        sys.exit("no TR lines found")
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        json.dump({"traceEvents": convert(events), "displayTimeUnit": "ms"}, f)
    print(f"{len(events)} events -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
