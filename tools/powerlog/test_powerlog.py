"""Tests for the power log viewer: py -3 -m unittest discover tools/powerlog"""
import math
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pwrlog_format as fmt  # noqa: E402
from store import Store  # noqa: E402

T0 = 1793000000   # 2026-10-26 07:33:20 UTC


def make_line(t, secs=60, slept_ds=550, mv=3900, avg_ma=-14, temp_dc=215, pct=80, chg=0,
              blocker=0, utc=1, wl=1, wb=0, wt=1, rx=0, tx=0, refresh=1, full=0, rc_ppm=12,
              jump=0, flags=None, fix=0, sync=(0, 0, 0, "0.0", "0.00"), app=(0, 0), columns=None):
    """A record printed exactly like meckPwrCsv() in examples/companion_radio/main.cpp."""
    f = {"locked": 1, "saving": 1}
    f.update(flags or {})
    date, hm = fmt.local_date_time(t, utc)
    tdeg = int(temp_dc / 10)                  # C division truncates toward zero
    tfrac = abs(int(math.fmod(temp_dc, 10)))
    line = ("%d,%s,%s,%d,%d.%d,%d,%d,%d,%d,%d,%d,%d.%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
            "%d,%d,%d,%s,%s,%d,%d"
            % ((t, date, hm, secs, slept_ds // 10, slept_ds % 10, wl, wb, wt, pct, mv, avg_ma, tdeg, tfrac,
               f.get("usb", 0), chg, f.get("locked", 0), blocker, f.get("saving", 0), f.get("gps", 0),
               f.get("modem", 0), f.get("wifi", 0), f.get("phone", 0), f.get("ble", 0), f.get("audio", 0),
               f.get("light", 0), rx, tx, refresh, full, jump, utc, rc_ppm, fix) + tuple(sync) + tuple(app)))
    return line if columns is None else ",".join(line.split(",")[:columns])


def series(t0, n, **kw):
    return [make_line(t0 + 60 * i, **kw) for i in range(n)]


