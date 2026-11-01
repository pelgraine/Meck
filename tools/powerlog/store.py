"""Local history of pulled power logs.

Every pull that brings something new gets its own folder under pulls/,
holding only the lines that were not in the history yet, so nothing is ever
rewritten and the SD card can be wiped without losing the history. Loading
reads all pulls and drops repeated lines: the firmware prints a record the
same way from RAM and from the SD card, so an identical line is the same
record. Lines keep the order they had on the deck: per daily file, the
records of one pull come before those of a later one, and records still in
RAM (ram.csv) after that pull's SD records. pull.json holds the deck's and
the computer's clock at the pull, which dates records the deck wrote before
its clock was ever set. "#fw" lines are kept like headers, in their place, so
the records after them still know their firmware. state.json remembers how big each SD file was when
it was last pulled completely, so the next pull dumps only new or grown files.
"""
import datetime
import json
import os

from pwrlog_format import classify, parse_fw, parse_line


class Store:
    def __init__(self, root):
        self.root = root
        self.pulls = os.path.join(root, "pulls")
        self.state_path = os.path.join(root, "state.json")

    def pull_dirs(self):
        if not os.path.isdir(self.pulls):
            return []
        return sorted(d for d in os.listdir(self.pulls) if os.path.isdir(os.path.join(self.pulls, d)))

    def files(self):
        """(pull, file name, path) of every stored file, oldest pull first."""
        for d in self.pull_dirs():
            base = os.path.join(self.pulls, d)
            for name in sorted(os.listdir(base)):
                if name.endswith(".csv"):
                    yield d, name, os.path.join(base, name)

    def known_lines(self):
        seen = set()
        for _, _, path in self.files():
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    s = line.strip()
                    if classify(s) in ("record", "bad"):
                        seen.add(s)
        return seen

    def is_empty(self):
        return not self.known_lines()

    def load(self):
        """All distinct records in write order per file, and what was skipped.

        Each record gets "fw": its index in info["firmware"], from the last
        "#fw" line before it in its file, or -1. A record that does not follow
        the one before it (a new boot) without a "#fw" line in between was
        written by firmware that did not name itself.
        """
        recs, bad, warnings = [], [], []
        firmware, fw_ids = [], {}
        seen = set()
        dupes = 0
        for pull, name, path in self.files():
            src = "ram" if name.startswith("ram") else name
            fw, prev = -1, None
            with open(path, encoding="utf-8", errors="replace") as f:
                for n, line in enumerate(f, 1):
                    s = line.strip()
                    kind = classify(s)
                    if kind == "fw":
                        info = parse_fw(s)
                        key = (info["hash"], info["dirty"], info["build_time"])
                        if key not in fw_ids:
                            fw_ids[key] = len(firmware)
                            firmware.append(dict(info, id=len(firmware)))
                        fw, prev = fw_ids[key], None
                        continue
                    if kind in ("header", "noise"):
                        continue
                    if kind == "record":
                        rec, warn = parse_line(s)
                        if prev is not None and abs(prev - (rec["utc"] - rec["clock_jump_s"] - rec["secs"])) > 5:
                            fw = -1
                        prev = rec["utc"]
                    if s in seen:
                        dupes += 1
                        continue
                    seen.add(s)
                    where = "%s/%s:%d" % (pull, name, n)
                    if kind == "bad":
                        try:
                            parse_line(s)
                        except ValueError as e:
                            bad.append({"where": where, "line": s, "why": str(e)})
                        continue
                    rec.update(src=src, pull=pull, where=where, fw=fw)
                    for w in warn:
                        warnings.append({"where": where, "why": w})
                    recs.append(rec)
        return recs, {"duplicates": dupes, "bad": bad, "warnings": warnings, "firmware": firmware}

    def anchors(self):
        """(deck_utc, host_utc, pull) of every pull that noted the deck's clock."""
        out = []
        for d in self.pull_dirs():
            try:
                with open(os.path.join(self.pulls, d, "pull.json"), encoding="utf-8") as f:
                    m = json.load(f)
                out.append((m["deck_utc"], m["host_utc"], d))
            except (OSError, ValueError, KeyError):
                pass
        return out

    def save_pull(self, dumps, meta=None):
        """dumps: {file name: [lines]}. Keeps the lines not stored yet (and the
        file's header lines); returns how many records and bad lines were new."""
        seen = self.known_lines()
        new, count = {}, 0
        for name, lines in dumps.items():
            keep, fresh = [], 0
            for line in lines:
                s = line.strip()
                kind = classify(s)
                if kind in ("header", "fw"):
                    keep.append(s)
                elif kind in ("record", "bad") and s not in seen:
                    seen.add(s)
                    keep.append(s)
                    fresh += 1
            if fresh:
                new[name] = keep
                count += fresh
        if not new:
            return 0
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        base = os.path.join(self.pulls, stamp)
        k = 1
        while os.path.exists(base):
            k += 1
            base = os.path.join(self.pulls, "%s-%d" % (stamp, k))
        os.makedirs(base)
        for name, keep in new.items():
            with open(os.path.join(base, name), "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(keep) + "\n")
        if meta:
            with open(os.path.join(base, "pull.json"), "w", encoding="utf-8") as f:
                json.dump(meta, f, indent=1)
        return count

    def state(self):
        try:
            with open(self.state_path, encoding="utf-8") as f:
                return json.load(f)
        except (OSError, ValueError):
            return {"files": {}}

    def save_state(self, state):
        os.makedirs(self.root, exist_ok=True)
        tmp = self.state_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(state, f, indent=1, sort_keys=True)
        os.replace(tmp, self.state_path)
