"""``.csr`` rasters: the one raster format of map packages (docs/architecture.md section 15.2).

Layout, little-endian, the same as ``sim/src/world/raster.cpp`` reads and writes::

    offset size  field
    0      4     magic "CSRA"
    4      2     format version (u16, currently 1)
    6      4     width in cells (u32)
    10     4     height in cells (u32)
    14     1     element type (1 = u8, 2 = u16, 3 = i16, 4 = f32)
    15     1     flags (bit 0: the raster has a no-data value)
    16     4     scale (f32)
    20     4     offset (f32)
    24     8     no-data value (f64; 0 when the flag is not set)
    32     8     payload size in bytes before compression (u64)
    40     8     compressed payload size in bytes (u64)
    48     n     one zstd frame of the cells, row-major, row 0 = SOUTH, each in the element type
    48+n   8     XXH3-64 checksum (u64) of every byte before it

A stored value ``raw`` means ``raw * scale + offset``. Arrays are indexed ``[row, column]`` with row 0 at the
south edge (so ``array[y, x]`` is the cell at northing ``y``).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import xxhash
import zstandard

MAGIC = b"CSRA"
VERSION = 1
HEADER_SIZE = 48
CHECKSUM_SIZE = 8
COMPRESSION_LEVEL = 3  # the C++ writer uses the same level
FLAG_NODATA = 1
# The same bound as the C++ reader: a file claiming more cells than this is refused.
MAX_CELLS = 1 << 31

_TYPE_CODES = {np.dtype("uint8"): 1, np.dtype("uint16"): 2, np.dtype("int16"): 3, np.dtype("float32"): 4}
_CODE_TYPES = {code: dtype for dtype, code in _TYPE_CODES.items()}
_TYPE_NAMES = {1: "u8", 2: "u16", 3: "i16", 4: "f32"}

_HEADER = struct.Struct("<4sHIIBBffdQQ")
assert _HEADER.size == HEADER_SIZE


class CsrError(ValueError):
    """A ``.csr`` file is not what the format says (the message names the file and the reason)."""


@dataclass(frozen=True)
class CsrHeader:
    width: int
    height: int
    dtype: np.dtype
    scale: float = 1.0
    offset: float = 0.0
    nodata: float | None = None

    @property
    def type_name(self) -> str:
        return _TYPE_NAMES[_TYPE_CODES[self.dtype]]


@dataclass
class CsrRaster:
    """A decoded raster: ``data`` is (height, width) of the stored dtype, row 0 at the south."""

    data: np.ndarray
    scale: float = 1.0
    offset: float = 0.0
    nodata: float | None = None

    @property
    def header(self) -> CsrHeader:
        height, width = self.data.shape
        return CsrHeader(width, height, self.data.dtype, self.scale, self.offset, self.nodata)

    def values(self) -> np.ndarray:
        """The decoded values ``raw * scale + offset`` as float64, with NaN where the cell is no-data."""
        decoded = self.data.astype(np.float64) * float(np.float32(self.scale)) + float(np.float32(self.offset))
        if self.nodata is not None:
            if np.isnan(self.nodata):
                missing = np.isnan(self.data) if self.data.dtype.kind == "f" else np.zeros(self.data.shape, bool)
            else:
                missing = self.data == self.nodata
            decoded[missing] = np.nan
        return decoded


def _type_code(dtype: np.dtype) -> int:
    try:
        return _TYPE_CODES[np.dtype(dtype)]
    except KeyError:
        raise ValueError(f"a .csr raster holds u8, u16, i16 or f32 cells, not {np.dtype(dtype)}") from None


def encode_csr(data: np.ndarray, *, scale: float = 1.0, offset: float = 0.0, nodata: float | None = None) -> bytes:
    """The bytes of a ``.csr`` file for a 2D array (row 0 = south). Deterministic for one zstandard version."""
    array = np.asarray(data)
    if array.ndim != 2 or array.shape[0] == 0 or array.shape[1] == 0:
        raise ValueError(f"a .csr raster must be a non-empty 2D array, got shape {array.shape}")
    code = _type_code(array.dtype)
    height, width = array.shape
    raw = np.ascontiguousarray(array).astype(array.dtype.newbyteorder("<"), copy=False).tobytes()
    payload = zstandard.ZstdCompressor(level=COMPRESSION_LEVEL).compress(raw)
    flags = FLAG_NODATA if nodata is not None else 0
    header = _HEADER.pack(MAGIC, VERSION, width, height, code, flags, np.float32(scale), np.float32(offset),
                          float(nodata) if nodata is not None else 0.0, len(raw), len(payload))
    body = header + payload
    return body + struct.pack("<Q", xxhash.xxh3_64_intdigest(body))


def write_csr(path: str | Path, data: np.ndarray, *, scale: float = 1.0, offset: float = 0.0,
              nodata: float | None = None) -> None:
    """Writes a ``.csr`` file, creating its folder."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_csr(data, scale=scale, offset=offset, nodata=nodata))


