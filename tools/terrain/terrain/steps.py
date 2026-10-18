"""The build steps of ``python -m terrain.build_map`` and their registry.

A step is a function ``run(context)``. They run in this order (``STEP_ORDER``); ``--steps`` picks some of them.
``fetch``, ``mosaic``, ``reproject``, ``corrections``, ``region``, ``detail`` and ``layers`` do real work; ``rivers``
and ``package`` are stubs that say which Task fills them in (T1.2.3.1, T1.2.6.1), so the command line, the step
selection and the offline check are all in place and tested.

The data flows like this: ``mosaic`` joins the Copernicus DEM tiles into a VRT and loads the land polygons;
``reproject`` reads them onto the region grid (mean, min and max elevation per cell, and the land mask) and makes the
working state; ``corrections`` changes that state; ``region`` writes the rasters; ``detail`` reads the 32 m tiles
straight from the DEM (with the same corrections applied) and writes them. Everything is in the map's world frame
(``projection.py``): ``[row, column]`` arrays with row 0 at the south.
"""

from __future__ import annotations

import json
import math
import time
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import shapely
from rasterio.enums import Resampling
from shapely.geometry import Polygon

from . import detail as detail_tiles
from .cache import CacheError, DownloadCache, Source
from .corrections import (CorrectionError, Grid, MapState, RiverLine, apply_corrections, load_correction_files,
                          write_report)
from .csr import write_csr
from .dem import DemError, Reprojector, build_vrt, copdem_sources
from .landmask import LandMaskError, land_source, load_land_polygons, project_geometry, rasterize_land
from .layers import (LayerError, LayerParams, apply_fertility_overrides, derive_layers, layer_name,
                     load_deposits, load_fertility_overrides, rasterize_deposits, to_u8)
from .lakes import build_lakes, load_ne_lakes, reservoir_polygons
from .previews import shaded_image, write_png
from .riverdem import (densify, floodplain, non_increasing, normals_of, sample_bilinear, section_minimum,
                       snap_offsets)
from .rivermodel import (RiverError, RiverParams, assign_discharge, assign_orders, assign_width_depth, authored_info,
                         build_reaches, group_rivers, load_ne_rivers, ne_to_river_lines, river_documents,
                         river_sources, simplify_line, topological_order)
from .riverpreview import rivers_image
from .projection import ProjectionError, WorldFrame, make_to_world, world_frame

REGION_ELEVATION_SCALE = 0.25  # meters per stored unit of the region elevation rasters (i16)


class StepError(RuntimeError):
    """A step failed; the message says why."""


@dataclass
class Context:
    """What every step gets: the map's ``map.json``, where the package goes, the download cache."""

    map_file: Path
    map_def: dict
    out_dir: Path
    cache: DownloadCache
    offline: bool = False
    log: Callable[[str], None] = print
    # Files steps leave for each other (name -> path), e.g. the mosaic for the reprojection step.
    products: dict[str, Path] = field(default_factory=dict)
    # The working data (ground, land mask, rivers) once the steps that build it have run; None before.
    state: MapState | None = None
    # The region-resolution minimum and maximum elevation, which corrections change like the mean in ``state``.
    region_min: Grid | None = None
    region_max: Grid | None = None
    land_polygons: list | None = None  # Natural Earth land, cut to the bounds, lon/lat shapely geometries
    region_layers: dict[str, str] = field(default_factory=dict)  # layer name -> file, relative to the package
    detail_tiles: list[tuple[int, int]] = field(default_factory=list)

    @property
    def work_dir(self) -> Path:
        """Files the steps leave for each other; beside the package, not in it."""
        return self.out_dir.parent / f"{self.out_dir.name}.work"

    def listed_sources(self) -> list[Source]:
        entries = self.map_def.get("sources", [])
        if not isinstance(entries, list):
            raise StepError(f"{self.map_file}: 'sources' must be a list")
        try:
            return [Source.from_json(entry) for entry in entries]
        except CacheError as error:
            raise StepError(f"{self.map_file}: {error}") from None

    def pins(self) -> dict[str, dict]:
        """File name -> {"size", "sha256"} from the ``sources`` list of map.json, for the downloads the pipeline
        derives itself (the DEM tiles for the bounds, the land polygons)."""
        return {s.name: {"size": s.size, "sha256": s.sha256} for s in self.listed_sources()}

    def dem_sources(self) -> list[Source]:
        """The Copernicus DEM tiles that cover the map bounds."""
        return copdem_sources(self.frame().bounds, self.pins())

    def sources(self) -> list[Source]:
        """Every file the build needs: what the map's bounds require, plus what map.json lists."""
        listed = self.listed_sources()
        if not isinstance(self.map_def.get("bounds"), dict):
            return listed
        derived = [*self.dem_sources(), land_source(self.pins()), *river_sources(self.pins())]
        known = {(s.dataset, s.name) for s in derived}
        return derived + [s for s in listed if (s.dataset, s.name) not in known]

    def frame(self) -> WorldFrame:
        try:
            return world_frame(self.map_def)
        except ProjectionError as error:
            raise StepError(f"{self.map_file}: {error}") from None


