# Star Identity Manifest CSV Schema

The star identity manifest is a build/data-preparation file used before Gaia DR3 astrometry is fetched. It answers:

- Which stars belong to a Taiyin star data tier?
- Which stable Taiyin ID should the runtime use?
- Which external IDs can later be used to query Gaia, Hipparcos, BSC, SIMBAD, or other catalogues?

It is not a runtime ephemeris file and is not the final `.tsc1`/`.tsr1` binary catalog.

## File format

CSV with a header row. UTF-8 text. Empty fields mean unknown/not applicable.

## Columns

```text
tier_flags,canonical_id,display_name,aliases,hip_id,hr_id,hd_id,gaia_dr3_source_id,simbad_id,bayer,flamsteed,constellation,vmag,priority,notes
```

### `tier_flags`

Pipe-separated tier labels:

- `fixed_traditional`
- `bright_bsc`
- `hipparcos`

Examples:

```text
fixed_traditional
fixed_traditional|bright_bsc|hipparcos
bright_bsc|hipparcos
hipparcos
```

### `canonical_id`

Stable lowercase snake_case Taiyin ID.

Recommended precedence:

1. curated fixed-star IDs, e.g. `spica`, `regulus`
2. BSC generated IDs, e.g. `hr_5056`
3. Hipparcos generated IDs, e.g. `hip_65474`

### `display_name`

Human-readable display name. Examples: `Spica`, `HR 5056`, `HIP 65474`.

### `aliases`

Comma-separated aliases. Prefer lowercase snake_case for machine lookup aliases:

```text
alpha_vir,vir_alpha,hip_65474,hr_5056,hd_116658
```

Aliases are not canonical and should not change the `canonical_id`.

### External identifiers

- `hip_id`: Hipparcos catalogue number.
- `hr_id`: Harvard Revised / Yale Bright Star Catalogue number.
- `hd_id`: Henry Draper catalogue number.
- `gaia_dr3_source_id`: Gaia DR3 source ID, usually empty in this first manifest and filled by a later Gaia fetch/crossmatch tool.
- `simbad_id`: optional SIMBAD identifier.
- `bayer`: Bayer designation, e.g. `alpha Vir`.
- `flamsteed`: Flamsteed designation, e.g. `67 Vir`.
- `constellation`: IAU 3-letter constellation abbreviation.

### `vmag`

Visual magnitude when known. This field is descriptor metadata only; the later astrometry/photometry table is authoritative for final catalog compilation.

### `priority`

Integer priority for conflict resolution and curated importance.

Suggested convention:

- `100`: built-in/test/reference entries, e.g. Spica or Galactic Center placeholders.
- `80`: traditional fixed stars.
- `20`: BSC stars.
- `10`: Hipparcos-only generated rows.

### `notes`

Free text. Keep short. Do not put machine-critical information only in `notes`.

## Source policy

This manifest should be generated from openly redistributable or user-provided sources.

Recommended sources:

- Fixed/traditional seed: project-curated CSV.
- Bright stars: Yale Bright Star Catalogue / VizieR `V/50/catalog.gz` decompressed to a local `catalog.dat`.
- Hipparcos: ESA Hipparcos Main Catalogue / VizieR `I/239/hip_main.dat`.

Do not import Swiss Ephemeris `fixstars.cat` into this repository. Swiss Ephemeris has AGPL/commercial licensing constraints; if a user wants to use it, support it later as an explicit user-provided external data source.

## Future Gaia step

A later Gaia fetch tool should consume this manifest and fill Gaia astrometry fields in a separate table. It should prefer:

1. explicit `gaia_dr3_source_id`, if present,
2. `hip_id` via Gaia DR3/Hipparcos crossmatch,
3. user-provided SIMBAD/VizieR enrichment for records without HIP.
