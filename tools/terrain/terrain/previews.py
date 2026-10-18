"""Preview images (hillshade PNGs) for the designer to check a build by eye. They are not part of the package.

A hillshade lights the terrain from the north-west; sea is blue, land is a gray-green shaded by the light. The
PNG writer is a few lines on zlib so the pipeline needs no imaging library.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

import numpy as np

_SEA = np.array([62, 104, 160], dtype=np.float32)
_LAND_LOW = np.array([106, 140, 86], dtype=np.float32)    # lowland green
_LAND_HIGH = np.array([196, 176, 130], dtype=np.float32)  # desert and rock tan


def write_png(path: Path | str, rgb: np.ndarray) -> None:
    """Writes an (height, width, 3) uint8 array as a PNG. Row 0 of the array is the top of the image."""
    array = np.ascontiguousarray(rgb, dtype=np.uint8)
    if array.ndim != 3 or array.shape[2] != 3:
        raise ValueError("write_png needs an (height, width, 3) uint8 array")
    height, width, _ = array.shape
    raw = b"".join(b"\x00" + array[row].tobytes() for row in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def hillshade(dem: np.ndarray, cell_m: float, *, azimuth_deg: float = 315.0, altitude_deg: float = 45.0,
              z_factor: float = 1.0) -> np.ndarray:
    """Light from 0 to 1 for a DEM (``[row, column]``, row 0 at the south, meters). Edges use one-sided slopes."""
    z = np.nan_to_num(dem.astype(np.float64), nan=0.0) * z_factor
    # Rows grow northward, so d/dz (north) is the gradient along axis 0.
    d_north, d_east = np.gradient(z, cell_m)
    slope = np.arctan(np.hypot(d_east, d_north))
    aspect = np.arctan2(-d_east, -d_north)  # downhill direction (the way the surface faces), clockwise from north
    zenith = np.radians(90.0 - altitude_deg)
    azimuth = np.radians(azimuth_deg)
    light = np.cos(zenith) * np.cos(slope) + np.sin(zenith) * np.sin(slope) * np.cos(azimuth - aspect)
    return np.clip(light, 0.0, 1.0)


def shaded_image(dem: np.ndarray, land: np.ndarray, cell_m: float, *, min_width: int = 600,
                 max_width: int = 1600, z_factor: float = 2.0) -> np.ndarray:
    """An (height, width, 3) uint8 image of the terrain, north at the top, enlarged (whole-number factor) to be
    at least ``min_width`` wide and shrunk by sampling when wider than ``max_width``."""
    light = hillshade(dem, cell_m, z_factor=z_factor)
    height_m = np.clip(np.nan_to_num(dem, nan=0.0), 0.0, 400.0) / 400.0
    base = _LAND_LOW[None, None, :] * (1.0 - height_m[..., None]) + _LAND_HIGH[None, None, :] * height_m[..., None]
    shade = (0.35 + 0.65 * light)[..., None]
    rgb = np.where(land[..., None], base * shade, _SEA[None, None, :] * (0.8 + 0.2 * light[..., None]))
    image = np.flipud(np.clip(rgb, 0, 255).astype(np.uint8))  # north up
    width = image.shape[1]
    if width < min_width:
        factor = -(-min_width // width)
        image = np.repeat(np.repeat(image, factor, axis=0), factor, axis=1)
    elif width > max_width:
        step = -(-width // max_width)
        image = image[::step, ::step]
    return image
