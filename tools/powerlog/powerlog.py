#!/usr/bin/env python3
"""T-Deck MAX power log viewer.

Pulls the per-minute power log off the deck over USB, keeps every pull in a
local history (see store.py) and shows it in the browser: current, battery
drain in %/h, battery level, and what was switched on.

  powerlog.py [serve]          local web viewer; pulls once if there is no data yet
  powerlog.py pull             pull new records from the deck
  powerlog.py check            validate the history and print a summary
  powerlog.py export -o x.html self-contained report to share (no Pull button)

Options: --data DIR (default: powerlogs/ in the main checkout), --port COMx
(default: the first Espressif USB device), --capacity mAh (default 1400).
Pulling needs pyserial; everything else only the standard library.
"""
import argparse
import datetime
import http.server
import json
import os
import subprocess
import sys
import threading
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import pwrlog_format as fmt  # noqa: E402
from store import Store  # noqa: E402


def default_data_dir():
    # Next to the main checkout, so all worktrees share one history
    try:
        out = subprocess.run(["git", "-C", HERE, "rev-parse", "--git-common-dir"],
                             capture_output=True, text=True, timeout=10)
        if out.returncode == 0 and out.stdout.strip():
            common = os.path.normpath(os.path.join(HERE, out.stdout.strip()))
            return os.path.join(os.path.dirname(common), "powerlogs")
    except (OSError, subprocess.SubprocessError):
        pass
    return os.path.join(os.path.dirname(os.path.dirname(HERE)), "powerlogs")


def fmt_time(t, off):
    return (datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=t + off * 3600)).strftime("%Y-%m-%d %H:%M")


