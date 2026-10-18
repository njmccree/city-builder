"""Writes the two small .csr files that the C++ test (sim/tests/world/raster_tests.cpp, "a .csr written by the
Python pipeline reads in C++") reads, so a change to either writer or reader that breaks the shared format
fails a test on the other side. Run from the repository root:

    python tools/terrain/scripts/make_cpp_fixture.py

The files are committed (a few hundred bytes each). The patterns are repeated in the C++ test; change both
together, and only with a format version bump.
"""

from pathlib import Path

import numpy as np

from terrain.csr import write_csr

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


if __name__ == "__main__":
    write_csr(OUT / "python_i16.csr", i16_pattern(), scale=0.25, offset=-5.0, nodata=-32768)
    write_csr(OUT / "python_f32.csr", f32_pattern(), scale=1.0, offset=0.0, nodata=float("nan"))
    for name in ("python_i16.csr", "python_f32.csr"):
        print(f"wrote {OUT / name} ({(OUT / name).stat().st_size} bytes)")