@dataclass(frozen=True)
class Step:
    name: str
    run: Callable[[Context], None]
    description: str
    needs_downloads: bool = False  # true if it may download, so --offline checks the cache first
    # The files it may download (for --offline's check); None means every file the map needs.
    sources: Callable[[Context], list[Source]] | None = None

    def required_sources(self, context: Context) -> list[Source]:
        return context.sources() if self.sources is None else self.sources(context)


def load_map_def(map_file: Path) -> dict:
    """Reads ``map.json``: ``map_id`` and ``package`` are required (more keys arrive with the steps)."""
    try:
        map_def = json.loads(Path(map_file).read_text(encoding="utf-8"))
    except OSError as error:
        raise StepError(f"{map_file}: cannot read the map definition: {error.strerror or error}") from None
    except ValueError as error:
        raise StepError(f"{map_file}: not valid JSON: {error}") from None
    if not isinstance(map_def, dict):
        raise StepError(f"{map_file}: the map definition must be a JSON object")
    for key in ("map_id", "package"):
        if not isinstance(map_def.get(key), str) or not map_def[key]:
            raise StepError(f"{map_file}: missing required text field '{key}'")
    return map_def


def _fetch(context: Context) -> None:
    sources = context.sources()
    if not sources:
        context.log("fetch: the map lists no sources")
        return
    for source in sources:
        cached = context.cache.is_valid(source)
        path = context.cache.fetch_optional(source) if source.optional else context.cache.fetch(source)
        if path is None:
            context.log(f"fetch: {source.dataset}/{source.name} does not exist at the source: sea, no data")
        else:
            size = path.stat().st_size
            context.log(f"fetch: {source.dataset}/{source.name} {'(cached)' if cached else '(downloaded)'} "
                        f"{size / 1e6:.1f} MB -> {path}")


def _corrections(context: Context) -> None:
    """Reads the overlay files listed in map.json ("corrections": [paths relative to map.json]) and checks them.
    If the working data exists, applies them in order and writes a report beside the package; if not (the steps
    that build it are T1.2.2.1), it only validates the files."""
    listed = context.map_def.get("corrections", [])
    if not isinstance(listed, list) or not all(isinstance(p, str) for p in listed):
        raise StepError(f"{context.map_file}: 'corrections' must be a list of file paths")
    if not listed:
        context.log("corrections: map.json lists none; nothing to do")
        return
    try:
        to_world = make_to_world(context.map_def)
        corrections = load_correction_files([context.map_file.parent / p for p in listed], to_world)
        if context.state is None:
            context.log(f"corrections: {len(corrections)} correction(s) in {len(listed)} file(s) read and checked; "
                        "the ground is not built yet (run the 'reproject' step first), so none was applied")
            return
        # The region's minimum and maximum elevation get the same changes as the mean, each with its own copy of
        # the land mask (a correction may clear or set land cells).
        land_before = context.state.land.data.copy()
        ground_ops = [c for c in corrections if c.op not in RIVER_OPS]  # the rivers step does remove_river, river_path
        effects = apply_corrections(context.state, ground_ops, to_world)
        for extreme in (context.region_min, context.region_max):
            if extreme is not None:
                apply_corrections(MapState(dem=extreme, land=Grid(land_before.copy(), extreme.origin_x,
                                                                  extreme.origin_z, extreme.cell_m)),
                                  ground_ops, to_world)
    except (CorrectionError, ProjectionError) as error:
        raise StepError(str(error)) from None
    report = context.out_dir.parent / f"{context.out_dir.name}.reports" / "corrections.md"
    write_report(report, effects, f"Historical corrections: {context.map_def['map_id']}")
    context.log(f"corrections: applied {len(effects)} ground correction(s) (river corrections are applied by the "
                f"rivers step); report: {report}")


