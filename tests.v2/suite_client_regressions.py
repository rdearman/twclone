"""Run offline regression tests for client changes that are part of V2.

This suite is intentionally independent of the live server and database. It is
discovered by run_suites_all.py alongside the protocol integration suites.
It covers supported/unsupported menu commands, server refusals, metadata-driven
art selection, and loading every shared-asset variant.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
GODOT_TEST_DIR = ROOT / "client/godot_client/godot/tests"


def run(command, *, cwd, env=None):
    result = subprocess.run(command, cwd=cwd, env=env, check=False)
    return result.returncode == 0


def find_godot():
    configured = os.environ.get("GODOT_BIN")
    if configured and Path(configured).is_file():
        return configured
    discovered = shutil.which("godot")
    if discovered:
        return discovered
    home_candidate = Path.home() / "bin/Godot_v4.5.1-stable_linux.x86_64"
    if home_candidate.is_file():
        return str(home_candidate)
    return None


def main():
    ok = run(
        [sys.executable, "-m", "pytest", "client/python_client/tests"],
        cwd=ROOT,
    )
    if not ok:
        print("Python client regression tests failed.", file=sys.stderr)
        return 1

    godot = find_godot()
    if not godot:
        print("SKIP: Godot client regressions (set GODOT_BIN or install godot).")
        # Python client coverage remains useful on hosts without Godot.
        godot_ok = True
    else:
        project = ROOT / "client/godot_client/godot"
        scripts = sorted(GODOT_TEST_DIR.glob("*_test.gd"))
        env = os.environ.copy()
        with tempfile.TemporaryDirectory(prefix="twclone-godot-user-") as user_data_dir:
            env["XDG_DATA_HOME"] = user_data_dir
            godot_ok = True
            # Recreate Godot's imported texture cache on clean checkouts; source
            # artwork alone does not populate res://.godot/imported resources.
            if not run([godot, "--headless", "--editor", "--path", str(project), "--import", "--quit"], cwd=ROOT, env=env):
                print("Godot asset import failed.", file=sys.stderr)
                return 1
            for path in scripts:
                script = "res://tests/" + path.name
                if not run([godot, "--headless", "--path", str(project), "--script", script], cwd=ROOT, env=env):
                    print(f"Godot client regression failed: {script}", file=sys.stderr)
                    godot_ok = False
                    break

    if not run([sys.executable, "assets/tools/validate_catalog.py"], cwd=ROOT):
        print("Shared asset catalogue validation failed.", file=sys.stderr)
        return 1
    return 0 if godot_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
