#!/usr/bin/env python3
"""Backward-compatible entrypoint for the public snapshot refresh service."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT / 'public-data/activity.json')
args = parser.parse_args()
subprocess.run([sys.executable, str(ROOT / 'scripts/refresh-public-data.py'), '--output', str(args.output.parent)], check=True)
