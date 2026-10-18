# Data licenses

The terrain pipeline (`tools/terrain`, docs/architecture.md §15.5) builds map packages from open datasets, and the
**derived data ships in the game** (heights, rivers, lakes, land cover layers). This file records, per dataset,
what its license says, whether the game may use it commercially, what credit it needs, and whether the derived
data may be redistributed.

## Read this first: what is verified and what is not

**First pass (2026-10-18, a cloud session):** the publishers' websites could not be reached, so everything was
taken from search summaries and catalog mirrors.

**Second pass (2026-10-18, the PC session, while building T1.2.2.1):** the primary documents were read for three of
the four datasets. They are quoted below. This is a careful reading of the texts by a software assistant, **not legal
advice**: before the game ships, the owner should have the obligations marked **(shipping)** below checked by someone
qualified, because some of them reach the game's end user license agreement (EULA) and credits.

| Dataset | Primary document read? | Usable in a commercial game? | What is still open |
|---|---|---|---|
| Natural Earth | **Yes** (official terms page) | **Yes**: public domain, no credit needed | nothing important |
| Copernicus DEM GLO-30 | **Yes** (the license PDF) | **Yes**: free; reproduction, distribution and adaptation are granted; the license never mentions commercial use at all | the exact notices and a disclaimer must reach the end user **(shipping)** |
| HydroLAKES | **Yes** (official product page) | **Yes**: CC BY 4.0, credit required | nothing important |
| HydroRIVERS (HydroSHEDS) | **Yes** (the License Agreement, Appendix A of the HydroSHEDS Technical Documentation v1.4) | **Yes, on conditions**: "free for non-commercial and commercial use", but the agreement has an EULA requirement, a no-stand-alone-distribution rule and an **indemnification** clause | the owner must decide whether to accept those terms, or use the fallback **(shipping)** |
| ESA WorldCover 2021 | **No**: esa-worldcover.org failed a TLS certificate check from the PC | expected yes (CC BY 4.0 per the first pass) | read the license page |

**None is known to be unsuitable for a commercial game.** The one that needs a decision is HydroRIVERS (indemnification).
The fallbacks are below.

### Fallbacks, if a dataset is refused

