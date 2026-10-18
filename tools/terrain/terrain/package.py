"""``package.json``, file checksums and the package hash (docs/architecture.md section 15.2).

The manifest lists every file of the package (except ``package.json`` itself) with its checksum: the XXH3-64
of the whole file's bytes as 16 lower-case hex digits. The package hash is the XXH3-64 of one text, a line
``<relative path>=<checksum>\\n`` for each file in byte-wise sorted path order. The C++ side
(``world::compute_package_hash``) computes the same value; ``tests/test_cross_language.py`` checks it.
"""

from __future__ import annotations

import json
from pathlib import Path

import xxhash

from . import PIPELINE_VERSION

PACKAGE_FILE = "package.json"
FORMAT = 1
_CHUNK = 1 << 20


class PackageError(RuntimeError):
    """A package is incomplete or does not match its manifest."""


def hex64(value: int) -> str:
    return f"{value:016x}"


def file_checksum(path: Path) -> int:
    """XXH3-64 of a file's bytes."""
    digest = xxhash.xxh3_64()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(_CHUNK), b""):
            digest.update(chunk)
    return digest.intdigest()


def package_hash(files: dict[str, int]) -> int:
    """The package hash of ``{relative path: checksum}``."""
    text = "".join(f"{path}={hex64(files[path])}\n" for path in sorted(files, key=lambda p: p.encode("utf-8")))
    return xxhash.xxh3_64_intdigest(text.encode("utf-8"))


def list_package_files(package_dir: Path) -> list[str]:
    """Every file under the folder except ``package.json`` and ``map_def.json`` (not part of a package),
    as ``/``-separated relative paths in byte-wise sorted order."""
    root = Path(package_dir)
    names = [path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()]
    names = [name for name in names if name not in (PACKAGE_FILE, "map_def.json") and not name.endswith(".part")]
    return sorted(names, key=lambda p: p.encode("utf-8"))


def write_package_json(package_dir: Path, *, map_id: str, sources: list[dict], projection: dict,
                       bounds_m: tuple[float, float], region: dict, detail: dict,
                       rivers: str = "rivers.json", lakes: str = "lakes.json",
                       pipeline_version: str = PIPELINE_VERSION) -> dict:
    """Checksums every file in the folder and writes ``package.json``. Returns the manifest.

    ``region`` is ``{"cell_size_m", "width", "height", "layers": {name: relative file}}`` and ``detail`` is
    ``{"tile_size_m", "sample_spacing_m", "tiles": [[tx, ty], ...]}``, as in the C++ ``PackageManifest``.
    """
    package_dir = Path(package_dir)
    files = {name: file_checksum(package_dir / name) for name in list_package_files(package_dir)}
    for needed in [rivers, lakes, *region["layers"].values(),
                   *(f"detail/{tx}_{ty}.csr" for tx, ty in detail["tiles"])]:
        if needed not in files:
            raise PackageError(f"{package_dir}: the manifest names {needed}, which is not in the package folder")
    manifest = {
        "format": FORMAT,
        "map_id": map_id,
        "pipeline_version": pipeline_version,
        "sources": sources,
        "projection": projection,
        "bounds_m": {"width": bounds_m[0], "height": bounds_m[1]},
        "region": region,
        "detail": {**detail, "tiles": sorted([int(tx), int(ty)] for tx, ty in detail["tiles"])},
        "rivers": rivers,
        "lakes": lakes,
        "files": {name: hex64(files[name]) for name in files},
        "package_hash": hex64(package_hash(files)),
    }
    (package_dir / PACKAGE_FILE).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def verify_package(package_dir: Path) -> dict:
    """Checks every listed file and the package hash against the manifest. Returns the manifest."""
    package_dir = Path(package_dir)
    try:
        manifest = json.loads((package_dir / PACKAGE_FILE).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PackageError(f"{package_dir / PACKAGE_FILE}: cannot read the manifest: {error}") from None
    problems: list[str] = []
    files: dict[str, int] = {}
    for name, listed in manifest.get("files", {}).items():
        path = package_dir / name
        if not path.is_file():
            problems.append(f"{name} is listed but missing")
            continue
        actual = file_checksum(path)
        files[name] = actual
        if hex64(actual) != listed:
            problems.append(f"{name} has checksum {hex64(actual)}, the manifest says {listed}")
    for name in list_package_files(package_dir):
        if name.startswith("detail/") and name not in manifest.get("files", {}):
            problems.append(f"{name} is not listed")
    if not problems and hex64(package_hash(files)) != manifest.get("package_hash"):
        problems.append(f"package_hash is {manifest.get('package_hash')}, the file list gives {hex64(package_hash(files))}")
    if problems:
        shown = "; ".join(problems[:5]) + (f"; and {len(problems) - 5} more" if len(problems) > 5 else "")
        raise PackageError(f"{package_dir}: {len(problems)} problem(s): {shown}")
    return manifest
