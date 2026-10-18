"""Elevation data: the Copernicus GLO-30 tiles, their mosaic, and reprojection into the map's world frame.

The Copernicus DEM GLO-30 is distributed as 1 degree x 1 degree Cloud-Optimized GeoTIFFs on the AWS Open Data
bucket ``copernicus-dem-30m`` (no account needed). A tile is named by its south-west corner::

    https://copernicus-dem-30m.s3.amazonaws.com/Copernicus_DSM_COG_10_N30_00_E031_00_DEM/Copernicus_DSM_COG_10_N30_00_E031_00_DEM.tif

(``N``/``S`` and two digits of latitude, ``E``/``W`` and three digits of longitude; this one covers 30..31 N and
31..32 E.) Tiles over open sea do not exist: the bucket answers 403 for them, and the pipeline treats the
missing ground as sea.

``build_vrt`` joins the tiles into one GDAL VRT (a small XML file that points at them, nothing is copied) and
``Reprojector`` reads windows of it, reprojected to the map's Lambert azimuthal equal-area frame, as arrays in the
convention of the rest of the pipeline: ``[row, column]`` with row 0 at the SOUTH and cell (column, row) centered
at ``(x0 + (column + 0.5) * cell, z0 + (row + 0.5) * cell)`` in world meters.
"""

from __future__ import annotations

import math
from pathlib import Path
from xml.sax.saxutils import escape

import numpy as np
import rasterio
from rasterio.enums import Resampling
from rasterio.transform import Affine
from rasterio.warp import reproject

from .cache import Source
from .projection import WorldFrame

COPDEM_DATASET = "copernicus-dem-glo30"
COPDEM_BASE_URL = "https://copernicus-dem-30m.s3.amazonaws.com"
# Tiles are fetched slightly beyond the map bounds, so the edge of the map reprojects from real neighbors.
DEFAULT_MARGIN_DEG = 0.05


class DemError(RuntimeError):
    """The elevation data cannot be used (no tiles, tiles that do not match, a window outside the data)."""


def copdem_tile_name(lat: int, lon: int) -> str:
    """The GLO-30 tile whose south-west corner is (lat, lon) degrees, e.g. ``Copernicus_DSM_COG_10_N30_00_E031_00_DEM``."""
    ns = "N" if lat >= 0 else "S"
    ew = "E" if lon >= 0 else "W"
    return f"Copernicus_DSM_COG_10_{ns}{abs(lat):02d}_00_{ew}{abs(lon):03d}_00_DEM"


def copdem_tile_source(lat: int, lon: int, pins: dict[str, dict] | None = None) -> Source:
    """The download for one tile. ``pins`` maps a file name to ``{"size", "sha256"}`` recorded in map.json."""
    stem = copdem_tile_name(lat, lon)
    name = f"{stem}.tif"
    pin = (pins or {}).get(name, {})
    return Source(COPDEM_DATASET, name, f"{COPDEM_BASE_URL}/{stem}/{name}", pin.get("size"), pin.get("sha256"),
                  optional=True)


def tiles_for_bounds(bounds: tuple[float, float, float, float], margin_deg: float = DEFAULT_MARGIN_DEG
                     ) -> list[tuple[int, int]]:
    """The (lat, lon) south-west corners of the 1 degree tiles that cover the bounds plus the margin, in order
    (south to north, then west to east)."""
    min_lon, min_lat, max_lon, max_lat = bounds
    lat_lo = math.floor(max(min_lat - margin_deg, -90.0))
    lat_hi = math.floor(min(max_lat + margin_deg, 89.999999))
    lon_lo = math.floor(max(min_lon - margin_deg, -180.0))
    lon_hi = math.floor(min(max_lon + margin_deg, 179.999999))
    return [(lat, lon) for lat in range(lat_lo, lat_hi + 1) for lon in range(lon_lo, lon_hi + 1)]


def copdem_sources(bounds: tuple[float, float, float, float], pins: dict[str, dict] | None = None,
                   margin_deg: float = DEFAULT_MARGIN_DEG) -> list[Source]:
    return [copdem_tile_source(lat, lon, pins) for lat, lon in tiles_for_bounds(bounds, margin_deg)]


