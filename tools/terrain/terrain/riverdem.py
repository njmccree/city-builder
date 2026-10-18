"""River geometry that needs the ground: snapping a coarse river line onto the real channel, the lowest ground across a
channel, and the floodplain around it.

Everything reads the DEM through a ``DemSource``: an object with ``window(x0, z0, cols, rows, cell_m, resampling)``
that returns a float32 ``[row, column]`` array (row 0 at the south, NaN where there is no data) of the ground on the
grid whose south-west corner is world ``(x0, z0)``. ``dem.Reprojector`` is one; tests use a function of position.
Coordinates are world meters (x east, z north).
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Protocol

import numpy as np
import shapely
from rasterio.enums import Resampling
from rasterio.features import shapes
from rasterio.transform import Affine
from shapely.geometry import LineString, Polygon, shape
from shapely.ops import unary_union

_CHUNK_VERTICES = 80  # vertices whose DEM window is read at once


class DemSource(Protocol):
    def window(self, x0: float, z0: float, cols: int, rows: int, cell_m: float,
               resampling: Resampling = Resampling.bilinear) -> np.ndarray: ...


@dataclass(frozen=True)
class SnapParams:
    """How a coarse river line is pulled onto the channel the DEM shows (see ``snap_to_channel``)."""

    enabled: bool = True
    radius_m: float = 800.0      # how far from the line the real channel may be
    step_m: float = 30.0         # spacing of the candidate positions, and of the DEM read for it
    half_width_m: float = 90.0   # the strip across the channel whose mean height scores a candidate
    penalty_m_per_km: float = 0.5  # elevation a km of sideways move must save, so the line does not jump to a canal
    smooth_vertices: int = 15    # the offsets are median filtered, then averaged, over about this many vertices


def sample_bilinear(window: np.ndarray, x0: float, z0: float, cell_m: float, xs: np.ndarray, zs: np.ndarray
                    ) -> np.ndarray:
    """The ground at points, bilinearly interpolated between cell centers of a window (clamped at its edges)."""
    rows, cols = window.shape
    fx = np.clip((xs - x0) / cell_m - 0.5, 0.0, cols - 1.0)
    fz = np.clip((zs - z0) / cell_m - 0.5, 0.0, rows - 1.0)
    c0 = np.minimum(np.floor(fx).astype(int), cols - 2)
    r0 = np.minimum(np.floor(fz).astype(int), rows - 2)
    c0 = np.maximum(c0, 0)
    r0 = np.maximum(r0, 0)
    tx = fx - c0
    tz = fz - r0
    c1 = np.minimum(c0 + 1, cols - 1)
    r1 = np.minimum(r0 + 1, rows - 1)
    return ((1 - tx) * (1 - tz) * window[r0, c0] + tx * (1 - tz) * window[r0, c1]
            + (1 - tx) * tz * window[r1, c0] + tx * tz * window[r1, c1])


class WindowSampler:
    """Samples the DEM at arbitrary points by reading one window that covers them (missing data reads as 0 m)."""

    def __init__(self, dem: DemSource, xs: np.ndarray, zs: np.ndarray, margin_m: float, cell_m: float):
        self.cell_m = cell_m
        self.x0 = math.floor((float(np.min(xs)) - margin_m) / cell_m) * cell_m
        self.z0 = math.floor((float(np.min(zs)) - margin_m) / cell_m) * cell_m
        cols = int(math.ceil((float(np.max(xs)) + margin_m - self.x0) / cell_m)) + 1
        rows = int(math.ceil((float(np.max(zs)) + margin_m - self.z0) / cell_m)) + 1
        self.window = np.nan_to_num(dem.window(self.x0, self.z0, max(cols, 2), max(rows, 2), cell_m), nan=0.0)

    def __call__(self, xs: np.ndarray, zs: np.ndarray) -> np.ndarray:
        return sample_bilinear(self.window, self.x0, self.z0, self.cell_m, xs, zs)


def densify(points: np.ndarray, max_step_m: float) -> np.ndarray:
    """The polyline with extra vertices so no segment is longer than ``max_step_m`` (original vertices stay)."""
    out = [points[0]]
    for a, b in zip(points[:-1], points[1:]):
        count = max(int(math.ceil(float(np.hypot(*(b - a))) / max_step_m)), 1)
        for k in range(1, count + 1):
            out.append(a + (b - a) * (k / count))
    return np.array(out)


def normals_of(points: np.ndarray) -> np.ndarray:
    """Unit vectors perpendicular to the line at each vertex (to the left of the direction of travel)."""
    if len(points) < 2:
        raise ValueError("a line needs at least 2 points")
    tangent = np.empty_like(points)
    tangent[1:-1] = points[2:] - points[:-2]
    tangent[0] = points[1] - points[0]
    tangent[-1] = points[-1] - points[-2]
    length = np.hypot(tangent[:, 0], tangent[:, 1])
    length[length == 0.0] = 1.0
    tangent = tangent / length[:, None]
    return np.stack([-tangent[:, 1], tangent[:, 0]], axis=1)


def _median_filter(values: np.ndarray, window: int) -> np.ndarray:
    if window <= 1 or len(values) < 3:
        return values.copy()
    half = window // 2
    padded = np.pad(values, half, mode="edge")
    return np.array([np.median(padded[i:i + window]) for i in range(len(values))])


def _moving_average(values: np.ndarray, window: int) -> np.ndarray:
    if window <= 1 or len(values) < 3:
        return values.copy()
    half = window // 2
    padded = np.pad(values, half, mode="edge")
    kernel = np.ones(window) / window
    return np.convolve(padded, kernel, mode="valid")[:len(values)]


def snap_offsets(points: np.ndarray, dem: DemSource, params: SnapParams) -> np.ndarray:
    """For every vertex, how far to move it sideways (along the left-hand normal, meters, positive = left) to sit
    on the channel the DEM shows.

    A rivers shows in a surface model as a flat, low band. For each candidate position across the line, within
    ``radius_m``, the mean ground height over a strip ``half_width_m`` either side is scored (lower is better) plus a
    small penalty for moving far; the best candidate wins. The offsets along the line are then median filtered and
    averaged, so the snapped line is smooth and does not hop between the river and a nearby canal."""
    normals = normals_of(points)
    candidates = np.arange(-params.radius_m, params.radius_m + params.step_m / 2, params.step_m)
    across = np.arange(-params.half_width_m, params.half_width_m + params.step_m / 2, params.step_m)
    offsets = np.zeros(len(points))
    for start in range(0, len(points), _CHUNK_VERTICES):
        index = slice(start, start + _CHUNK_VERTICES)
        chunk, chunk_normals = points[index], normals[index]
        reach = params.radius_m + params.half_width_m
        sampler = WindowSampler(dem, chunk[:, 0], chunk[:, 1], reach + 2 * params.step_m, params.step_m)
        shift = candidates[None, :, None] + across[None, None, :]                    # (1, K, M)
        xs = chunk[:, 0][:, None, None] + shift * chunk_normals[:, 0][:, None, None]  # (m, K, M)
        zs = chunk[:, 1][:, None, None] + shift * chunk_normals[:, 1][:, None, None]
        height = sampler(xs, zs).mean(axis=2)                                         # (m, K)
        score = height + params.penalty_m_per_km * np.abs(candidates)[None, :] / 1000.0
        offsets[index] = candidates[np.argmin(score, axis=1)]
    offsets = _median_filter(offsets, params.smooth_vertices)
    offsets = _moving_average(offsets, max(params.smooth_vertices // 2, 1))
    return np.clip(offsets, -params.radius_m, params.radius_m)


def snap_to_channel(points: np.ndarray, dem: DemSource, params: SnapParams) -> tuple[np.ndarray, np.ndarray]:
    """(snapped points, offsets): the line moved sideways onto the channel. The line should already be dense
    (``densify``) to about ``params.step_m * 2``."""
    offsets = snap_offsets(points, dem, params)
    return points + normals_of(points) * offsets[:, None], offsets


def section_minimum(points: np.ndarray, half_width_m: np.ndarray, dem: DemSource, step_m: float = 30.0) -> np.ndarray:
    """The lowest ground across the channel at each vertex: the minimum over the strip ``half_width_m`` (plus a cell)
    either side of the centerline, perpendicular to it."""
    normals = normals_of(points)
    reach = float(np.max(half_width_m)) + 2 * step_m
    out = np.empty(len(points))
    for start in range(0, len(points), _CHUNK_VERTICES):
        index = slice(start, start + _CHUNK_VERTICES)
        chunk, chunk_normals, half = points[index], normals[index], half_width_m[index]
        sampler = WindowSampler(dem, chunk[:, 0], chunk[:, 1], reach + step_m, step_m)
        across = np.arange(-reach, reach + step_m / 2, step_m)
        xs = chunk[:, 0][:, None] + across[None, :] * chunk_normals[:, 0][:, None]
        zs = chunk[:, 1][:, None] + across[None, :] * chunk_normals[:, 1][:, None]
        height = sampler(xs, zs)
        inside = np.abs(across)[None, :] <= (half[:, None] + step_m)
        height = np.where(inside, height, np.inf)
        out[index] = height.min(axis=1)
    return out


def non_increasing(values: np.ndarray) -> np.ndarray:
    """The non-increasing sequence closest to ``values`` in the least-squares sense (pool adjacent violators): a
    river never runs uphill, but the DEM is noisy, so the levels are fitted rather than clamped (a clamp would
    let one pit lower everything downstream of it)."""
    blocks: list[list[float]] = []  # [sum, count]
    for value in values:
        blocks.append([float(value), 1.0])
        while len(blocks) > 1 and blocks[-2][0] / blocks[-2][1] < blocks[-1][0] / blocks[-1][1]:
            total, count = blocks.pop()
            blocks[-1][0] += total
            blocks[-1][1] += count
    return np.concatenate([np.full(int(count), total / count) for total, count in blocks])


def floodplain(points: np.ndarray, levels: np.ndarray, dem: DemSource, *, distance_m: float, height_m: float,
               cell_m: float, simplify_m: float, min_area_m2: float) -> list[np.ndarray]:
    """The floodplain of one reach: the ground within ``distance_m`` of the channel that lies below the water level
    (at the nearest point of the channel) plus ``height_m``, kept where it connects to the channel. Returned as the
    outer rings (closed, ``(n, 2)`` world coordinates) of its polygons, simplified to ``simplify_m``."""
    pieces = []
    step = max(len(points) // max(int(math.ceil(len(points) / 40)), 1), 2)
    for start in range(0, len(points) - 1, step):
        part = points[start:start + step + 1]
        part_levels = levels[start:start + step + 1]
        if len(part) < 2:
            continue
        x0 = math.floor((float(part[:, 0].min()) - distance_m) / cell_m) * cell_m
        z0 = math.floor((float(part[:, 1].min()) - distance_m) / cell_m) * cell_m
        cols = int(math.ceil((float(part[:, 0].max()) + distance_m - x0) / cell_m))
        rows = int(math.ceil((float(part[:, 1].max()) + distance_m - z0) / cell_m))
        ground = np.nan_to_num(dem.window(x0, z0, cols, rows, cell_m), nan=0.0)
        cx = x0 + (np.arange(cols) + 0.5) * cell_m
        cz = z0 + (np.arange(rows) + 0.5) * cell_m
        gx, gz = np.meshgrid(cx, cz)
        best = np.full(ground.shape, np.inf)
        level_here = np.zeros(ground.shape)
        for a, b, la, lb in zip(part[:-1], part[1:], part_levels[:-1], part_levels[1:]):
            axis = b - a
            length_sq = float(axis @ axis)
            t = np.zeros(ground.shape) if length_sq == 0.0 else np.clip(
                ((gx - a[0]) * axis[0] + (gz - a[1]) * axis[1]) / length_sq, 0.0, 1.0)
            distance = np.hypot(gx - (a[0] + t * axis[0]), gz - (a[1] + t * axis[1]))
            closer = distance < best
            best = np.where(closer, distance, best)
            level_here = np.where(closer, la + (lb - la) * t, level_here)
        mask = (best <= distance_m) & (ground < level_here + height_m)
        if not mask.any():
            continue
        top = z0 + rows * cell_m
        transform = Affine(cell_m, 0.0, x0, 0.0, -cell_m, top)
        burned = np.flipud(mask).astype(np.uint8)
        channel = LineString(part).buffer(2.0 * cell_m)
        for geometry, value in shapes(burned, mask=burned.astype(bool), transform=transform):
            if value == 1:
                polygon = shape(geometry)
                if polygon.intersects(channel):
                    pieces.append(polygon)
    if not pieces:
        return []
    merged = unary_union(pieces)
    polygons = [merged] if isinstance(merged, Polygon) else list(getattr(merged, "geoms", []))
    rings = []
    for polygon in polygons:
        if polygon.area < min_area_m2:
            continue
        simplified = polygon.simplify(simplify_m, preserve_topology=True)
        if simplified.is_empty or simplified.geom_type != "Polygon":
            continue
        rings.append(np.array(shapely.get_coordinates(simplified.exterior)))
    return rings
