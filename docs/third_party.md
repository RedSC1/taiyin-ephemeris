# Third-party software and data

Status: Current

Taiyin combines original project code with third-party source, published
astronomical models, generated datasets, and external reference results. This
page is a practical overview for users and integrators. The authoritative
attribution and redistribution terms are in the
root [`NOTICE`](../NOTICE), [`LICENSE`](../LICENSE), retained source headers,
and the README or manifest stored beside each data product.

## Software and algorithm sources

| Component | Use in Taiyin | Location and terms |
| --- | --- | --- |
| ERFA / IAU SOFA | A focused subset of CIRS and astrometry routines | [`src/internal/erfa_cirs_subset.cpp`](../src/internal/erfa_cirs_subset.cpp) retains the ERFA/SOFA notices and BSD-3-Clause redistribution terms. |
| miniz | ZIP/deflate support used by runtime data containers | The split C sources in [`src/third_party/miniz/`](../src/third_party/miniz/) retain MIT notices; `miniz.c` also retains the complete upstream Unlicense/public-domain dedication. |
| Astronomy Engine | Compact L1.2 Galilean-moon coefficient subset | [`src/third_party/astronomy_engine/`](../src/third_party/astronomy_engine/) retains the adapted subset and Don Cross's MIT license. Taiyin supplies its own differentiated state evaluation and frame integration. |
| Shouxing Astronomical Calendar (sxwnl) | Archived eclipse implementations, Chinese-calendar behavior references, and regression oracles | Archived source is isolated under [`legacy/sxwnl/`](../legacy/sxwnl/) and is not linked into the production runtime. Full attribution and the upstream source statement are retained in [`NOTICE`](../NOTICE). |

Taiyin previously adapted selected sxwnl solar- and lunar-eclipse geometry.
Those ports and their frozen route/oracle material are now archived under
`legacy/sxwnl/` for comparison only. Production eclipse and occultation code
uses Taiyin's independently maintained three-dimensional shadow,
ellipsoid-intersection, route, and local-observer geometry. The Chinese
calendar uses independently implemented winter-solstice-year and
no-principal-term rules, with sxwnl retained as one behavioral oracle; its
historical China profile includes fixed civil-day results generated from
sxwnl/SSQ. Some calendar and eclipse regression fixtures likewise retain
sxwnl results.

## Astronomical models and fitting references

| Source | Use in Taiyin | Distribution note |
| --- | --- | --- |
| ELP/MPP02 lunar theory | Frequency basis for the built-in compact lunar model | The runtime contains 1,175 selected phase terms from the DE405-constant-set ELP/MPP02 tables. The complete ELP table is a regeneration input and is not bundled. |
| NASA/JPL DE441 | Fit and validation reference for the compact lunar residual, planetary semi-analytical models, event seed models, and packaged ephemerides | DE441 BSP files are not bundled. Generated Taiyin coefficients and data products are distributed instead. |

The ELP lunar theory was developed by Michelle Chapront-Touzé and Jean
Chapront. ELP/MPP02 is the revision by Jean Chapront and Gérard Francou and
incorporates the MPP01 planetary perturbations by P. Bidart. The revision is
described in *The lunar theory ELP revisited. Introduction of new planetary
perturbations*, Astronomy & Astrophysics 404 (2003), 735–742,
doi:10.1051/0004-6361:20030529.

The 35,901-term candidate pool was converted in DE405 mode from the published
`ELP_MAIN.S1`–`S3` and `ELP_PERT.S1`–`S3` files. The exact converted-input
checksum, source revision, and transformation chain are retained in the
private maintainer provenance record; the six complete upstream coefficient
files are not bundled with Taiyin. Regeneration and validation tooling that
reads JPL BSP files uses optional Python packages such as NumPy, jplephem, and
PyERFA; none is a runtime dependency or part of the public source snapshot.

## Packaged and optional data

| Product | Upstream source | Local provenance |
| --- | --- | --- |
| Major-body, satellite-center-of-body, and asteroid OPM2 products | NASA/JPL planetary, satellite, asteroid SPK/Horizons data, with documented self-integrated extensions for some long-range asteroid intervals | [`data/ephemerides/opm2/`](../data/ephemerides/opm2/) README and `MANIFEST.json` files |
| Kepler/TKC1 small-body packs | NASA/JPL Small-Body Database Query API | [`data/kepler/sbdb/manifest.json`](../data/kepler/sbdb/manifest.json) |
| TSC1 star catalogs | Gaia DR3, ESA Hipparcos, Yale Bright Star Catalogue/BSC5, project-maintained special directions, and the Stellarium sky-cultures Chinese/western line-star selection and names (CC BY-SA) | [`tsc1_v1_known_limitations.md`](tsc1_v1_known_limitations.md) describes the source priority and record counts; [`data/stars/catalogs/lite/required_stars.json`](../data/stars/catalogs/lite/required_stars.json) pins the Stellarium source revision. |
| TLL1 lunar-limb table | SELENE (Kaguya) LALT global topography from the SELENE Data Archive at ISAS/JAXA | [`data/lunar-limb/README.md`](../data/lunar-limb/README.md) contains the source product, requested acknowledgment, generation method, and checksum. |

OPM2, TKC1, TSC1, and TLL1 are Taiyin runtime formats. Converting source data
into one of these formats does not replace the source provider's terms. When a
data pack is redistributed, keep its adjacent README and manifest metadata
with it.

## External validation references

The test suite and technical documentation also compare selected results with
NASA eclipse/transit catalogs, JPL Horizons, Purple Mountain Observatory
tables, Swiss Ephemeris, SOFA/ERFA, and sxwnl. These are reference results and
behavioral oracles; they are not additional runtime dependencies unless a test
explicitly enables the corresponding external program or dataset.
