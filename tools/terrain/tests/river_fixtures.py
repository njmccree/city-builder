"""Natural Earth style river and lake zips on the synthetic ground of fixtures.py (nothing touches the network)."""

from __future__ import annotations

import shutil
import tempfile
import zipfile
from pathlib import Path

import shapefile

from terrain.rivermodel import LAKES_FILE, NATURAL_EARTH_DATASET, RIVERS_FILE

# The synthetic ground falls to the south-west: a main river, a tributary, a rank-12 stream that the discharge
# threshold drops. (name, scalerank, rivernum, lon/lat points)
RIVER_LINES = [
    ("Big River", 1, 1, [(30.56, 30.46), (30.45, 30.36), (30.32, 30.24)]),
    ("Side Creek", 3, 2, [(30.36, 30.46), (30.40, 30.40), (30.4005, 30.3153)]),
    ("Tiny Brook", 12, 3, [(30.5, 30.3), (30.45, 30.25)]),
]


def _write_shape_zip(path: Path, stem: str, fields: list[str], shapes: list[tuple[str, list, list]]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="ne_"))
    try:
        writer = shapefile.Writer(str(work / stem))
        for name in fields:
            writer.field(name, "N" if name in ("scalerank", "rivernum") else "C")
        for kind, parts, record in shapes:
            (writer.line if kind == "line" else writer.poly)(parts)
            writer.record(*record)
        writer.close()
        with zipfile.ZipFile(path, "w") as archive:
            for suffix in (".shp", ".shx", ".dbf"):
                archive.write((work / stem).with_suffix(suffix), f"{stem}{suffix}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return path


def write_rivers_zip(path: Path, lines=RIVER_LINES) -> Path:
    shapes = [("line", [[list(p) for p in points]], ["River", name, rank, num]) for name, rank, num, points in lines]
    shapes.append(("line", [[[30.3, 30.3], [30.4, 30.3]]], ["Lake Centerline", "Lake Line", 1, 99]))
    return _write_shape_zip(path, RIVERS_FILE[:-4], ["featurecla", "name", "scalerank", "rivernum"], shapes)


def write_lakes_zip(path: Path, lakes) -> Path:
    """``lakes``: (name, [(lon, lat), ...] ring), clockwise."""
    shapes = [("poly", [[list(p) for p in ring]], [name]) for name, ring in lakes]
    return _write_shape_zip(path, LAKES_FILE[:-4], ["name"], shapes)


def seed_river_cache(cache_root: Path, lakes=()) -> None:
    write_rivers_zip(cache_root / NATURAL_EARTH_DATASET / RIVERS_FILE)
    write_lakes_zip(cache_root / NATURAL_EARTH_DATASET / LAKES_FILE, list(lakes))
