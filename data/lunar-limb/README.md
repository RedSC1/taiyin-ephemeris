# Lunar-limb data

`kaguya_lalt_16ppd.tll1` is a Taiyin-generated lunar-limb profile table.
It is derived from the public Kaguya/SELENE Laser Altimeter global topographic
grid distributed by the SELENE Data Archive at ISAS/JAXA.

Source product:

- mission: SELENE (Kaguya);
- instrument: Laser Altimeter (LALT);
- product: `SLN-L-LALT-5-TOPO-GGT-NUM-V2.0`;
- source resolution: 1/16 degree;
- source reference surface: 1737.4 km sphere centered on the lunar center of
  mass in Mean Earth/Polar Axis body-fixed coordinates.

The generated table contains projected lunar-limb offsets at 0.5-degree
intervals in libration longitude and latitude and 0.2-degree intervals in
position angle. It covers the full optical-libration range needed by solar
eclipses:

- longitude: -9.0 to +9.0 degrees;
- latitude: -8.0 to +8.0 degrees;
- position angle: 0.0 to 359.8 degrees.

Attribution requested by the source archive:

> We thank the SELENE (Kaguya) LALT team and the SELENE Data Archive for
> providing the SELENE (Kaguya) data. SELENE is a Japanese mission developed
> and operated by JAXA.

The TLL1 conversion and profile generation are Taiyin project work. The
generated file is modified, value-added data and is not endorsed by JAXA.

Regenerate from the official PDS3 table:

```bash
python3 tools/generate_tll1_kaguya.py \
  --input /path/to/LALT_GGT_NUM.TAB \
  --output data/lunar-limb/kaguya_lalt_16ppd.tll1 \
  --validate-samples 250
```

The bundled table is 4,395,792 bytes. A fixed-seed 250-sample validation
against direct off-grid DEM evaluation measured 100.4 m RMS, 209.5 m 95th
percentile absolute error, and 376.9 m maximum absolute error.

Bundled file SHA-256:

```text
96c44b63e20813419eef292f9418c7b378de08a4844ef26e8c22a9553d81872b
```