def _layers(context: Context) -> None:
    """Derives fertility, vegetation, wetness and the deposit layers (rules in layers.py). map.json keys:
    "layers" (parameter overrides), "deposits" and "fertility_overrides" (GeoJSON paths relative to map.json).
    Without working data it only validates the files; otherwise it writes u8 rasters (scale 1/255) to
    ``<out>/region/``."""
    map_def = context.map_def
    for key in ("deposits", "fertility_overrides"):
        if key in map_def and not isinstance(map_def[key], str):
            raise StepError(f"{context.map_file}: '{key}' must be a file path")
    try:
        params = LayerParams.from_json(map_def.get("layers"))
        to_world = make_to_world(map_def) if ("deposits" in map_def or "fertility_overrides" in map_def) else None
        deposits = (load_deposits(context.map_file.parent / map_def["deposits"], to_world)
                    if "deposits" in map_def else [])
        overrides = (load_fertility_overrides(context.map_file.parent / map_def["fertility_overrides"], to_world)
                     if "fertility_overrides" in map_def else [])
        state = context.state
        if state is None:
            context.log(f"layers: {len(deposits)} deposit feature(s) and {len(overrides)} fertility override(s) "
                        "read and checked; the ground is not built yet (T1.2.2.1), so no layer was written")
            return
        layers = derive_layers(state.dem, state.land, state.rivers, params, floodplain=state.floodplain,
                               landcover=state.landcover)
        apply_fertility_overrides(layers.fertility, overrides)
        layers.deposits = rasterize_deposits(state.dem, state.land, deposits, params)
    except (LayerError, CorrectionError, ProjectionError) as error:
        raise StepError(str(error)) from None
    region = context.out_dir / "region"
    named = {"fertility": layers.fertility, "vegetation": layers.vegetation, "wetness": layers.wetness,
             **layers.deposits}
    for name, grid in named.items():
        write_csr(region / f"{name}.csr", to_u8(grid.data), scale=1.0 / 255.0)
    context.log(f"layers: wrote {len(named)} layer(s) to {region}")


def _require_bounds(context: Context) -> WorldFrame:
    if not isinstance(context.map_def.get("bounds"), dict):
        raise StepError(f"{context.map_file}: this step needs 'bounds' (min_lon, min_lat, max_lon, max_lat)")
    return context.frame()


def _region_grid(context: Context, frame: WorldFrame) -> tuple[float, int, int]:
    """(cell size, columns, rows) of the region grid, which covers the whole world box."""
    cell = context.map_def.get("region_cell_size_m", 1000.0)
    if not isinstance(cell, (int, float)) or isinstance(cell, bool) or cell <= 0:
        raise StepError(f"{context.map_file}: 'region_cell_size_m' must be a number above 0")
    return float(cell), math.ceil(frame.width_m / cell), math.ceil(frame.height_m / cell)


def _land_polygons(context: Context, frame: WorldFrame) -> list:
    if context.land_polygons is None:
        path = context.cache.fetch(land_source(context.pins()))
        try:
            context.land_polygons = load_land_polygons(path, frame.bounds)
        except LandMaskError as error:
            raise StepError(str(error)) from None
    return context.land_polygons


def _mosaic(context: Context) -> None:
    """Joins the Copernicus DEM tiles for the map's bounds into ``<out>.work/dem.vrt`` and loads the Natural Earth
    land polygons. Tiles that do not exist (open sea) are skipped; the ground there reads as sea."""
    frame = _require_bounds(context)
    sources = context.dem_sources()
    paths: list[Path] = []
    missing = 0
    for source in sources:
        path = context.cache.fetch_optional(source)
        if path is None:
            missing += 1
        else:
            paths.append(path)
    if not paths:
        raise StepError(f"{context.map_file}: none of the {len(sources)} Copernicus DEM tiles for these bounds "
                        "exists, so the whole map would be sea; check the bounds")
    vrt = context.work_dir / "dem.vrt"
    try:
        build_vrt(paths, vrt)
    except DemError as error:
        raise StepError(str(error)) from None
    context.products["dem_vrt"] = vrt
    polygons = _land_polygons(context, frame)
    context.log(f"mosaic: {len(paths)} DEM tile(s) ({missing} absent: sea) -> {vrt}; "
                f"{len(polygons)} land polygon(s) in the bounds")


def _dem_vrt(context: Context) -> Path:
    vrt = context.products.get("dem_vrt") or context.work_dir / "dem.vrt"
    if not vrt.is_file():
        raise StepError(f"{vrt} does not exist: run the 'mosaic' step first")
    return vrt


def _sea_level_ground(elevation: np.ndarray, land: np.ndarray) -> np.ndarray:
    """No data reads as sea level, and the sea is never above 0 m (the DEM has coastal artifacts and water bodies)."""
    ground = np.nan_to_num(elevation.astype(np.float64), nan=0.0)
    return np.where(land, ground, np.minimum(ground, 0.0))