class ParseTest(unittest.TestCase):
    def test_header_matches_firmware(self):
        self.assertEqual(len(fmt.FIELDS), 39)
        self.assertEqual(fmt.classify(fmt.HEADER), "header")

    def test_record(self):
        rec, warn = fmt.parse_line(make_line(T0, temp_dc=-15, avg_ma=-14, flags={"wifi": 1}))
        self.assertEqual(warn, [])
        self.assertEqual(rec["utc"], T0)
        self.assertEqual(rec["temp_c"], -1.5)
        self.assertEqual(rec["avg_ma"], -14)
        self.assertEqual(rec["asleep_s"], 55.0)
        self.assertEqual((rec["wifi"], rec["locked"], rec["usb"]), (1, 1, 0))
        self.assertEqual(rec["local_time"], fmt.local_date_time(T0, 1)[1])

    def test_bad_and_noise(self):
        line = make_line(T0)
        self.assertEqual(fmt.classify(line), "record")
        self.assertEqual(fmt.classify(line[:40]), "bad")          # cut off by other output
        self.assertEqual(fmt.classify("[PowerLog] wrote 60 records to SD"), "noise")
        self.assertEqual(fmt.classify("pwrlog sd 20261026"), "noise")
        self.assertEqual(fmt.classify("20261026.csv 8123"), "noise")
        self.assertEqual(fmt.classify(line.replace(",80,", ",8x,", 1)), "bad")

    def test_first_format_without_rc_ppm(self):
        line = make_line(T0, columns=len(fmt.FIELDS) - 2 - len(fmt.SYNC_FIELDS) - len(fmt.APP_FIELDS))
        self.assertEqual(fmt.classify(fmt.HEADER_V1), "header")
        rec, warn = fmt.parse_line(line)
        self.assertEqual((rec["rc_ppm"], rec["fix"], rec["pct"], warn), (0, 0, 80, []))

    def test_second_format_without_fix(self):
        line = make_line(T0, columns=len(fmt.FIELDS) - 1 - len(fmt.SYNC_FIELDS) - len(fmt.APP_FIELDS))
        self.assertEqual(fmt.classify(fmt.HEADER_V2), "header")
        rec, warn = fmt.parse_line(line)
        self.assertEqual((rec["rc_ppm"], rec["fix"], warn), (12, 0, []))

    def test_format_without_sync_columns(self):
        line = make_line(T0, fix=1, columns=len(fmt.FIELDS) - len(fmt.SYNC_FIELDS) - len(fmt.APP_FIELDS))
        self.assertEqual(fmt.classify(fmt.HEADER_V3), "header")
        rec, warn = fmt.parse_line(line)
        self.assertEqual((rec["fix"], rec["ble_win"], rec["sync_s"], warn), (1, 0, 0.0, []))

    def test_sync_columns(self):
        rec, warn = fmt.parse_line(make_line(T0, blocker=9, sync=(10, 1, 1, "12.4", "18.75")))
        self.assertEqual((rec["ble_win"], rec["sync_win"], rec["sync_link"], rec["sync_s"], rec["link_ms"], warn),
                         (10, 1, 1, 12.4, 18.75, []))
        self.assertEqual(fmt.BLOCKERS[rec["blocker"]], "phone sync")

    def test_format_without_app_columns(self):
        line = make_line(T0, sync=(10, 1, 1, "12.4", "18.75"), columns=len(fmt.FIELDS) - len(fmt.APP_FIELDS))
        self.assertEqual(fmt.classify(fmt.HEADER_V4), "header")
        rec, warn = fmt.parse_line(line)
        self.assertEqual((rec["link_ms"], rec["app_rx"], rec["ble_drop"], warn), (18.75, 0, 0, []))

    def test_app_columns(self):
        rec, warn = fmt.parse_line(make_line(T0, app=(14, 0x13)))
        self.assertEqual((rec["app_rx"], rec["ble_drop"], warn), (14, 19, []))

    def test_fix_column(self):
        rec, warn = fmt.parse_line(make_line(T0, flags={"gps": 1}, fix=1))
        self.assertEqual((rec["gps"], rec["fix"], warn), (1, 1, []))

    def test_warnings(self):
        _, warn = fmt.parse_line(make_line(T0, blocker=12, slept_ds=900))
        self.assertEqual(len(warn), 2)


