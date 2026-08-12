# Lite TSC1 Star Catalog

`stars-bright-v5.tsc1` is the distribution-sized fixed-star catalog. It is
derived from `../stars-bright-gaia-bsc.tsc1`, selecting records with catalogue
visual magnitude `V <= 5.0`, retaining Taiyin's two manual special-direction
records (`galactic_center_j2000` and `sgr_a_apparent`), and applying the
minimum historical-coverage contract in `required_stars.json`.

That contract guarantees the 28 traditional Chinese mansion determinative
stars (距星), with Chinese and pinyin aliases such as `角宿一` and
`jiao_xiu_1`, plus one representative bright star for every western zodiac
constellation. It adds Alrescha (α Piscium, V≈5.23) beyond the ordinary
magnitude cut so Pisces remains represented.

The 28-star list is Taiyin's explicit v1 reference profile, not a claim that
all historical Chinese sky maps selected exactly the same determinative stars.
Song, Chen Zhuo, and later reconstructions can differ; a complete historical
star-official map belongs in a separately versioned sky-culture overlay rather
than in this astrometric star table.

Current contents:

| Catalog | Stars | Aliases | File size |
| --- | ---: | ---: | ---: |
| `stars-bright-v5.tsc1` | 2,114 | 9,621 | about 0.49 MB |
| `../stars-bright-gaia-bsc.tsc1` | 9,098 | 37,527 | about 1.9 MB |
| `../stars-hipparcos-gaia.tsc1` | 118,058 | 328,593 | about 21 MB |

The full catalogs remain in the parent directory; this folder does not replace
or modify them.  Language bindings may package this lite catalog by default and
make the larger catalogs optional downloads.

Regenerate the file from the checked-in bright catalog with:

```sh
python3 tools/filter_tsc1_catalog.py \
  --input data/stars/catalogs/stars-bright-gaia-bsc.tsc1 \
  --output data/stars/catalogs/lite/stars-bright-v5.tsc1 \
  --requirements data/stars/catalogs/lite/required_stars.json \
  --max-magnitude 5
```

The filter preserves selected TSC1 astrometry and aliases verbatim; it does not
refit stellar positions.
