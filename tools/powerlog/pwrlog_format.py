"""The T-Deck MAX power log: parsing, clock repair and derived metrics.

The firmware (examples/companion_radio/main.cpp, meckPwrRecord / meckPwrCsv)
writes one record per minute to /meshcore/powerlog/YYYYMMDD.csv. A record
covers the interval that ends at its "utc"; "avg_ma" is the gauge's average
current, negative while discharging. Records written before the clock was set
carry a wrong epoch; the record during which the clock changed has the jump
in "clock_jump_s" and the corrected time in "utc".

Since git_version.py, a "#fw,<hash>,<commit time>,<build time>,<version>"
line before a boot's first records in a file names the firmware that wrote
them; older firmware wrote none.
"""
import datetime

HEADER = ("utc,local_date,local_time,secs,asleep_s,wake_lora,wake_btn,wake_timer,"
          "pct,mv,avg_ma,temp_c,usb,charge,locked,blocker,saving,gps,modem,wifi,"
          "phone,ble,audio,light,rx,tx,refresh,full_refresh,clock_jump_s,utc_offset,rc_ppm,fix,"
          "ble_win,sync_win,sync_link,sync_s,link_ms,app_rx,ble_drop")
FIELDS = HEADER.split(",")
# The first firmware with the log (6eb18730) had no rc_ppm column, the next ones no fix column,
# then none of the Bluetooth sync window columns (ble_win .. link_ms), then
# no app_rx / ble_drop
SYNC_FIELDS = ["ble_win", "sync_win", "sync_link", "sync_s", "link_ms"]
APP_FIELDS = ["app_rx", "ble_drop"]
HEADER_V4 = HEADER.rsplit(",", len(APP_FIELDS))[0]
HEADER_V3 = HEADER_V4.rsplit(",", len(SYNC_FIELDS))[0]
HEADER_V2 = HEADER_V3.rsplit(",", 1)[0]
HEADER_V1 = HEADER_V2.rsplit(",", 1)[0]
# In column order, the last ones; 0 when the column is missing
OPTIONAL = {"rc_ppm": 0, "fix": 0, "ble_win": 0, "sync_win": 0, "sync_link": 0, "sync_s": 0.0, "link_ms": 0.0,
            "app_rx": 0, "ble_drop": 0}
FLOAT_FIELDS = {"asleep_s", "temp_c", "sync_s", "link_ms"}
TEXT_FIELDS = {"local_date", "local_time"}

# 0/1 columns, in the order the viewer shows them
FLAGS = ["locked", "saving", "usb", "gps", "fix", "modem", "wifi", "phone", "ble", "audio", "light"]
# MeckSleepBlock in examples/companion_radio/ui-new/UITask.h
BLOCKERS = ["can sleep", "saving off", "USB", "phone", "GPS", "modem", "WiFi", "audio", "SOS", "phone sync"]
CHARGE = ["off", "pre-charge", "fast charge", "done"]

DESIGN_CAPACITY_MAH = 1400   # BQ27220_DESIGN_CAPACITY_MAH in variants/lilygo_tdeck_max
PLAUSIBLE_FROM = 1735689600  # 2025-01-01: anything earlier is a clock that was never set


def parse_line(line):
    """One CSV record -> (dict, warnings). Raises ValueError when it is not a record."""
    parts = line.strip().split(",")
    if not len(FIELDS) - len(OPTIONAL) <= len(parts) <= len(FIELDS):
        raise ValueError("%d fields instead of %d" % (len(parts), len(FIELDS)))
    rec = dict(OPTIONAL)
    for name, val in zip(FIELDS, parts):
        if name in TEXT_FIELDS:
            rec[name] = val
        elif name in FLOAT_FIELDS:
            rec[name] = float(val)
        else:
            rec[name] = int(val)
    warnings = []
    for f in FLAGS:
        if rec[f] not in (0, 1):
            warnings.append("%s=%d is not 0/1" % (f, rec[f]))
    if not 0 <= rec["charge"] < len(CHARGE):
        warnings.append("charge=%d unknown" % rec["charge"])
    if not 0 <= rec["blocker"] < len(BLOCKERS):
        warnings.append("blocker=%d unknown" % rec["blocker"])
    if not 0 <= rec["pct"] <= 100:
        warnings.append("pct=%d out of range" % rec["pct"])
    if rec["asleep_s"] > rec["secs"] + 1:
        warnings.append("asleep %.1f s > interval %d s" % (rec["asleep_s"], rec["secs"]))
    date, hm = local_date_time(rec["utc"], rec["utc_offset"])
    if (date, hm) != (rec["local_date"], rec["local_time"]):
        warnings.append("local time %s %s, expected %s %s" % (rec["local_date"], rec["local_time"], date, hm))
    return rec, warnings


