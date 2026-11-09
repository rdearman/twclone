#!/usr/bin/env python3
"""Compatibility entry point for the complete JSON regression run.

The full suite definition is discovery-based in ``run_suites_all.py``.  This
wrapper intentionally contains no gameplay subset or interactive restart step.
"""

from run_suites_all import run_all


if __name__ == "__main__":
    run_all()