- **Rivers:** Natural Earth's rivers (public domain; coarse, 1:10m, so fewer small channels and hand corrections in
  `tools/terrain/maps/<map>/` would carry more of the work), or OpenStreetMap waterways (ODbL: credit "© OpenStreetMap
  contributors" and **share-alike on a derived database**, which needs its own legal reading).
- **Elevation:** NASA SRTM (about 30 m, in the public domain as far as I know; not verified) in place of Copernicus
  DEM; it has voids and older data, and the pipeline's mosaic step would need a void fill.
- **Land cover / vegetation:** none needed for a start; the fertility and vegetation layers are authored and derived
  (T1.2.4.1, T1.2.4.2).

## Copernicus DEM GLO-30 (elevation, 30 m)

- **What it is:** a **digital surface model**: heights include buildings, infrastructure and vegetation (AWS readme).
  In towns and on tree-covered ground the terrain therefore has bumps that are not ground; see the notes in
  `tools/terrain/README.md`.
- **Where from:** the AWS Open Data bucket `copernicus-dem-30m` (<https://registry.opendata.aws/copernicus-dem/>,
  managed by Sinergise, no account). Tile URL pattern in `tools/terrain/terrain/dem.py`. "GLO-30 Public" is
  **not complete**: the AWS readme says a small subset of tiles covering specific countries has not been released to
  the public. **Check the list of unreleased tiles against a map before building it** (T1.2.6.1); the readme links the
  list. Ocean areas have no tiles: heights there are zero (the pipeline treats them as sea).
- **License:** "Licence for Copernicus DEM instance COP-DEM-GLO-30-F Global 30m Full, Free & Open", read in full:
  <https://documentation.dataspace.copernicus.eu/APIs/SentinelHub/Data/DEM/resources/license/License-COPDEM-30.pdf>
  (the AWS page links to the Copernicus Data Space collection page, <https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM>,
  for the same license; whether the page's text is identical to the PDF was not compared).
- **Commercial use allowed?** The license grants, free of charge (Article 5), worldwide and without time limit
  (Article 3), non-exclusive rights of **(a) reproduction, (b) distribution, (c) communication to the general public,
  (d) adaptation, modification and combination with other data** (Article 4). The word "commercial" does not occur in
  the text, and there is no purpose restriction. So there is no non-commercial clause.
- **Obligations (Article 6) (shipping):**
  - (a) when distributing or communicating the data, tell the public the source with the notice: "© DLR e.V. 2010-2014
    and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights
    reserved".
  - (b) when the data was adapted or modified (ours is: reprojected, resampled, corrected), use: "produced using
    Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under
    COPERNICUS by the European Union and ESA; all rights reserved".
  - (c) ensure "Subsequent Users" understand that: "The organisations in charge of the Copernicus programme by law or by
    delegation do not incur any liability for any use of the Copernicus WorldDEM-30".
  - (d) do not give the impression that the game is endorsed by the Copernicus programme or its entities.
  - (e) anyone you license distribution or communication rights to must be bound by the same obligations. The shipped
    game gives players the heights, so its EULA or credits screen should carry (b) and (c).
- **Warranty (Article 7):** "as is"; the user protects the licensor and the entities in charge of Copernicus against
  liability claims resulting from its use of the data. This is lighter than the HydroSHEDS indemnification but is
  also something to read before shipping.
- **Trademarks (Article 9):** "WorldDEM" may be used in the context of these terms; the Copernicus trademark has
  separate terms.
- **Date read:** 2026-10-18.

## HydroSHEDS: HydroRIVERS and HydroLAKES (rivers and lakes)

The two products have **different licenses**: this resolves the conflict noted in the first pass (one catalog said
CC BY 4.0, others the WWF agreement: each was right about a different product).

- **HydroLAKES:** "licensed under a Creative Commons Attribution (CC-BY) 4.0 International License" (official page,
  <https://www.hydrosheds.org/products/hydrolakes>). Commercial use and redistribution of derived data are allowed
  with credit. Cite: Messager et al. (2016), Nature Communications 7:13603.
- **HydroRIVERS:** "freely available for scientific, educational and commercial use", "distributed under the same
  license agreement as the HydroSHEDS core products" (official page, <https://www.hydrosheds.org/products/hydrorivers>).
  The agreement is **Appendix A of the HydroSHEDS Technical Documentation v1.4**
  (<https://data.hydrosheds.org/file/technical-documentation/HydroSHEDS_TechDoc_v1_4.pdf>), read in full. Its terms:
  - **Distribution (2.1.2):** the Licensed Materials may be distributed "incorporated into any Derivative Works to End
    Users", under "an end user license agreement with terms that are at least as protective of the Licensed Materials
    as are the terms of this License". "In no event shall Licensee license or distribute the Licensed Materials as a
    stand-alone product." End users get the data "for such party's use and not for redistribution" (1.2). **(shipping)**:
    a game that ships rivers derived from HydroRIVERS inside the game fits this, but the EULA must carry the matching
    terms, and the river files must not be offered on their own.
  - **No reverse engineering (2.1.3)**, to be repeated in the EULA. **(shipping)**
  - **Indemnification (5.2):** the licensee "will defend, indemnify and hold WWF harmless" against claims arising from
    the use of the data by the licensee or its end users and from its distribution. **This is the clause the owner must
    accept or avoid**; if not, use the Natural Earth fallback for rivers.
  - **Liability and waiver (6), no warranty (4), termination (7):** the license ends automatically on breach.
  - **Required attribution (Exhibit B), in the documentation or metadata of the product (shipping):** "This product
    [product name] incorporates data from the HydroSHEDS version 1 database which is © World Wildlife Fund, Inc.
    (2006-2022) and has been used herein under license. WWF has not evaluated the data as altered and incorporated
    within [product name], and therefore gives no warranty regarding its accuracy, completeness, currency or
    suitability for any particular purpose. Portions of the HydroSHEDS v1 database incorporate data which are the
    intellectual property rights of © USGS (2006-2008), NASA (2000-2005), ESRI (1992-1998), CIAT (2004-2006),
    UNEP-WCMC (1993), WWF (2004), Commonwealth of Australia (2007), and Her Royal Majesty and the British Crown and are
    used under license. The HydroSHEDS v1 database and more information are available at https://www.hydrosheds.org."
    Cite: Lehner, B., Verdin, K., Jarvis, A. (2008), Eos, Transactions, AGU 89(10): 93-94.
- **Date read:** 2026-10-18. Not yet used by the pipeline (T1.2.3.1).

## Natural Earth (coastlines, country and water vector data)

- **License:** public domain; official terms page read: <https://www.naturalearthdata.com/about/terms-of-use/>. It says
  all versions of the raster and vector data "are in the public domain", may be used "in any manner, including
  modifying the content and design", and invites "personal, educational, and commercial purposes. No permission is
  needed to use Natural Earth. Crediting the authors is unnecessary."
- **Attribution:** not required. A courtesy line is suggested: "Made with Natural Earth."
- **Derived data redistributable?** Yes.
- **Caveat (unchanged):** the terms page reproduces a grant by The Washington Post to Natural Earth "for the sole purpose
  of creating a world base map". Our land/sea mask is a map layer, so this is not a concern in practice.
- **Used for:** the land polygons `ne_10m_land.zip`, <https://naciscdn.org/naturalearth/10m/physical/ne_10m_land.zip>
  (3.3 MB, checked by the pipeline against the checksum in `map.json`).
- **Date read:** 2026-10-18.

## ESA WorldCover 2021 (land cover, 10 m)

- **License:** Creative Commons Attribution 4.0 International (CC BY 4.0) according to the first pass. **The official
  page was not read**: esa-worldcover.org failed a TLS certificate check from the PC on 2026-10-18 (the first pass also
  could not reach it). Download page: <https://worldcover2021.esa.int/download>; license: <https://creativecommons.org/licenses/by/4.0/>
- **Attribution (first pass):** for a layer in a published map: "© ESA WorldCover project / Contains modified Copernicus
  Sentinel data (2021) processed by ESA WorldCover consortium".
- **Status:** **not verified.** Not used by the pipeline yet.

## Credits to show in the game

When a package built from a dataset ships, the game's credits or legal screen must carry that dataset's text above
(localization keys, architecture §9). Draft, to settle with the owner:

> Terrain: produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018
> provided under COPERNICUS by the European Union and ESA; all rights reserved. The organisations in charge of the
> Copernicus programme by law or by delegation do not incur any liability for any use of the Copernicus WorldDEM-30.
> [Rivers: the HydroSHEDS Exhibit B notice above, with the product's name.] Lakes: HydroLAKES, © Messager et al.
> (2016), CC BY 4.0. Land cover: © ESA WorldCover project / Contains modified Copernicus Sentinel data (2021)
> processed by ESA WorldCover consortium. Made with Natural Earth.

## Verification checklist

- [x] Copernicus DEM GLO-30: license PDF read; commercial use not restricted, adaptation and distribution granted
      (2026-10-18). Still to do by the owner: have the Article 6 notices and the disclaimer put in the EULA / credits.
- [x] HydroSHEDS: HydroRIVERS agreement read; HydroLAKES license read on its official page (2026-10-18). **Owner's
      decision needed on the indemnification clause**, and the EULA terms the agreement requires.
- [x] Natural Earth: official terms page read (2026-10-18).
- [ ] ESA WorldCover: read the license on the official page (blocked by a certificate error from the PC).
- [ ] Check the list of unreleased Copernicus GLO-30 tiles against the Ancient Near East bounds (T1.2.6.1).
