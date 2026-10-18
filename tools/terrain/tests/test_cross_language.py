"""The contracts shared with the C++ side: .csr files and the package manifest.

Needs citysim_mapgen (build the C++ first, or set CITYSIM_MAPGEN); skipped otherwise. The reverse direction,
C++ reading a .csr written by Python, is a C++ test on a committed fixture (sim/tests/world/raster_tests.cpp).
"""

import json
import subprocess

import numpy as np

from terrain.csr import read_csr, read_csr_header
from terrain.package import file_checksum, hex64, package_hash, verify_package


def make_synthetic(mapgen, out, seed=1):
    subprocess.run([str(mapgen), "--synthetic", "--out", str(out), "--seed", str(seed), "--size-km", "16"],
                   check=True, capture_output=True)
    return json.loads((out / "package.json").read_text())


def test_python_reads_every_raster_the_cpp_tool_writes(mapgen, tmp_path):
    manifest = make_synthetic(mapgen, tmp_path / "pkg")
    elevation = read_csr(tmp_path / "pkg" / manifest["region"]["layers"]["elevation"])
    assert elevation.data.dtype == np.int16 and elevation.scale == 0.25
    assert elevation.data.shape == (16, 16)
    land = read_csr(tmp_path / "pkg" / manifest["region"]["layers"]["land_mask"])
    assert np.array_equal(land.data == 1, elevation.data > 0)  # the C++ tool's own invariant
    # row 0 is the south edge: land in the south, sea in the north
    assert (elevation.data[0] > 0).all() and (elevation.data[-1] < 0).all()
    for tx, ty in manifest["detail"]["tiles"][:3]:
        header = read_csr_header(tmp_path / "pkg" / f"detail/{tx}_{ty}.csr")
        assert (header.width, header.height, header.scale) == (65, 65, 0.125)


def test_python_computes_the_same_checksums_and_package_hash(mapgen, tmp_path):
    manifest = make_synthetic(mapgen, tmp_path / "pkg")
    files = {}
    for name, listed in manifest["files"].items():
        files[name] = file_checksum(tmp_path / "pkg" / name)
        assert hex64(files[name]) == listed
    assert hex64(package_hash(files)) == manifest["package_hash"]
    assert verify_package(tmp_path / "pkg")["package_hash"] == manifest["package_hash"]
