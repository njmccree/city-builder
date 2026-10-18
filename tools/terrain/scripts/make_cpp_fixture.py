"""Writes the two small .csr files that the C++ test (sim/tests/world/raster_tests.cpp, "a .csr written by the
Python pipeline reads in C++") reads, so a change to either writer or reader that breaks the shared format
fails a test on the other side. Run from the repository root:

    python tools/terrain/scripts/make_cpp_fixture.py

The files are committed (a few hundred bytes each). The patterns are repeated in the C++ test; change both
together, and only with a format version bump.
"""

import json
from pathlib import Path

import numpy as np

from terrain.corrections import RiverLine
from terrain.csr import write_csr
from terrain.rivermodel import assign_discharge, assign_orders, assign_width_depth, build_reaches, group_rivers
from terrain.rivermodel import RiverParams, river_documents

OUT = Path(__file__).resolve().parents[3] / "sim" / "tests" / "fixtures" / "maps"


def i16_pattern() -> np.ndarray:
    """20 x 11 i16: raw(x, y) = (x * 7 + y * 13) % 200 - 100, with no-data (-32768) at (0, 0) and (19, 10)."""
    y, x = np.mgrid[0:11, 0:20]
    data = ((x * 7 + y * 13) % 200 - 100).astype(np.int16)
    data[0, 0] = -32768
    data[10, 19] = -32768
    return data


def f32_pattern() -> np.ndarray:
    """7 x 5 f32: value(x, y) = x * 0.5 - y, NaN (the no-data value) at (3, 2)."""
    y, x = np.mgrid[0:5, 0:7]
    data = (x * 0.5 - y).astype(np.float32)
    data[2, 3] = np.nan
    return data


def rivers_document() -> dict:
    """A rivers.json made by the pipeline's own writer: two arms (A, B) meeting into a trunk, run south."""
    def line(name, ident, points):
        pts = np.array(points, dtype=float)
        return RiverLine(ident, name, pts, np.full(len(pts), 50.0), np.full(len(pts), 3.0))

    reaches = build_reaches([line("A", "a", [(-5000, 5000), (0, 0)]), line("B", "b", [(5000, 5000), (0, 0)]),
                             line("Trunk", "t", [(0, 0), (0, -9000)])], 600.0)
    for reach in reaches:
        reach.own_discharge = 300.0
    assign_discharge(reaches)
    assign_orders(reaches)
    assign_width_depth(reaches, RiverParams())
    for reach in reaches:
        reach.levels = np.linspace(20.0, 10.0, len(reach.points)) if reach.line_id.startswith("t") else             np.linspace(30.0, 20.0, len(reach.points))
        reach.floodplain = [np.array([[-300.0, -300.0], [300.0, -300.0], [300.0, 300.0], [-300.0, -300.0]])]
    return {"format": 1, "rivers": river_documents(reaches, group_rivers(reaches))}


if __name__ == "__main__":
    write_csr(OUT / "python_i16.csr", i16_pattern(), scale=0.25, offset=-5.0, nodata=-32768)
    write_csr(OUT / "python_f32.csr", f32_pattern(), scale=1.0, offset=0.0, nodata=float("nan"))
    for name in ("python_i16.csr", "python_f32.csr"):
        print(f"wrote {OUT / name} ({(OUT / name).stat().st_size} bytes)")
    text = json.dumps(rivers_document(), separators=(",", ":"))
    (OUT / "python_rivers.json").write_text(text + chr(10), encoding="utf-8")
    print(f"wrote {OUT / 'python_rivers.json'}")
