"""
PlatformIO pre-build script: tell the companion firmware which commit it was
built from, for the power log (examples/companion_radio/main.cpp, "#fw" line).

Defines, for main.cpp only (so a new build time does not rebuild everything):
  MECK_GIT_HASH    short commit hash, "+" appended when tracked files differ
  MECK_GIT_TIME    commit time, UTC epoch
  MECK_BUILD_TIME  build time, UTC epoch
Without git (a source archive) the hash is "unknown" and the commit time 0.
"""
import subprocess
import time

Import("env")


def git(*args):
    try:
        out = subprocess.run(["git", "-C", env["PROJECT_DIR"]] + list(args),
                             capture_output=True, text=True, timeout=20)
        return out.stdout.strip() if out.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        return None


ghash = git("rev-parse", "--short=8", "HEAD") or "unknown"
if git("status", "--porcelain", "--untracked-files=no"):
    ghash += "+"
gtime = git("log", "-1", "--format=%ct") or "0"
defines = [
    ("MECK_GIT_HASH", env.StringifyMacro(ghash)),
    ("MECK_GIT_TIME", gtime + "UL"),
    ("MECK_BUILD_TIME", "%dUL" % time.time()),
]
print("git_version: %s, commit %s" % (ghash, gtime))


def add_version(env, node):
    return env.Object(node, CPPDEFINES=list(env["CPPDEFINES"]) + defines)


env.AddBuildMiddleware(add_version, "*companion_radio*main.cpp")
