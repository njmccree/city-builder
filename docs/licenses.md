# Data licenses

The terrain pipeline (`tools/terrain`, docs/architecture.md §15.5) builds map packages from open datasets, and the
**derived data ships in the game** (heights, rivers, lakes, land cover layers). This file records, per dataset,
what its license says, whether the game may use it commercially, what credit it needs, and whether the derived
data may be redistributed.

## Read this first: what is cleared and what is not

**Checked on 2026-10-18, from a cloud session whose network could not reach the data publishers' own websites**
(Copernicus, HydroSHEDS, Natural Earth and ESA WorldCover all failed to resolve). Everything below comes from
web-search summaries of the official pages and from catalog mirrors (Google Earth Engine, AWS Open Data, Planet,
the Sentinel Hub license PDF's search entry). **No license text was read in full.** Before any dataset's derived
data is shipped, someone must read the primary document linked below and tick the "verified" box at the end.

| Dataset | Looks usable in a commercial game? | What is unconfirmed |
|---|---|---|
| Natural Earth | **Yes**: public domain, no credit needed | nothing important (see the Washington Post note) |
| ESA WorldCover 2021 | **Yes**: CC BY 4.0, credit required | nothing important |
| Copernicus DEM GLO-30 | **Probably yes**: free license, credit and a disclaimer required when distributed | the license text's wording on **commercial use** was not seen |
| HydroSHEDS (HydroRIVERS, HydroLAKES) | **Probably yes, but not cleared**: catalogs say free for commercial use under a WWF license agreement | **redistribution of derived data and the exact attribution** were not seen; one catalog says CC BY 4.0, which conflicts with the agreement the others describe |

**None is known to be unsuitable for a commercial game.** The two that need a human check before shipping are
**HydroSHEDS** (the game ships rivers and lakes derived from it) and the **Copernicus DEM** commercial wording.
If either turns out not to allow shipping derived data, do not switch silently: the fallbacks are below, and the
choice is the designer's.

### Fallbacks, if a dataset is refused

- **Rivers and lakes:** Natural Earth's rivers and lakes (public domain; coarse, 1:10m, so fewer small channels and
  hand corrections in `tools/terrain/maps/<map>/` would carry more of the work), or OpenStreetMap waterways (ODbL:
  credit "© OpenStreetMap contributors" and **share-alike on a derived database**, which needs its own legal reading
  before a shipped package is built from it).
- **Elevation:** NASA SRTM (about 30 m, in the public domain as far as I know; not verified in this check) in
  place of Copernicus DEM; it has voids and older data, and the pipeline's mosaic step would need a void fill.
- **Land cover / vegetation:** none needed for a start; the fertility and vegetation layers are authored and derived
  (T1.2.4.1, T1.2.4.2). WorldCover's license is already fine.

## Copernicus DEM GLO-30 (elevation, 30 m)

- **License:** "Licence for the use of the Copernicus WorldDEM-30": free and open worldwide.
  Link: <https://documentation.dataspace.copernicus.eu/APIs/SentinelHub/Data/DEM/resources/license/License-COPDEM-30.pdf>
- **Commercial use allowed?** Not confirmed in the material read. The license is described as free and open;
  nothing seen restricts commercial use, but the text itself must be read. **To verify.**
- **Attribution (as quoted by the sources):** when the data are distributed or communicated to the public:
  "© DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European
  Union and ESA; all rights reserved". When the data have been adapted or modified, the notice reads
  "produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018
  provided under COPERNICUS by the European Union and ESA; all rights reserved".
- **Disclaimer:** distribution to the public must add a statement that the organisations in charge of the Copernicus
  programme do not accept liability for any use of the Copernicus WorldDEM-30.
- **Derived data redistributable?** The "produced using" notice implies adapted data may be distributed with that
  notice and the disclaimer. **To verify** in the license text.
- **Date checked:** 2026-10-18 (secondary sources only).

## HydroSHEDS: HydroRIVERS and HydroLAKES (rivers and lakes)

- **License:** the HydroSHEDS License Agreement (WWF / HydroSHEDS team). Terms page:
  <https://www.hydrosheds.org/page/license>. HydroLAKES has its own terms page on the same site.
- **Commercial use allowed?** Catalogs say HydroSHEDS core products are free for scientific, educational and
  commercial use, under the license agreement. **To verify** on the terms page, for HydroRIVERS and HydroLAKES
  specifically (nothing specific to them was found).
- **Attribution:** a WWF notice must accompany derivative products, along the lines of "this product incorporates
  HydroSHEDS data used under license", with a disclaimer that WWF has not evaluated the altered data. The exact
  text must be copied from the license page. **To verify.**
- **Derived data redistributable?** Unknown. The agreement has redistribution restrictions (not seen). This is the
  point that decides whether `rivers.json` and `lakes.json` built from it can ship. **To verify first.**
- **Conflict:** one catalog record lists the license as CC BY 4.0, while the others describe the WWF license
  agreement. The agreement on the official page controls.
- **Date checked:** 2026-10-18 (secondary sources only).

## Natural Earth (coastlines, country and water vector data)

- **License:** public domain. Terms: <https://www.naturalearthdata.com/about/terms-of-use/>
- **Commercial use allowed?** Yes: the authors renounce all claim and invite personal, educational and commercial use.
- **Attribution:** not required. A courtesy line is suggested: "Made with Natural Earth."
- **Derived data redistributable?** Yes.
- **Caveat:** the terms page also reproduces a separate grant by The Washington Post for the sole purpose of creating a
  world base map, which sits next to the public-domain statement. How the two interact was not checked; for data used
  only as a base map this is not a concern.
- **Date checked:** 2026-10-18 (mirrors of the terms page; the official page was only partly visible).

## ESA WorldCover 2021 (land cover, 10 m)

- **License:** Creative Commons Attribution 4.0 International (CC BY 4.0), "free of charge, without restriction of
  use". Download page: <https://worldcover2021.esa.int/download>; license: <https://creativecommons.org/licenses/by/4.0/>
- **Commercial use allowed?** Yes: CC BY 4.0 allows commercial use with attribution.
- **Attribution:** for a layer in a published map: "© ESA WorldCover project / Contains modified Copernicus Sentinel
  data (2021) processed by ESA WorldCover consortium". When citing the data: Zanaga et al. (2022), "ESA WorldCover
  10 m 2021 v200".
- **Derived data redistributable?** Yes, as an adaptation under CC BY 4.0, with credit and a note that it was changed.
- **Date checked:** 2026-10-18 (official download page and catalog mirrors, via search summaries).

## Credits to show in the game

When a package built from a dataset ships, the game's credits or legal screen must carry that dataset's text above
(localization keys, architecture §9). Draft, to settle after verification:

> Terrain contains Copernicus DEM data produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and © Airbus Defence
> and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved. [HydroSHEDS
> notice as the agreement requires.] Land cover: © ESA WorldCover project / Contains modified Copernicus Sentinel data
> (2021) processed by ESA WorldCover consortium. Made with Natural Earth.

## Verification checklist (for a person with normal web access)

- [ ] Copernicus DEM GLO-30: read the license PDF; confirm commercial use and the redistribution of adapted data.
- [ ] HydroSHEDS: read the license agreement; confirm commercial use, redistribution of derived rivers and lakes, and
      copy the required notice. Do the same for HydroLAKES if it is used.
- [ ] Natural Earth: read the official terms page.
- [ ] ESA WorldCover: read the license on the official page (CC BY 4.0 expected).
- [ ] Update the "Date checked" lines and remove the caveats at the top.
