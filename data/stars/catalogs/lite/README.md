# Lite TSC1 Star Catalog

`stars-bright-v5.tsc1` is the distribution-sized fixed-star catalog. Its
ordinary bright-star layer contains every available record with catalogue
visual magnitude `V <= 5.0`. It then adds:

- all HIP stars used by the line figures in Stellarium's Chinese sky culture;
- all HIP stars used by the twelve western zodiac line figures;
- Taiyin's two manual special-direction records, `galactic_center_j2000` and
  `sgr_a_apparent`.

The cultural selection is generated from Stellarium sky-cultures revision
`014fbb5e59233d133c22f9811af96b67d05a95c9`. It contains 1,385 Chinese
line stars and 141 western-zodiac line stars, or 1,399 unique HIP identifiers
after overlap. Unambiguous English and Simplified Chinese traditional star
names are retained as aliases, including names such as `织女一`, `角宿一`, and
`毕宿一`. Twelve aliases shared by more than one HIP star are omitted because
TSC1 alias lookup is intentionally one-to-one.

The Stellarium-derived cultural selection and names are provided under
CC BY-SA. The astrometric records retain their original Gaia DR3, Hipparcos,
BSC5, or manual provenance. See `required_stars.json` for the pinned source
revision and the exact generated selection.

Current contents:

| Catalog | Stars | Aliases | File size |
| --- | ---: | ---: | ---: |
| `stars-bright-v5.tsc1` | 2,057 | 12,242 | about 0.56 MB |
| `../stars-bright-gaia-bsc.tsc1` | 9,098 | 37,527 | about 1.9 MB |
| `../stars-hipparcos-gaia.tsc1` | 118,059 | 328,594 | about 21 MB |

The full catalogs remain in the parent directory. Language bindings may
package this lite catalog by default and make the larger catalogs optional.

Generate `required_stars.json` after obtaining the pinned upstream Chinese and
western `index.json` files and Chinese `po/zh_CN.po`:

```sh
python3 tools/generate_stellarium_skyculture_requirements.py \
  --chinese-index raw-data/stars/skycultures/chinese/index.json \
  --chinese-zh-cn-po raw-data/stars/skycultures/chinese/po/zh_CN.po \
  --western-index raw-data/stars/skycultures/western/index.json \
  --source-revision 014fbb5e59233d133c22f9811af96b67d05a95c9 \
  --output data/stars/catalogs/lite/required_stars.json
```

Then generate the lite catalog from the complete Hipparcos/Gaia table. The
identity manifest supplies V-band magnitudes both for selection and for the
stored lite-catalog magnitude field:

```sh
python3 tools/filter_tsc1_catalog.py \
  --input data/stars/catalogs/stars-hipparcos-gaia.tsc1 \
  --output data/stars/catalogs/lite/stars-bright-v5.tsc1 \
  --requirements data/stars/catalogs/lite/required_stars.json \
  --visual-magnitude-manifest raw-data/stars/manifests/star_identity_merged.csv \
  --max-magnitude 5
```

The filter preserves selected astrometry and ordinary aliases. It does not
refit stellar positions.