def _reproject(context: Context) -> None:
    """Builds the working state at the region resolution: the mean elevation of every region cell (and its minimum
    and maximum), reprojected into the map's frame, and the land mask from the Natural Earth polygons."""
    frame = _require_bounds(context)
    vrt = _dem_vrt(context)
    cell, cols, rows = _region_grid(context, frame)
    polygons = _land_polygons(context, frame)
    started = time.perf_counter()
    try:
        with Reprojector(vrt, frame) as reprojector:
            mean = reprojector.window(0.0, 0.0, cols, rows, cell, Resampling.average)
            low = reprojector.window(0.0, 0.0, cols, rows, cell, Resampling.min)
            high = reprojector.window(0.0, 0.0, cols, rows, cell, Resampling.max)
    except Exception as error:  # rasterio raises its own error types for unreadable or unprojectable data
        raise StepError(f"reproject: cannot read the DEM ({error.__class__.__name__}: {error})") from None
    land = rasterize_land(polygons, frame, 0.0, 0.0, cols, rows, cell)

    def grid(data: np.ndarray) -> Grid:
        return Grid(data, 0.0, 0.0, cell)

    context.state = MapState(dem=grid(_sea_level_ground(mean, land)), land=grid(land))
    context.region_min = grid(_sea_level_ground(low, land))
    context.region_max = grid(_sea_level_ground(high, land))
    context.state.check()
    nodata = int(np.isnan(mean).sum())
    context.log(f"reproject: {cols} x {rows} region cells of {cell:g} m; {int(land.sum())} land cells "
                f"({100.0 * land.mean():.0f}%), {nodata} without DEM data; elevation "
                f"{context.state.dem.data.min():.1f} to {context.state.dem.data.max():.1f} m; "
                f"{time.perf_counter() - started:.1f} s")


def _load_corrections(context: Context, to_world):
    listed = context.map_def.get("corrections", [])
    if not isinstance(listed, list) or not all(isinstance(p, str) for p in listed):
        raise StepError(f"{context.map_file}: 'corrections' must be a list of file paths")
    corrections = load_correction_files([context.map_file.parent / p for p in listed], to_world) if listed else []
    return listed, corrections


def _region(context: Context) -> None:
    """Writes the region rasters (i16, 0.25 m units): ``elevation`` (mean), ``elevation_min``, ``elevation_max``,
    and the u8 ``land_mask``, to ``<out>/region/``, and a hillshade preview to ``<out>/previews/region.png``."""
    state = context.state
    if state is None or context.region_min is None or context.region_max is None:
        raise StepError("region: no ground to write; run the 'reproject' step first")
    mean = state.dem.data
    low = np.minimum(context.region_min.data, mean)   # corrections can move the mean past the extremes
    high = np.maximum(context.region_max.data, mean)
    region = context.out_dir / "region"
    named = {"elevation": mean, "elevation_min": low, "elevation_max": high}
    limit = 32767 * REGION_ELEVATION_SCALE
    for name, values in named.items():
        if values.min() < -limit or values.max() > limit:
            raise StepError(f"region: {name} ranges {values.min():.0f} to {values.max():.0f} m, beyond what an i16 "
                            f"with {REGION_ELEVATION_SCALE} m steps holds (+/-{limit:.0f} m)")
        write_csr(region / f"{name}.csr", np.rint(values / REGION_ELEVATION_SCALE).astype(np.int16),
                  scale=REGION_ELEVATION_SCALE)
        context.region_layers[name] = f"region/{name}.csr"
    write_csr(region / "land_mask.csr", state.land.data.astype(np.uint8))
    context.region_layers["land_mask"] = "region/land_mask.csr"
    preview = context.out_dir / "previews" / "region.png"
    write_png(preview, shaded_image(mean, state.land.data, state.dem.cell_m))
    rows, cols = mean.shape
    context.log(f"region: wrote {len(context.region_layers)} raster(s) of {cols} x {rows} cells to {region}; "
                f"preview: {preview}")


def _detail_polygons(context: Context, frame: WorldFrame) -> list[Polygon]:
    """The map's ``detail_regions`` (rings of [lon, lat]) as polygons in the world frame."""
    rings = context.map_def.get("detail_regions", [])
    if not isinstance(rings, list):
        raise StepError(f"{context.map_file}: 'detail_regions' must be a list of polygons")
    to_projected = frame.lonlat_to_projected()
    polygons = []
    for index, ring in enumerate(rings):
        try:
            polygon = Polygon([(float(lon), float(lat)) for lon, lat in ring])
        except (TypeError, ValueError):
            raise StepError(f"{context.map_file}: detail_regions[{index}] must be a ring of [lon, lat] points") from None
        if len(ring) < 4 or not polygon.is_valid or polygon.is_empty:
            raise StepError(f"{context.map_file}: detail_regions[{index}] is not a valid polygon "
                            "(at least 3 corners, closed, not self-crossing)")
        projected = project_geometry(shapely.segmentize(polygon, 0.02), to_projected)
        polygons.append(shapely.transform(projected, lambda c: c - np.array([frame.origin_x, frame.origin_y])))
    return polygons