def local_date_time(utc, offset_h):
    t = datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=utc + offset_h * 3600)
    return t.strftime("%Y%m%d"), t.strftime("%H:%M")


def parse_fw(line):
    """A "#fw" line -> dict, or None."""
    parts = line.strip().split(",", 4)
    if len(parts) != 5 or parts[0] != "#fw":
        return None
    try:
        git_time, build_time = int(parts[2]), int(parts[3])
    except ValueError:
        return None
    return {"hash": parts[1].rstrip("+"), "dirty": parts[1].endswith("+"),
            "git_time": git_time, "build_time": build_time, "version": parts[4]}


def classify(line):
    """'header', 'fw', 'record', 'bad' (looks like a record but does not parse) or 'noise'."""
    s = line.strip()
    if s in (HEADER, HEADER_V4, HEADER_V3, HEADER_V2, HEADER_V1):
        return "header"
    if s.startswith("#fw,"):
        return "fw" if parse_fw(s) else "noise"
    if not s or not s[0].isdigit() or s.count(",") < 5:
        return "noise"   # CLI echo, other log output, listings
    try:
        parse_line(s)
        return "record"
    except ValueError:
        return "bad"


def repair_clock(recs, anchors=()):
    """Move records written with a wrong clock onto the right one.

    recs: dicts with "utc", "secs", "clock_jump_s" and "local_date", in the
    order they were written per daily file (see Store.load). Each record is
    linked to the one written before it: that one ended secs before this
    one's start on the clock before any jump (utc - clock_jump_s - secs).
    Normally it is the previous line of the same daily file. After a reset
    the clock starts again at the same default time, so several boots can
    share the wrong-clock file and times; only the first record of a file and
    a record with a jump (its predecessor sits in the wrong-date file) and a
    record with a full interval but no predecessor in its file (a run that
    crossed midnight) are looked up among all records ending a run.

    The last record of each chain keeps its time, unless an anchor says how
    far off the deck's clock was at a pull: (deck_utc, host_utc, pull). Walking
    back, a predecessor's shift is its successor's shift plus the successor's
    jump. Sets "utc_fixed" and "shift" on every record.
    """
    n = len(recs)
    for r in recs:
        r["utc_fixed"], r["shift"] = r["utc"], 0
    streams, pos = {}, [0] * n
    for i, r in enumerate(recs):
        st = streams.setdefault(r["local_date"], [])
        pos[i] = len(st)
        st.append(i)

    def expect(i):
        r = recs[i]
        return r["utc"] - r["clock_jump_s"] - r["secs"]

    def follows(p, i):
        # drift under 5 s is not reported as a jump, so allow that much
        return abs(recs[p]["utc"] - expect(i)) <= 5

    def prev_in_stream(i):
        k = pos[i]
        return streams[recs[i]["local_date"]][k - 1] if k else None

    def next_in_stream(i):
        st = streams[recs[i]["local_date"]]
        return st[pos[i] + 1] if pos[i] + 1 < len(st) else None

    pred, succ = [None] * n, [None] * n
    for i in range(n):
        p = prev_in_stream(i)
        if p is not None and follows(p, i):
            pred[i], succ[p] = p, i

    by_t = {}
    for i, r in enumerate(recs):
        by_t.setdefault(r["utc"], []).append(i)

    def run_end(j):
        nx = next_in_stream(j)
        return nx is None or pred[nx] != j

    for i in range(n):
        # a full interval continues a run, unlike the first record after a boot
        r = recs[i]
        if pred[i] is not None or not (r["clock_jump_s"] or prev_in_stream(i) is None or r["secs"] >= 50):
            continue
        best = None
        e = expect(i)
        for dt in range(-5, 6):
            for j in by_t.get(e + dt, ()):
                if j == i or succ[j] is not None or not run_end(j):
                    continue
                # boots sharing the default clock all cross midnight at the
                # same time: pair them up in write order
                score = (abs(dt) > 2, pos[j])
                if best is None or score < best[0]:
                    best = (score, j)
        if best:
            j = best[1]
            pred[i], succ[j] = j, i

    # anchors: the shift of the record the deck wrote last before a pull
    anchor_shift = {}
    for deck_utc, host_utc, pull in anchors:
        off = int(round(host_utc - deck_utc))
        # records first pulled later were written after this pull
        cands = [i for i in by_t_range(by_t, deck_utc - 150, deck_utc) if recs[i].get("pull", "") <= pull]
        if not cands:
            continue
        i = max(cands, key=lambda i: (recs[i]["utc"], pos[i]))
        anchor_shift[i] = off if abs(off) >= 30 else 0

    done = [False] * n
    for root in range(n):
        if succ[root] is not None:
            continue
        chain, cur, seen = [], root, set()
        while cur is not None and cur not in seen and not done[cur]:
            seen.add(cur)
            chain.append(cur)
            cur = pred[cur]
        # the anchor nearest to the end of the chain sets its shift
        shift = 0
        acc = 0
        for i in chain:
            if i in anchor_shift:
                shift = anchor_shift[i] - acc
                break
            acc += recs[i]["clock_jump_s"]
        for i in chain:
            r = recs[i]
            r["shift"] = shift
            r["utc_fixed"] = r["utc"] + shift
            done[i] = True
            shift += r["clock_jump_s"]
    return recs


