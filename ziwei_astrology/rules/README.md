# Ziwei rule resources

`default.toml` is a profile manifest. It points to independent resources:

- `stars.toml`: stable star declaration order and categories;
- `placement.toml`: final branch answer tables;
- `brightness.toml`: twelve-branch brightness tables;
- `sihua.toml`: independently selectable ten-stem Si-Hua rows;
- `masters.toml`: life-master and body-master tables.

`ZiweiDataCatalog` parses the manifest and every declared option once.
`ZiweiContext` then selects one option for each independent entry without
rereading these files. Explicit catalog reload publishes a new immutable
snapshot; already-created contexts retain the previous snapshot.

Every selectable entry carries an option name. If a profile omits a component
default or a per-rule selection, the loader selects `option1`. For example, a
profile may override only one placement and one Si-Hua stem without coupling
those choices to brightness:

```toml
[placement]
tiankui = "option2"

[sihua]
geng = "option2"
```

The official `option1` placement resource is a ROM-like result of offline
enumeration. A placement contains only:

```toml
[[placements]]
star = "tianji"
option = "option1"
inputs = ["anchor.ziwei"]
shape = [12]
positions = [11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
```

One to three bounded inputs are allowed, with at most 384 final entries. The
runtime schema has no offset, direction, pipeline, expression, condition, or
arbitrary-dimensional evaluator. The loader verifies that `shape` exactly
matches every input domain, compiles row-major strides, and resolves names to
`StarId`; placement then performs only indexing and a table read.

The bundled tables are generated from the defaults in the author's
MIT-licensed Dart `ziwei_core` 0.13.0 implementation. Regenerate them with:

```sh
python3 ziwei_astrology/tools/migrate_ziwei_core_rules.py \
  /path/to/ziwei_core \
  ziwei_astrology/rules
```

The migration tool understands the retired Dart rule representation only as
an offline oracle input. It evaluates every finite combination and writes the
final answer tables; none of that representation reaches the C++ loader.

Traditional-school differences belong in independent options for the affected
rule dimension. Profiles are convenience selections, not monolithic school
presets. A resource is rejected rather than partially loaded when it contains
duplicates, missing selected options, invalid shapes, unknown stars, or values
outside their finite domains.

Calendar-source policies are deliberately not encoded as table-option names.
五虎遁, natal Si-Hua, body master, and flow limits can independently choose
the lunar or solar-term pillar set through C++ options. The selected Si-Hua
row and its source stem are therefore separate decisions, just as brightness
and placement variants are separate decisions.

The numeric differential corpora under `tests/data/` record the output of the
Dart 0.13.0 oracle. They are test fixtures, not runtime rule resources.