def _detail(context: Context) -> None:
    """Writes the 32 m detail tiles (65 x 65 i16 samples, 0.1 m steps, a whole-meter offset per tile) for every
    2,048 m tile that touches a ``detail_regions`` polygon, to ``<out>/detail/<tx>_<ty>.csr``. Each tile is read
    from the DEM with a margin, the same corrections as the region are applied to it, and shared edges are made
    equal. Also writes ``previews/detail_<tx>_<ty>.png`` (the tile nearest the middle) and ``previews/detail_all.png``."""
    frame = _require_bounds(context)
    polygons = _detail_polygons(context, frame)
    if not polygons:
        context.log("detail: map.json has no detail_regions; no tiles written")
        return
    tiles = detail_tiles.tiles_for_polygons(polygons, (frame.width_m, frame.height_m))
    if not tiles:
        raise StepError(f"{context.map_file}: the detail_regions touch no tile inside the map")
    vrt = _dem_vrt(context)
    land_polygons = _land_polygons(context, frame)
    to_world = frame.to_world()
    try:
        _listed, corrections = _load_corrections(context, to_world)
        corrections = [c for c in corrections if c.op not in RIVER_OPS]
    except (CorrectionError, ProjectionError) as error:
        raise StepError(str(error)) from None
    started = time.perf_counter()
    heights: dict[tuple[int, int], np.ndarray] = {}
    lands: dict[tuple[int, int], np.ndarray] = {}
    try:
        with Reprojector(vrt, frame) as reprojector:
            for tx, ty in tiles:
                x0, z0, n = detail_tiles.tile_window(tx, ty)
                land = rasterize_land(land_polygons, frame, x0, z0, n, n, detail_tiles.SPACING_M)
                dem = _sea_level_ground(
                    reprojector.window(x0, z0, n, n, detail_tiles.SPACING_M, Resampling.bilinear), land)
                if corrections:
                    state = MapState(dem=Grid(dem, x0, z0, detail_tiles.SPACING_M),
                                     land=Grid(land.copy(), x0, z0, detail_tiles.SPACING_M))
                    apply_corrections(state, corrections, to_world)
                    dem, land = state.dem.data, state.land.data
                heights[(tx, ty)] = detail_tiles.center(dem).astype(np.float32).copy()
                lands[(tx, ty)] = detail_tiles.center(land).copy()
    except CorrectionError as error:
        raise StepError(str(error)) from None
    except StepError:
        raise
    except Exception as error:
        raise StepError(f"detail: cannot read the DEM ({error.__class__.__name__}: {error})") from None
    detail_tiles.reconcile_edges(heights)
    folder = context.out_dir / "detail"
    for (tx, ty), values in heights.items():
        try:
            raw, offset = detail_tiles.quantize_tile(values)
        except detail_tiles.DetailError as error:
            raise StepError(f"detail tile ({tx}, {ty}): {error}") from None
        write_csr(folder / f"{tx}_{ty}.csr", raw, scale=detail_tiles.RESOLUTION_M, offset=offset)
    context.detail_tiles = tiles
    # Previews: one tile (nearest the middle of the set) and all tiles side by side.
    mid_x = sum(t[0] for t in tiles) / len(tiles)
    mid_y = sum(t[1] for t in tiles) / len(tiles)
    middle = min(tiles, key=lambda t: (t[0] - mid_x) ** 2 + (t[1] - mid_y) ** 2)
    previews = context.out_dir / "previews"
    one = previews / f"detail_{middle[0]}_{middle[1]}.png"
    write_png(one, shaded_image(heights[middle], lands[middle], detail_tiles.SPACING_M))
    xs = [t[0] for t in tiles]
    ys = [t[1] for t in tiles]
    steps = detail_tiles.SAMPLES - 1
    mosaic = np.zeros(((max(ys) - min(ys) + 1) * steps, (max(xs) - min(xs) + 1) * steps), dtype=np.float64)
    mosaic_land = np.zeros(mosaic.shape, dtype=bool)
    for (tx, ty), values in heights.items():
        r, c = (ty - min(ys)) * steps, (tx - min(xs)) * steps
        mosaic[r:r + steps, c:c + steps] = values[:steps, :steps]
        mosaic_land[r:r + steps, c:c + steps] = lands[(tx, ty)][:steps, :steps]
    everything = previews / "detail_all.png"
    write_png(everything, shaded_image(mosaic, mosaic_land, detail_tiles.SPACING_M, min_width=300, max_width=1600))
    size = sum(f.stat().st_size for f in folder.glob("*.csr"))
    context.log(f"detail: wrote {len(tiles)} tile(s) ({size / 1e6:.2f} MB) to {folder}; "
                f"{time.perf_counter() - started:.1f} s; previews: {one}, {everything}")


