"""``python -m terrain.build_map <map.json> [--out DIR] [--steps a,b,c] [--offline]``

Builds the map package of the map that ``map.json`` describes (docs/architecture.md section 15.5). Exit status:
0 success, 1 a step failed, 2 a usage error.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import PIPELINE_VERSION
from .cache import CacheError, DownloadCache
from .steps import STEP_ORDER, Context, StepError, load_map_def, select_steps

REPO_ROOT = Path(__file__).resolve().parents[3]


def default_out_dir(map_def: dict) -> Path:
    """``game/content/<mod>/maps/<package>`` (the mod is "base" unless map.json says otherwise)."""
    return REPO_ROOT / "game" / "content" / map_def.get("mod", "base") / "maps" / map_def["package"]


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m terrain.build_map",
        description="Build a map package from open datasets (docs/architecture.md section 15).")
    parser.add_argument("map_file", type=Path, help="the map's map.json")
    parser.add_argument("--out", type=Path, help="the package folder (default: game/content/<mod>/maps/<package>)")
    parser.add_argument("--steps", help=f"comma-separated steps to run, from: {', '.join(STEP_ORDER)} (default: all)")
    parser.add_argument("--offline", action="store_true",
                        help="never download: fail at once if a needed file is not in the cache")
    parser.add_argument("--cache", type=Path, help="the download cache folder (default: ~/.cache/citysim/terrain)")
    parser.add_argument("--version", action="version", version=f"terrain pipeline {PIPELINE_VERSION}")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    try:
        names = [name.strip() for name in args.steps.split(",") if name.strip()] if args.steps else None
        if args.steps is not None and not names:
            raise StepError("--steps needs at least one step name")
        steps = select_steps(names)
        map_def = load_map_def(args.map_file)
    except StepError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    context = Context(map_file=args.map_file, map_def=map_def, out_dir=args.out or default_out_dir(map_def),
                      cache=DownloadCache(args.cache, offline=args.offline), offline=args.offline)
    try:
        if args.offline and any(step.needs_downloads for step in steps):
            context.cache.require_offline(context.sources())  # fail before any work starts
        context.out_dir.mkdir(parents=True, exist_ok=True)
        context.log(f"building {map_def['map_id']} into {context.out_dir}")
        for step in steps:
            context.log(f"== {step.name}: {step.description}")
            step.run(context)
    except (StepError, CacheError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    context.log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