def _parse_header(blob: bytes, source: str) -> tuple[CsrHeader, int, int]:
    if len(blob) < HEADER_SIZE:
        raise CsrError(f"{source}: not a .csr file: only {len(blob)} bytes, shorter than the {HEADER_SIZE}-byte header")
    (magic, version, width, height, code, flags, scale, offset, nodata, raw_size,
     compressed_size) = _HEADER.unpack_from(blob)
    if magic != MAGIC:
        raise CsrError(f'{source}: not a .csr file: the first four bytes are not "CSRA"')
    if version != VERSION:
        raise CsrError(f"{source}: unsupported .csr format version {version} (this reader reads version {VERSION})")
    if code not in _CODE_TYPES:
        raise CsrError(f"{source}: unknown element type code {code}")
    if flags & ~FLAG_NODATA:
        raise CsrError(f"{source}: unknown flags {flags}")
    if width == 0 or height == 0:
        raise CsrError(f"{source}: the raster is empty ({width} x {height})")
    if width * height > MAX_CELLS:
        raise CsrError(f"{source}: the raster is implausibly large ({width * height} cells)")
    dtype = _CODE_TYPES[code]
    if raw_size != width * height * dtype.itemsize:
        raise CsrError(f"{source}: the payload size {raw_size} does not match {width} x {height} cells of "
                       f"{_TYPE_NAMES[code]}")
    header = CsrHeader(width, height, dtype, float(scale), float(offset),
                       nodata if flags & FLAG_NODATA else None)
    return header, raw_size, compressed_size


def decode_csr(blob: bytes, source: str = "<bytes>") -> CsrRaster:
    """Decodes and verifies a whole ``.csr`` file (checksum included)."""
    header, raw_size, compressed_size = _parse_header(blob, source)
    expected_size = HEADER_SIZE + compressed_size + CHECKSUM_SIZE
    if len(blob) != expected_size:
        raise CsrError(f"{source}: the file is {len(blob)} bytes but its header describes {expected_size}")
    stored = struct.unpack_from("<Q", blob, len(blob) - CHECKSUM_SIZE)[0]
    if xxhash.xxh3_64_intdigest(blob[:-CHECKSUM_SIZE]) != stored:
        raise CsrError(f"{source}: the checksum does not match (the file is corrupt)")
    try:
        raw = zstandard.ZstdDecompressor().decompress(blob[HEADER_SIZE:HEADER_SIZE + compressed_size],
                                                      max_output_size=raw_size)
    except zstandard.ZstdError as error:
        raise CsrError(f"{source}: the payload cannot be decompressed: {error}") from None
    if len(raw) != raw_size:
        raise CsrError(f"{source}: the payload decompressed to {len(raw)} bytes, not {raw_size}")
    data = np.frombuffer(raw, dtype=header.dtype.newbyteorder("<")).reshape(header.height, header.width)
    return CsrRaster(data.astype(header.dtype, copy=True), header.scale, header.offset, header.nodata)


def read_csr(path: str | Path) -> CsrRaster:
    """Reads and verifies a ``.csr`` file."""
    path = Path(path)
    try:
        blob = path.read_bytes()
    except OSError as error:
        raise CsrError(f"{path}: cannot read the file: {error.strerror or error}") from None
    return decode_csr(blob, str(path))


def read_csr_header(path: str | Path) -> CsrHeader:
    """Reads only the header (size and type), without the cells; the checksum is not checked."""
    path = Path(path)
    try:
        with path.open("rb") as handle:
            blob = handle.read(HEADER_SIZE)
    except OSError as error:
        raise CsrError(f"{path}: cannot read the file: {error.strerror or error}") from None
    return _parse_header(blob, str(path))[0]