def by_t_range(by_t, t0, t1):
    for t in range(int(t0), int(t1) + 1):
        yield from by_t.get(t, ())


def plausible(t, now=None):
    now = now if now is not None else datetime.datetime.now(datetime.timezone.utc).timestamp()
    return PLAUSIBLE_FROM <= t <= now + 86400


def build_timeline(recs, capacity=DESIGN_CAPACITY_MAH, now=None, anchors=()):
    """Repaired, sorted records with metrics, plus the ones whose time is unknown."""
    repair_clock(recs, anchors)
    good, unknown = [], []
    for r in recs:
        (good if plausible(r["utc_fixed"], now) else unknown).append(r)
    good.sort(key=lambda r: r["utc_fixed"])
    # two different records at one time: a clock set backwards that the
    # repair could not explain; keep the first, the others go to unknown
    # (unlocking adds a record of 0 s, which may share the minute's time)
    timeline = []
    for r in good:
        if timeline:
            last = timeline[-1]
            if r["utc_fixed"] < last["utc_fixed"] or (r["utc_fixed"] == last["utc_fixed"] and r["secs"] and last["secs"]):
                unknown.append(r)
                continue
        timeline.append(r)
    for r in timeline:
        r["t"] = r["utc_fixed"]
        r["ma"] = -r["avg_ma"]          # consumption, positive while discharging
        r["drain_pct_h"] = round(r["ma"] / capacity * 100, 2)
        r["awake"] = round(max(0.0, 1 - r["asleep_s"] / r["secs"]) if r["secs"] else 1.0, 3)
    mark_gaps(timeline)
    gauge_slope(timeline)
    return timeline, unknown


def mark_gaps(timeline):
    """r["gap"] = seconds without data before r (0 = none)."""
    prev = None
    for r in timeline:
        r["gap"] = 0
        if prev is not None:
            missing = (r["t"] - r["secs"]) - prev["t"]
            if missing > max(120, 2 * r["secs"]):
                r["gap"] = missing
        prev = r


def gauge_slope(timeline, window=7200):
    """%/h from the gauge's state of charge: least squares over +-window/2, within one run.

    The gauge reports whole percent, about one step an hour when locked, so a
    shorter window only sees steps, not a slope.
    """
    n = len(timeline)
    run = 0
    runs = []
    for r in timeline:
        if r["gap"]:
            run += 1
        runs.append(run)
    lo = 0
    hi = 0
    for i, r in enumerate(timeline):
        while timeline[lo]["t"] < r["t"] - window / 2 or runs[lo] != runs[i]:
            lo += 1
        hi = max(hi, i)
        while hi + 1 < n and timeline[hi + 1]["t"] <= r["t"] + window / 2 and runs[hi + 1] == runs[i]:
            hi += 1
        pts = timeline[lo:hi + 1]
        r["gauge_pct_h"] = None
        if len(pts) < 10 or pts[-1]["t"] - pts[0]["t"] < window * 0.6:
            continue
        mt = sum(p["t"] for p in pts) / len(pts)
        mp = sum(p["pct"] for p in pts) / len(pts)
        sxx = sum((p["t"] - mt) ** 2 for p in pts)
        sxy = sum((p["t"] - mt) * (p["pct"] - mp) for p in pts)
        if sxx:
            r["gauge_pct_h"] = round(-sxy / sxx * 3600, 2)


def state_key(r):
    return (tuple(r[f] for f in FLAGS), r["charge"])


def run_key(r):
    return state_key(r) + (r.get("fw", -1),)


def state_label(r):
    on = [f for f in FLAGS if r[f]]
    if r["usb"] and r["charge"]:
        on[on.index("usb")] = "usb (%s)" % CHARGE[r["charge"]]
    return ", ".join(on) if on else "nothing"