class TimelineTest(unittest.TestCase):
    def recs(self, lines, src="a.csv"):
        out = []
        for l in lines:
            r, _ = fmt.parse_line(l)
            r["src"] = src
            out.append(r)
        return out

    def test_clock_jump_repair(self):
        # deck boots with its clock at 2024-01-01, the phone sets it after 10 min
        wrong = 1704067200
        before = series(wrong, 10)
        fixed = make_line(T0, jump=T0 - (wrong + 600), secs=60)
        after = series(T0 + 60, 5)
        recs = self.recs(before, "20240101.csv") + self.recs([fixed] + after, "20261026.csv")
        tl, unknown = fmt.build_timeline(recs, now=T0 + 3600)
        self.assertEqual(unknown, [])
        self.assertEqual(len(tl), 16)
        self.assertEqual([r["t"] for r in tl], [T0 - 600 + 60 * i for i in range(16)])
        self.assertTrue(all(r["shift"] == T0 - 600 - wrong for r in tl[:10]))
        self.assertEqual(sum(1 for r in tl if r["gap"]), 0)

    def test_boots_sharing_the_default_clock(self):
        # Two resets, each starting at the same default time; only the second
        # boot gets its clock set. Both land in one wrong-date file.
        wrong = 1715777880
        boot1 = series(wrong, 30, avg_ma=-50)
        boot2 = series(wrong + 60, 10, avg_ma=-20)
        fixed = make_line(T0, jump=T0 - (wrong + 600 + 60), secs=60, avg_ma=-20)
        recs = (self.recs(boot1 + boot2, "20240515.csv")
                + self.recs([fixed] + series(T0 + 60, 5, avg_ma=-20), "20261026.csv"))
        tl, unknown = fmt.build_timeline(recs, now=T0 + 3600)
        self.assertEqual(len(unknown), 30)
        self.assertTrue(all(r["avg_ma"] == -50 for r in unknown))   # boot 1 stays unknown
        self.assertEqual(len(tl), 16)
        self.assertTrue(all(r["ma"] == 20 for r in tl))
        self.assertEqual(tl[0]["t"], T0 - 600)

    def test_anchor_from_pull(self):
        # the clock was never set; at the pull the deck said "wrong + 600",
        # the computer said T0 + 600
        wrong = 1715777880
        recs = self.recs(series(wrong, 10))
        for r in recs:
            r["pull"] = "20261026-120000"
        tl, unknown = fmt.build_timeline(recs, now=T0 + 3600,
                                         anchors=[(wrong + 600, T0 + 600.4, "20261026-120000")])
        self.assertEqual(unknown, [])
        self.assertEqual([r["t"] for r in tl], [T0 + 60 * i for i in range(10)])

    def test_unknown_time(self):
        recs = self.recs(series(1704067200, 5) + series(T0, 5))
        tl, unknown = fmt.build_timeline(recs, now=T0 + 3600)
        self.assertEqual((len(tl), len(unknown)), (5, 5))

    def test_gap(self):
        recs = self.recs(series(T0, 5) + series(T0 + 3 * 3600, 5))
        tl, _ = fmt.build_timeline(recs, now=T0 + 86400)
        gaps = [r["gap"] for r in tl if r["gap"]]
        self.assertEqual(gaps, [3 * 3600 - 4 * 60 - 60])

    def test_drain(self):
        lines = [make_line(T0 + 60 * i, avg_ma=-14, pct=80 - i // 10) for i in range(180)]
        tl, _ = fmt.build_timeline(self.recs(lines), capacity=1400, now=T0 + 86400)
        self.assertEqual(tl[0]["ma"], 14)
        self.assertEqual(tl[0]["drain_pct_h"], 1.0)
        # 1 % per 10 min = 6 %/h
        self.assertAlmostEqual(tl[90]["gauge_pct_h"], 6.0, delta=0.3)
        self.assertIsNone(tl[0]["gauge_pct_h"])      # half a window only

    def test_segments_and_quiet(self):
        lines = (series(T0, 240, avg_ma=-14)                                  # 4 h asleep
                 + series(T0 + 240 * 60, 20, avg_ma=-60, flags={"locked": 0, "light": 1})
                 + series(T0 + 260 * 60, 30, avg_ma=-14))
        tl, _ = fmt.build_timeline(self.recs(lines), now=T0 + 86400)
        segs = fmt.segments(tl)
        self.assertEqual([s["n"] for s in segs], [240, 20, 30])
        self.assertEqual(segs[1]["ma"], 60.0)
        q = fmt.quiet_stretches(tl)
        self.assertEqual(len(q), 1)
        self.assertEqual(q[0]["t0"], T0 - 60 + 600)
        self.assertEqual(q[0]["t1"], T0 + 239 * 60 - 600)

    def test_quiet_survives_single_spikes(self):
        lines = [make_line(T0 + 60 * i, avg_ma=-40 if i % 30 == 0 else -14) for i in range(240)]
        tl, _ = fmt.build_timeline(self.recs(lines), now=T0 + 86400)
        self.assertEqual(len(fmt.quiet_stretches(tl)), 1)


class StoreTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.store = Store(self.dir)

    def tearDown(self):
        shutil.rmtree(self.dir)

    def test_repeated_pulls_no_duplicates(self):
        day = series(T0, 60)
        ram = series(T0 + 3600, 10)
        noise = ["dongle", "pwrlog sd 20261026", "[PowerLog] wrote 60 records to SD"]
        self.assertTrue(self.store.is_empty())
        n = self.store.save_pull({"20261026.csv": [fmt.HEADER] + day + noise, "ram.csv": ram})
        self.assertEqual(n, 70)
        # next pull: the RAM records were flushed to the SD meanwhile, plus new RAM
        n = self.store.save_pull({"20261026.csv": [fmt.HEADER] + day + ram,
                                  "ram.csv": series(T0 + 4200, 3)})
        self.assertEqual(n, 3)
        self.assertEqual(self.store.save_pull({"20261026.csv": day + ram}), 0)
        self.assertEqual(len(self.store.pull_dirs()), 2)
        recs, info = self.store.load()
        self.assertEqual(len(recs), 73)
        self.assertEqual(info["bad"], [])
        self.assertEqual(len({r["utc"] for r in recs}), 73)

    def test_firmware_lines(self):
        fw_a = "#fw,9d4bb1d9,1793293985,1793295000,Meck v1.15.1"
        fw_b = "#fw,1234abcd+,1793300000,1793301000,Meck v1.15.1"
        self.assertEqual(fmt.classify(fw_a), "fw")
        self.assertEqual(fmt.parse_fw(fw_b)["dirty"], True)
        old_boot = series(T0, 5)                     # firmware that wrote no #fw line
        boot_a = series(T0 + 3600, 5)
        boot_b = series(T0 + 7200, 5)
        self.store.save_pull({"20261026.csv": [fmt.HEADER] + old_boot + [fw_a] + boot_a[:3],
                              "ram.csv": [fmt.HEADER, fw_a] + boot_a[3:]})
        # later: the RAM records reached the SD, and a newer firmware booted
        self.store.save_pull({"20261026.csv": [fmt.HEADER] + old_boot + [fw_a] + boot_a + [fw_b] + boot_b,
                              "ram.csv": [fmt.HEADER, fw_b] + boot_b[3:]})
        recs, info = self.store.load()
        self.assertEqual(len(recs), 15)
        self.assertEqual([f["hash"] for f in info["firmware"]], ["9d4bb1d9", "1234abcd"])
        by_t = {r["utc"]: r["fw"] for r in recs}
        self.assertEqual([by_t[T0 + 60 * i] for i in range(5)], [-1] * 5)
        self.assertEqual([by_t[T0 + 3600 + 60 * i] for i in range(5)], [0] * 5)
        self.assertEqual([by_t[T0 + 7200 + 60 * i] for i in range(5)], [1] * 5)

    def test_unnamed_boot_after_named_one(self):
        # a boot of older firmware after a named one, in the same file
        fw_a = "#fw,9d4bb1d9,1793293985,1793295000,Meck v1.15.1"
        self.store.save_pull({"20261026.csv": [fw_a] + series(T0, 5) + series(T0 + 3600, 5)})
        recs, info = self.store.load()
        self.assertEqual([r["fw"] for r in recs], [0] * 5 + [-1] * 5)
        tl, _ = fmt.build_timeline(recs, now=T0 + 86400)
        fws = fmt.by_firmware(tl, info["firmware"])
        self.assertEqual(sorted(f["fw"] for f in fws), [-1, 0])
        self.assertEqual(fws[0]["hash"], None)        # newest first: the unnamed boot

    def test_bad_lines_kept_once(self):
        cut = make_line(T0)[:40]
        self.store.save_pull({"a.csv": [cut, make_line(T0)]})
        self.store.save_pull({"a.csv": [cut]})
        recs, info = self.store.load()
        self.assertEqual((len(recs), len(info["bad"])), (1, 1))

    def test_state(self):
        st = self.store.state()
        st["files"]["20261026.csv"] = 1234
        self.store.save_state(st)
        self.assertEqual(self.store.state()["files"]["20261026.csv"], 1234)


if __name__ == "__main__":
    unittest.main()
