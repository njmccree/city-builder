import json

import numpy as np
import pytest

from terrain.csr import write_csr
from terrain.package import PackageError, file_checksum, hex64, package_hash, verify_package, write_package_json


def make_package(path):
    write_csr(path / "region" / "elevation.csr", np.arange(16, dtype=np.int16).reshape(4, 4), scale=0.25)
    write_csr(path / "region" / "land_mask.csr", np.ones((4, 4), dtype=np.uint8))
    write_csr(path / "detail" / "0_0.csr", np.zeros((65, 65), dtype=np.int16), scale=0.125)
    write_csr(path / "detail" / "1_0.csr", np.ones((65, 65), dtype=np.int16), scale=0.125)
    (path / "rivers.json").write_text('{"format": 1, "rivers": []}\n')
    (path / "lakes.json").write_text('{"format": 1, "lakes": []}\n')
    return write_package_json(
        path, map_id="test:map.x", sources=[{"name": "synthetic", "version": "1", "license": "none"}],
        projection={"type": "laea", "lat_0": 28.0, "lon_0": 31.0}, bounds_m=(4000.0, 4000.0),
        region={"cell_size_m": 1000.0, "width": 4, "height": 4,
                "layers": {"elevation": "region/elevation.csr", "land_mask": "region/land_mask.csr"}},
        detail={"tile_size_m": 2048.0, "sample_spacing_m": 32.0, "tiles": [[1, 0], [0, 0]]})


def test_manifest_lists_every_file_with_its_checksum(tmp_path):
    manifest = make_package(tmp_path)
    assert sorted(manifest["files"]) == ["detail/0_0.csr", "detail/1_0.csr", "lakes.json", "region/elevation.csr",
                                         "region/land_mask.csr", "rivers.json"]
    assert manifest["files"]["rivers.json"] == hex64(file_checksum(tmp_path / "rivers.json"))
    assert manifest["detail"]["tiles"] == [[0, 0], [1, 0]]  # sorted
    assert json.loads((tmp_path / "package.json").read_text()) == manifest
    assert verify_package(tmp_path) == manifest


def test_package_hash_is_the_hash_of_the_sorted_lines():
    import xxhash
    files = {"b.csr": 2, "a.csr": 1}
    text = "a.csr=0000000000000001\nb.csr=0000000000000002\n"
    assert package_hash(files) == xxhash.xxh3_64_intdigest(text.encode())
    assert package_hash(files) != package_hash({"a.csr": 1, "b.csr": 3})
    assert package_hash(files) != package_hash({"a.csr": 1, "c.csr": 2})


def test_every_kind_of_damage_is_found(tmp_path):
    make_package(tmp_path)
    (tmp_path / "rivers.json").write_text("changed")
    with pytest.raises(PackageError, match="rivers.json has checksum"):
        verify_package(tmp_path)
    make_package(tmp_path)
    (tmp_path / "lakes.json").unlink()
    with pytest.raises(PackageError, match="lakes.json is listed but missing"):
        verify_package(tmp_path)
    make_package(tmp_path)
    write_csr(tmp_path / "detail" / "9_9.csr", np.zeros((65, 65), dtype=np.int16))
    with pytest.raises(PackageError, match="detail/9_9.csr is not listed"):
        verify_package(tmp_path)
    (tmp_path / "detail" / "9_9.csr").unlink()
    manifest = json.loads((tmp_path / "package.json").read_text())
    manifest["package_hash"] = "0" * 16
    (tmp_path / "package.json").write_text(json.dumps(manifest))
    with pytest.raises(PackageError, match="package_hash"):
        verify_package(tmp_path)


def test_a_manifest_naming_a_missing_file_is_refused(tmp_path):
    (tmp_path / "rivers.json").write_text("{}")
    with pytest.raises(PackageError, match="lakes.json"):
        write_package_json(tmp_path, map_id="x", sources=[], projection={}, bounds_m=(1.0, 1.0),
                           region={"layers": {}}, detail={"tiles": []})