def segments(timeline, capacity=DESIGN_CAPACITY_MAH):
    """Runs of one state (flags + charge) and one firmware without a gap."""
    segs, cur = [], None
    for r in timeline:
        if cur is None or r["gap"] or run_key(r) != cur["key"]:
            cur = {"key": run_key(r), "state": state_label(r), "fw": r.get("fw", -1), "recs": []}
            segs.append(cur)
        cur["recs"].append(r)
    out = []
    for s in segs:
        rs = s["recs"]
        dur = sum(r["secs"] for r in rs)
        ma = sum(r["ma"] * r["secs"] for r in rs) / dur if dur else 0
        out.append({
            "t0": rs[0]["t"] - rs[0]["secs"], "t1": rs[-1]["t"], "secs": dur, "n": len(rs),
            "state": s["state"], "fw": s["fw"], "ma": round(ma, 1),
            "pct_h": round(ma / capacity * 100, 2),
            "gauge_pct_h": round((rs[0]["pct"] - rs[-1]["pct"]) / dur * 3600, 2) if dur >= 1800 else None,
            "awake": round(sum(r["awake"] * r["secs"] for r in rs) / dur, 3) if dur else 0,
            "rx": sum(r["rx"] for r in rs), "tx": sum(r["tx"] for r in rs),
        })
    return out


def by_state(segs, capacity=DESIGN_CAPACITY_MAH):
    """Total time and mean current for each state combination."""
    acc = {}
    for s in segs:
        a = acc.setdefault(s["state"], {"state": s["state"], "secs": 0, "mas": 0.0, "runs": 0, "rx": 0, "tx": 0})
        a["secs"] += s["secs"]
        a["mas"] += s["ma"] * s["secs"]
        a["runs"] += 1
        a["rx"] += s["rx"]
        a["tx"] += s["tx"]
    out = []
    for a in acc.values():
        ma = a.pop("mas") / a["secs"] if a["secs"] else 0
        a["ma"] = round(ma, 1)
        a["pct_h"] = round(ma / capacity * 100, 2)
        out.append(a)
    out.sort(key=lambda a: -a["secs"])
    return out


def by_firmware(timeline, firmware, capacity=DESIGN_CAPACITY_MAH):
    """Per firmware (and -1 for unnamed): time logged and current on battery."""
    acc = {}
    for r in timeline:
        a = acc.setdefault(r.get("fw", -1), {"fw": r.get("fw", -1), "n": 0, "secs": 0, "t0": r["t"] - r["secs"],
                                             "t1": r["t"], "bat": 0, "mas": 0.0, "asleep": 0.0})
        a["n"] += 1
        a["secs"] += r["secs"]
        a["t1"] = r["t"]
        if not r["usb"]:
            a["bat"] += r["secs"]
            a["mas"] += r["ma"] * r["secs"]
            a["asleep"] += (1 - r["awake"]) * r["secs"]
    out = []
    for a in acc.values():
        bat = a["bat"]
        ma = a.pop("mas") / bat if bat else None
        a["ma"] = round(ma, 1) if ma is not None else None
        a["pct_h"] = round(ma / capacity * 100, 2) if ma is not None else None
        a["asleep"] = round(a["asleep"] / bat, 3) if bat else None
        info = firmware[a["fw"]] if a["fw"] >= 0 else {}
        a.update({k: info.get(k) for k in ("hash", "dirty", "git_time", "build_time", "version")})
        out.append(a)
    out.sort(key=lambda a: a["t1"], reverse=True)
    return out


def quiet_stretches(timeline, min_secs=3600, margin=600, band_ma=3.0, band_rel=0.25):
    """Long uneventful runs that the viewer may compress.

    A run ends at a gap, a state change, a send, a button wake, a clock jump,
    or when the current stays out of the band around the run's median for 3
    minutes (single packets only spike it). Runs longer than min_secs +
    2 * margin are returned without their margins, so the edges stay readable.
    """
    out = []
    run, odd = [], []

    def close(rs):
        if len(rs) < 3:
            return
        t0 = rs[0]["t"] - rs[0]["secs"] + margin
        t1 = rs[-1]["t"] - margin
        if t1 - t0 >= min_secs:
            ma = sum(r["ma"] for r in rs) / len(rs)
            out.append({"t0": t0, "t1": t1, "ma": round(ma, 1), "state": state_label(rs[0])})

    for r in timeline:
        if r["tx"] or r["wake_btn"]:
            close(run)
            run, odd = [], []
            continue
        if not run or r["gap"] or r["clock_jump_s"] or state_key(r) != state_key(run[0]):
            close(run)
            run, odd = [r], []
            continue
        tail = sorted(x["ma"] for x in run[-60:])
        ref = tail[len(tail) // 2]
        if abs(r["ma"] - ref) > max(band_ma, abs(ref) * band_rel):
            odd.append(r)
            if len(odd) >= 3:           # the level changed: start over from there
                close(run)
                run, odd = odd, []
            continue
        run.extend(odd)
        odd = []
        run.append(r)
    close(run)
    return out
