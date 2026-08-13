#!/usr/bin/env python3
"""Stamp the build with the git commit it was built from.

`docs/EVIDENCE.md` requires every entry to name a commit SHA -- "a result without a host and
a SHA is not evidence" (`AGENTS.md`) -- and until now the firmware could not state one. The
boot banner printed `firmware : 0.4.1` and a `__DATE__`/`__TIME__` stamp, so two builds of
the same version were indistinguishable on the console, and a board already in the field
could not be matched to a commit at all. On 2026-08-13 an evidence entry had to record its
SHA as *inferred* from a build timestamp. This closes that hole.

Mechanism: a PlatformIO `pre:` extra script. It runs before the build, asks git what the
project directory is checked out at, and appends one `-D FIRMWARE_COMMIT="<sha>"` define.
It never fails the build -- a tarball export or a checkout without history degrades to
`unknown`, because a build that cannot say what it is should say so rather than assert a SHA
it does not have or refuse to compile.

Run this file directly to see what the banner would carry for the current tree:

    python3 scripts/pio_git_rev.py

CITE(datasheet): PlatformIO advanced scripting -- `extra_scripts` with a `pre:` prefix runs
    before the build and receives the SCons construction environment via `Import("env")`
    https://docs.platformio.org/en/latest/scripting/index.html [CIT-PIO-SCRIPTING]
CITE(datasheet): PlatformIO Core `builder/tools/piobuild.py:275` -- `env.StringifyMacro()`
    escapes a value into a C string literal fit for a `CPPDEFINES` tuple
    https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/builder/tools/piobuild.py
    [CIT-PIO-STRINGIFY]
CITE(spec): git-rev-parse -- `--short[=n]` yields the abbreviated object name
    https://git-scm.com/docs/git-rev-parse [CIT-GIT-REV-PARSE]
CITE(spec): git-status -- `--porcelain` is the stable script-readable format; empty output
    means the working tree matches HEAD https://git-scm.com/docs/git-status [CIT-GIT-STATUS]
"""

from __future__ import annotations

import subprocess

try:  # Import() is injected by SCons. Absent when this file is run directly.
    Import("env")  # type: ignore[name-defined]  # noqa: F821
except NameError:
    env = None


def _git(project_dir: str, *args: str) -> str | None:
    """Stripped stdout, or None if git is missing, this is not a repo, or it has no HEAD."""
    try:
        proc = subprocess.run(
            ["git", *args], cwd=project_dir, capture_output=True, text=True, timeout=10
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout.strip()


def commit_id(project_dir: str) -> str:
    """The short SHA, plus `-dirty` when the tree does not match it, else `unknown`."""
    sha = _git(project_dir, "rev-parse", "--short=7", "HEAD")
    if not sha:
        return "unknown"
    # Any porcelain output at all counts as dirty, untracked files included. build_src_filter
    # is `+<*>`, so an untracked .cpp under src/ is compiled into the image -- a bare SHA
    # would then misdescribe the binary, which is the exact failure this stamp exists to stop.
    return f"{sha}-dirty" if _git(project_dir, "status", "--porcelain") else sha


if env is None:
    import os
    import sys

    print(commit_id(os.getcwd()))
    sys.exit(0)

_commit = commit_id(env.subst("$PROJECT_DIR"))  # type: ignore[union-attr]
env.Append(CPPDEFINES=[("FIRMWARE_COMMIT", env.StringifyMacro(_commit))])  # type: ignore[union-attr]
print(f"commit stamp: FIRMWARE_COMMIT={_commit}")