def fmt_dur(s):
    s = int(s)
    if s < 3600:
        return "%d min" % (s // 60)
    if s < 86400:
        return "%d h %02d min" % (s // 3600, s % 3600 // 60)
    return "%d d %d h" % (s // 86400, s % 86400 // 3600)


def analyse(store, capacity):
    recs, info = store.load()
    timeline, unknown = fmt.build_timeline(recs, capacity, anchors=store.anchors())
    segs = fmt.segments(timeline, capacity)
    return {
        "timeline": timeline, "unknown": unknown, "info": info, "segments": segs,
        "by_state": fmt.by_state(segs, capacity), "quiet": fmt.quiet_stretches(timeline),
        "by_firmware": fmt.by_firmware(timeline, info["firmware"], capacity),
    }


COLUMNS = ["t", "secs", "ma", "awake", "pct", "mv", "temp_c", "drain_pct_h", "gauge_pct_h",
           "charge", "blocker", "rx", "tx", "refresh", "full_refresh", "wake_lora", "wake_btn",
           "wake_timer", "clock_jump_s", "gap", "shift", "utc_offset", "rc_ppm", "fw",
           "ble_win", "sync_win", "sync_link", "sync_s", "link_ms", "app_rx", "ble_drop"]


def payload(store, capacity, static=False):
    a = analyse(store, capacity)
    tl = a["timeline"]
    cols = {c: [r[c] for r in tl] for c in COLUMNS}
    cols["flags"] = [sum(r[f] << i for i, f in enumerate(fmt.FLAGS)) for r in tl]
    cols["where"] = [r["where"] for r in tl]
    pulls = store.pull_dirs()
    last = [r["t"] for r in tl if pulls and r["pull"] == pulls[-1]]
    return {
        "static": static, "capacity": capacity,
        "generated": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"),
        "flags": fmt.FLAGS, "blockers": fmt.BLOCKERS, "charge": fmt.CHARGE,
        "firmware": a["info"]["firmware"], "by_firmware": a["by_firmware"],
        "cols": cols, "segments": a["segments"], "by_state": a["by_state"], "quiet": a["quiet"],
        "pulls": len(pulls), "last_pull": pulls[-1] if pulls else None,
        "last_pull_t0": min(last) if last else None,
        "duplicates": a["info"]["duplicates"],
        "bad": a["info"]["bad"][:200], "bad_count": len(a["info"]["bad"]),
        "warnings": a["info"]["warnings"][:200], "warning_count": len(a["info"]["warnings"]),
        "unknown": [{"utc": r["utc"], "where": r["where"], "pct": r["pct"], "ma": -r["avg_ma"],
                     "local": r["local_date"] + " " + r["local_time"]} for r in a["unknown"][:500]],
        "unknown_count": len(a["unknown"]),
    }


def cmd_check(store, args):
    a = analyse(store, args.capacity)
    tl, info = a["timeline"], a["info"]
    print("history: %s (%d pulls)" % (store.root, len(store.pull_dirs())))
    if not tl and not a["unknown"]:
        print("no records")
        return 1
    print("records: %d on the timeline, %d with unknown time, %d repeated lines skipped"
          % (len(tl), len(a["unknown"]), info["duplicates"]))
    if tl:
        off = tl[-1]["utc_offset"]
        print("span: %s .. %s local (%s)" % (fmt_time(tl[0]["t"] - tl[0]["secs"], off),
                                             fmt_time(tl[-1]["t"], off), fmt_dur(tl[-1]["t"] - tl[0]["t"])))
        covered = sum(r["secs"] for r in tl)
        print("covered: %s" % fmt_dur(covered))
        gaps = [r for r in tl if r["gap"]]
        print("gaps: %d%s" % (len(gaps), ", longest %s before %s" % (
            fmt_dur(max(g["gap"] for g in gaps)), fmt_time(max(gaps, key=lambda g: g["gap"])["t"], off)) if gaps else ""))
        jumps = [r for r in tl if r["clock_jump_s"]]
        moved = [r for r in tl if r["shift"]]
        print("clock changes: %d, records moved onto the corrected clock: %d" % (len(jumps), len(moved)))
        for j in jumps[:10]:
            print("  %s  %+d s" % (fmt_time(j["t"], j["utc_offset"]), j["clock_jump_s"]))
        print("quiet stretches the viewer compresses: %d" % len(a["quiet"]))
        print("firmware (time logged, mean current on battery):")
        for fw in a["by_firmware"]:
            name = "%s%s %s, commit %s" % (fw["hash"], "+" if fw["dirty"] else "", fw["version"],
                                           fmt_time(fw["git_time"], off)) if fw["fw"] >= 0 else "not named (older firmware)"
            print("  %-60s %12s  %s" % (name[:60], fmt_dur(fw["secs"]), "%.1f mA" % fw["ma"] if fw["ma"] is not None else "-"))
        print("by state (time, mean current, drain):")
        for s in a["by_state"][:15]:
            print("  %-48s %12s  %6.1f mA  %5.2f %%/h" % (s["state"][:48], fmt_dur(s["secs"]), s["ma"], s["pct_h"]))
    for u in a["unknown"][:10]:
        print("unknown time: %s (%s)" % (u["where"], u["local_date"] + " " + u["local_time"]))
    print("lines that are not valid records: %d" % len(info["bad"]))
    for b in info["bad"][:10]:
        print("  %s: %s | %s" % (b["where"], b["why"], b["line"][:80]))
    print("warnings: %d" % len(info["warnings"]))
    for w in info["warnings"][:10]:
        print("  %s: %s" % (w["where"], w["why"]))
    return 0


def cmd_pull(store, args):
    from device import PullError, pull
    try:
        pull(store, args.port)
    except PullError as e:
        print("pull failed:", e)
        return 1
    return 0


def page(data):
    with open(os.path.join(HERE, "web", "index.html"), encoding="utf-8") as f:
        html = f.read()
    blob = json.dumps(data, separators=(",", ":")).replace("</", "<\\/")
    return html.replace("/*PWR_DATA*/null", blob)


def cmd_export(store, args):
    out = args.output or os.path.join(store.root, "report.html")
    with open(out, "w", encoding="utf-8") as f:
        f.write(page(payload(store, args.capacity, static=True)))
    print("wrote", out)
    return 0


class Puller:
    """One pull at a time in the background; the page polls its status."""

    def __init__(self, store, port):
        self.store, self.port = store, port
        self.lock = threading.Lock()
        self.status = {"running": False, "log": [], "result": None, "error": None, "seq": 0}

    def start(self):
        with self.lock:
            if self.status["running"]:
                return False
            self.status = {"running": True, "log": [], "result": None, "error": None,
                           "seq": self.status["seq"] + 1}
        threading.Thread(target=self.run, daemon=True).start()
        return True

    def log(self, msg):
        print("pull:", msg)
        with self.lock:
            self.status["log"] = (self.status["log"] + [msg])[-30:]

    def run(self):
        result, error = None, None
        try:
            from device import PullError, pull
            try:
                result = pull(self.store, self.port, self.log)
            except PullError as e:
                error = str(e)
        except Exception as e:  # keep the server alive, show it on the page
            error = "%s: %s" % (type(e).__name__, e)
        if error:
            self.log("failed: " + error)
        with self.lock:
            self.status.update(running=False, result=result, error=error)

    def get(self):
        with self.lock:
            return dict(self.status)


def cmd_serve(store, args):
    puller = Puller(store, args.port)

    class Handler(http.server.BaseHTTPRequestHandler):
        def send(self, code, body, ctype):
            data = body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/":
                self.send(200, page(None), "text/html; charset=utf-8")
            elif path == "/api/data":
                self.send(200, json.dumps(payload(store, args.capacity), separators=(",", ":")), "application/json")
            elif path == "/api/status":
                self.send(200, json.dumps(puller.get()), "application/json")
            else:
                self.send(404, "not found", "text/plain")

        def do_POST(self):
            if self.path == "/api/pull":
                started = puller.start()
                self.send(202 if started else 409, json.dumps(puller.get()), "application/json")
            else:
                self.send(404, "not found", "text/plain")

        def log_message(self, *a):
            pass

    srv = None
    for port in range(args.http_port, args.http_port + 20):
        try:
            srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
            break
        except OSError:
            continue
    if srv is None:
        print("no free local port")
        return 1
    url = "http://127.0.0.1:%d/" % srv.server_address[1]
    print("power log viewer at", url, "(Ctrl+C to stop)")
    print("history:", store.root)
    if store.is_empty():
        print("no data yet: pulling from the deck")
        puller.start()
    if not args.no_browser:
        webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


def main():
    ap = argparse.ArgumentParser(description="T-Deck MAX power log viewer")
    ap.add_argument("command", nargs="?", default="serve", choices=["serve", "pull", "check", "export"])
    ap.add_argument("--data", default=None, help="history folder (default: powerlogs/ in the main checkout)")
    ap.add_argument("--port", default=None, help="serial port of the deck (default: auto)")
    ap.add_argument("--capacity", type=int, default=fmt.DESIGN_CAPACITY_MAH, help="battery mAh for %%/h")
    ap.add_argument("--http-port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("-o", "--output", help="export: output HTML file")
    args = ap.parse_args()
    store = Store(args.data or default_data_dir())
    return {"serve": cmd_serve, "pull": cmd_pull, "check": cmd_check, "export": cmd_export}[args.command](store, args)


if __name__ == "__main__":
    sys.exit(main())