def build_vrt(tile_paths: list[Path], vrt_path: Path) -> None:
    """Writes a VRT that mosaics the GeoTIFF tiles (all in geographic coordinates) into one image.

    The VRT's pixel size is the finest of the tiles' (GLO-30 tiles are 3600 x 3600 pixels at low latitudes and
    have fewer columns toward the poles); coarser tiles are stretched to it. Where no tile covers, the VRT has no
    data."""
    if not tile_paths:
        raise DemError("no DEM tiles to mosaic")
    infos = []
    for path in sorted(tile_paths):
        with rasterio.open(path) as src:
            if src.crs is None or not src.crs.is_geographic:
                raise DemError(f"{path}: expected a tile in geographic coordinates, found {src.crs}")
            if src.transform.b != 0 or src.transform.d != 0 or src.transform.e >= 0:
                raise DemError(f"{path}: expected a north-up tile")
            infos.append((path, src.bounds, src.transform.a, -src.transform.e, src.width, src.height,
                          src.nodata, src.dtypes[0]))
    xres = min(info[2] for info in infos)
    yres = min(info[3] for info in infos)
    left = min(info[1].left for info in infos)
    right = max(info[1].right for info in infos)
    bottom = min(info[1].bottom for info in infos)
    top = max(info[1].top for info in infos)
    width = round((right - left) / xres)
    height = round((top - bottom) / yres)
    nodata = next((info[6] for info in infos if info[6] is not None), None)

    sources = []
    for path, bounds, src_xres, src_yres, src_width, src_height, _nodata, _dtype in infos:
        x_off = round((bounds.left - left) / xres)
        y_off = round((top - bounds.top) / yres)
        x_size = round((bounds.right - bounds.left) / xres)
        y_size = round((bounds.top - bounds.bottom) / yres)
        sources.append(
            f'      <SimpleSource><SourceFilename relativeToVRT="0">{escape(path.resolve().as_posix())}'
            f'</SourceFilename><SourceBand>1</SourceBand>'
            f'<SrcRect xOff="0" yOff="0" xSize="{src_width}" ySize="{src_height}"/>'
            f'<DstRect xOff="{x_off}" yOff="{y_off}" xSize="{x_size}" ySize="{y_size}"/></SimpleSource>')
    nodata_xml = f"      <NoDataValue>{nodata}</NoDataValue>\n" if nodata is not None else ""
    xml = (
        f'<VRTDataset rasterXSize="{width}" rasterYSize="{height}">\n'
        f"  <SRS>EPSG:4326</SRS>\n"
        f"  <GeoTransform>{left!r}, {xres!r}, 0.0, {top!r}, 0.0, {-yres!r}</GeoTransform>\n"
        f'  <VRTRasterBand dataType="Float32" band="1">\n{nodata_xml}' + "\n".join(sources) +
        "\n  </VRTRasterBand>\n</VRTDataset>\n")
    vrt_path.parent.mkdir(parents=True, exist_ok=True)
    vrt_path.write_text(xml, encoding="utf-8")


class Reprojector:
    """Reads windows of a DEM (a GeoTIFF or the VRT) in the map's world frame."""

    def __init__(self, dem_path: Path, frame: WorldFrame):
        self.frame = frame
        self._src = rasterio.open(dem_path)

    def close(self) -> None:
        self._src.close()

    def __enter__(self) -> "Reprojector":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def window(self, x0: float, z0: float, cols: int, rows: int, cell_m: float,
               resampling: Resampling = Resampling.bilinear) -> np.ndarray:
        """The DEM resampled onto the grid whose south-west corner is world (x0, z0): float32 ``[row, column]``
        with row 0 at the south, in meters, and NaN where the source has no data."""
        top_y = self.frame.origin_y + z0 + rows * cell_m
        left_x = self.frame.origin_x + x0
        transform = Affine(cell_m, 0.0, left_x, 0.0, -cell_m, top_y)
        out = np.full((rows, cols), np.nan, dtype=np.float32)
        reproject(source=rasterio.band(self._src, 1), destination=out, src_transform=self._src.transform,
                  src_crs=self._src.crs, dst_transform=transform, dst_crs=self.frame.crs, resampling=resampling,
                  dst_nodata=np.nan, init_dest_nodata=True, num_threads=1)
        return np.flipud(out).copy()