RIVER_OPS = ("remove_river", "river_path")
# A line whose downstream end is higher than its upstream end by more than this is reversed (Natural Earth lines run
# downstream; this only guards against a line drawn the other way). Short lines are never judged.
_REVERSED_BY_M = 8.0
_REVERSAL_MIN_LENGTH_M = 40_000.0
_BLEND_M = 1000.0  # how far along a reach a junction's shift is spread


def _rasterize_polygons(polygons: list, grid: Grid) -> np.ndarray:
    """A bool array shaped like ``grid`` that is True where a polygon (world meters) covers the cell center."""
    from rasterio.features import rasterize
    from rasterio.transform import Affine

    rows, cols = grid.shape
    if not polygons:
        return np.zeros((rows, cols), dtype=bool)
    transform = Affine(grid.cell_m, 0.0, grid.origin_x, 0.0, -grid.cell_m, grid.origin_z + rows * grid.cell_m)
    burned = rasterize([(p, 1) for p in polygons], out_shape=(rows, cols), transform=transform, fill=0,
                       dtype="uint8", all_touched=False)
    return np.flipud(burned).astype(bool)


def _orient_downstream(line, grid: Grid):
    """The line, reversed if the region DEM says it runs uphill over a long distance."""
    length = float(np.hypot(*np.diff(line.points, axis=0).T).sum())
    if length < _REVERSAL_MIN_LENGTH_M:
        return line, False
    segment = np.hypot(*np.diff(line.points, axis=0).T)
    along = np.concatenate([[0.0], np.cumsum(segment)])
    probes = np.linspace(0.0, length, max(int(length / 1000.0), 8))
    xs = np.interp(probes, along, line.points[:, 0])
    zs = np.interp(probes, along, line.points[:, 1])
    ground = sample_bilinear(grid.data, grid.origin_x, grid.origin_z, grid.cell_m, xs, zs)
    edge = max(len(ground) // 7, 2)
    if float(ground[:edge].mean()) < float(ground[-edge:].mean()) - _REVERSED_BY_M:
        line.points = line.points[::-1].copy()
        line.width_m = line.width_m[::-1].copy()
        line.depth_m = line.depth_m[::-1].copy()
        return line, True
    return line, False


def _snap_reaches(reaches: list, dem, params: RiverParams) -> list[float]:
    """Moves every reach onto the channel the DEM shows. The move fades to nothing over ``_BLEND_M`` toward an end where
    other reaches meet, so reaches that join keep exactly the shared point they had. Returns the sideways moves
    (meters, absolute) for the log."""
    snap = params.snap
    joined: dict[int, int] = {}
    for r in reaches:
        joined[r.start_node] = joined.get(r.start_node, 0) + 1
        joined[r.end_node] = joined.get(r.end_node, 0) + 1
    moves: list[float] = []
    for r in reaches:
        dense = densify(r.points, 2.0 * snap.step_m)
        if not snap.enabled:
            r.points = dense
            continue
        offsets = snap_offsets(dense, dem, snap)
        along = np.concatenate([[0.0], np.cumsum(np.hypot(*np.diff(dense, axis=0).T))])
        total = float(along[-1])
        blend = max(min(_BLEND_M, 0.4 * total), 1.0)
        if joined[r.start_node] > 1:
            offsets = offsets * np.clip(along / blend, 0.0, 1.0)
        if joined[r.end_node] > 1:
            offsets = offsets * np.clip((total - along) / blend, 0.0, 1.0)
        moves.extend(np.abs(offsets).tolist())
        r.points = dense + normals_of(dense) * offsets[:, None]
    return moves


def _rivers(context: Context) -> None:
    """Extracts the rivers and lakes (docs/architecture.md section 15.6) from Natural Earth: selects the rivers by
    discharge, applies the river corrections, joins the lines into reaches (splitting where rivers meet or branch),
    pulls each reach onto the channel the DEM shows, simplifies it, and gives every vertex a width, a depth and a
    water level that never rises downstream. Also finds each reach's floodplain, adds the lakes, and writes
    ``rivers.json``, ``lakes.json`` and ``previews/rivers.png``. map.json's ``rivers`` object sets every threshold
    (see ``rivermodel.RiverParams``)."""
    frame = _require_bounds(context)
    state = context.state
    if state is None:
        raise StepError("rivers: no ground to work on; run the 'reproject' step first")
    vrt = _dem_vrt(context)
    to_world = frame.to_world()
    started = time.perf_counter()
    try:
        params = RiverParams.from_json(context.map_def.get("rivers"))
        rivers_zip = context.cache.fetch(river_sources(context.pins())[0])
        lakes_zip = context.cache.fetch(river_sources(context.pins())[1])
        natural_earth = load_ne_rivers(rivers_zip, frame.bounds)
        lines, info = ne_to_river_lines(natural_earth, to_world, (frame.width_m, frame.height_m), params)
        _listed, corrections = _load_corrections(context, to_world)
    except (RiverError, CorrectionError, ProjectionError) as error:
        raise StepError(str(error)) from None
    found = len(lines)

    # The river corrections (remove_river, river_path) work on the list of lines; the others were applied earlier.
    river_ops = [c for c in corrections if c.op in RIVER_OPS]
    effects = []
    if river_ops:
        state.rivers = lines
        try:
            effects = apply_corrections(state, river_ops, to_world)
        except CorrectionError as error:
            raise StepError(str(error)) from None
        lines = list(state.rivers)
        report = context.out_dir.parent / f"{context.out_dir.name}.reports" / "corrections_rivers.md"
        write_report(report, effects, f"River corrections: {context.map_def['map_id']}")
        context.log(f"rivers: applied {len(effects)} river correction(s); report: {report}")
    for line in lines:
        if line.id not in info:
            info[line.id] = authored_info(line, params)
    kept = [line for line in lines if info[line.id].explicit_discharge
            or info[line.id].own_discharge >= params.min_discharge_m3s]
    flipped = 0
    for line in kept:
        if info[line.id].rank is not None:
            _line, was_reversed = _orient_downstream(line, state.dem)
            flipped += int(was_reversed)
    context.log(f"rivers: {found} Natural Earth line(s) in the map, {len(lines)} after corrections, {len(kept)} at "
                f"or above {params.min_discharge_m3s:g} m3/s" + (f"; {flipped} reversed to run downhill" if flipped else ""))

    reaches = build_reaches(kept, params.junction_m)
    for reach in reaches:
        line_info = info[reach.line_id.rsplit(".", 1)[0]]
        reach.own_discharge = line_info.own_discharge
        reach.explicit_discharge = line_info.explicit_discharge
        reach.fixed_width_m, reach.fixed_depth_m = line_info.fixed_width_m, line_info.fixed_depth_m
    assign_discharge(reaches)
    assign_orders(reaches)
    assign_width_depth(reaches, params)

    detail_polygons = _detail_polygons(context, frame)
    detail_area = shapely.unary_union(detail_polygons) if detail_polygons else None
    land_polygons = _land_polygons(context, frame)
    try:
        with Reprojector(vrt, frame) as dem:
            moves = _snap_reaches(reaches, dem, params)
            for reach in reaches:
                reach.points = simplify_line(reach.points, detail_area, params.tolerance_detail_m,
                                             params.tolerance_elsewhere_m)
            # Water levels: the lowest ground across the channel, fitted so it never rises downstream.
            order = topological_order(reaches)
            end_level: dict[int, float] = {}
            for key in order:
                reach = reaches[key]
                lowest = section_minimum(reach.points, np.full(len(reach.points), reach.width_m / 2.0), dem)
                levels = non_increasing(lowest + params.level_depth_fraction * reach.depth_m)
                above = [end_level[u] for u in reach.upstream if u in end_level]
                if above:
                    levels = np.minimum(levels, min(above))
                reach.levels = levels
                end_level[key] = float(levels[-1])
            plain = params.floodplain
            for reach in reaches:
                reach.floodplain = floodplain(reach.points, reach.levels, dem, distance_m=plain.distance_m,
                                              height_m=plain.height_m, cell_m=plain.cell_m,
                                              simplify_m=plain.simplify_m, min_area_m2=plain.min_area_km2 * 1e6)
            lake_features = load_ne_lakes(lakes_zip, frame.bounds)
            lakes, lake_notes = build_lakes(lake_features, frame, dem, params, reservoir_polygons(corrections))
            cell = max(60.0, max(frame.width_m, frame.height_m) / 1500.0)
            cols, rows = int(math.ceil(frame.width_m / cell)), int(math.ceil(frame.height_m / cell))
            ground = np.nan_to_num(dem.window(0.0, 0.0, cols, rows, cell), nan=0.0)
    except (RiverError, DemError) as error:
        raise StepError(str(error)) from None
    except StepError:
        raise
    except Exception as error:
        raise StepError(f"rivers: cannot read the DEM ({error.__class__.__name__}: {error})") from None

    documents = river_documents(reaches, group_rivers(reaches))
    out = context.out_dir
    (out / "rivers.json").write_text(json.dumps({"format": 1, "rivers": documents}, separators=(",", ":")) + "\n",
                                     encoding="utf-8")
    (out / "lakes.json").write_text(json.dumps({"format": 1, "lakes": lakes}, separators=(",", ":")) + "\n",
                                    encoding="utf-8")
    preview = out / "previews" / "rivers.png"
    land = rasterize_land(land_polygons, frame, 0.0, 0.0, cols, rows, cell)
    write_png(preview, rivers_image(ground, land, cell, documents, lakes))

    # What the layers step uses: the rivers as lines, and the floodplains on the region grid.
    state.rivers = [RiverLine(r.line_id, r.name, r.points, np.full(len(r.points), r.width_m),
                              np.full(len(r.points), r.depth_m)) for r in reaches]
    rings = [Polygon(ring) for r in reaches for ring in r.floodplain if len(ring) >= 4]
    state.floodplain = Grid(_rasterize_polygons(rings, state.dem), state.dem.origin_x, state.dem.origin_z,
                            state.dem.cell_m)
    count = sum(len(r["reaches"]) for r in documents)
    snapped = f"; snapped {np.median(moves):.0f} m (median), {np.percentile(moves, 90):.0f} m (90th percentile) onto the DEM channel" if moves else ""
    context.log(f"rivers: {len(documents)} river(s) in {count} reach(es){snapped}; "
                f"{sum(len(r.floodplain) for r in reaches)} floodplain polygon(s); {len(lakes)} lake(s); "
                f"{time.perf_counter() - started:.1f} s")
    for river in documents:
        reach_text = ", ".join(f"{r['id']} (order {r['order']}, {r['discharge_m3s']:g} m3/s, "
                               f"{r['vertices'][0]['width_m']:g} m wide)" for r in river["reaches"])
        context.log(f"rivers:   {river['name']}: {reach_text}")
    for note in lake_notes:
        context.log(f"rivers:   lake {note}")
    context.log(f"rivers: wrote {out / 'rivers.json'}, {out / 'lakes.json'}; preview: {preview}")


def _elevation_sources(context: Context) -> list[Source]:
    """The DEM tiles and the land polygons (``mosaic``, ``reproject``, ``detail``)."""
    return [*context.dem_sources(), land_source(context.pins())]


def _river_download_sources(context: Context) -> list[Source]:
    """What ``rivers`` reads: the river lines and lake polygons, and the land polygons for its preview."""
    return [*river_sources(context.pins()), land_source(context.pins())]


def _stub(name: str, task: str, what: str) -> Callable[[Context], None]:
    def run(context: Context) -> None:
        context.log(f"{name}: not implemented yet ({task}: {what}); nothing done")

    return run


STEPS: tuple[Step, ...] = (
    Step("fetch", _fetch, "download the source datasets into the cache", needs_downloads=True),
    Step("mosaic", _mosaic, "join the DEM tiles into a mosaic and load the land polygons", needs_downloads=True,
         sources=_elevation_sources),
    Step("reproject", _reproject,
         "reproject to the map's equal-area projection: region elevation and land mask"),
    Step("corrections", _corrections, "apply the historical correction overlays (architecture §15.7)"),
    Step("region", _region, "write the region rasters (elevation, min, max, land mask)"),
    Step("detail", _detail, "write the 32 m detail tiles", needs_downloads=True, sources=_elevation_sources),
    Step("rivers", _rivers, "extract rivers and lakes (Natural Earth), with floodplains; write rivers.json, lakes.json",
         needs_downloads=True, sources=_river_download_sources),
    Step("layers", _layers,
         "derive fertility, vegetation, wetness and deposit layers"),
    Step("package", _stub("package", "T1.2.6.1", "write package.json with checksums"),
         "write package.json with the checksums and the package hash"),
)
STEP_ORDER: tuple[str, ...] = tuple(step.name for step in STEPS)
_BY_NAME = {step.name: step for step in STEPS}


def select_steps(names: list[str] | None) -> list[Step]:
    """The steps to run, in pipeline order. ``None`` means all. Raises StepError for an unknown name."""
    if names is None:
        return list(STEPS)
    unknown = [name for name in names if name not in _BY_NAME]
    if unknown:
        raise StepError(f"unknown step(s): {', '.join(unknown)}. The steps are: {', '.join(STEP_ORDER)}")
    wanted = set(names)
    return [step for step in STEPS if step.name in wanted]
