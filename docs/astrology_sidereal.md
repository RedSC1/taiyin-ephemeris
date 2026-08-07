# Sidereal Zodiac And Ayanamsha Models

Status: Current

The astrology extension treats an ayanamsha as a named sidereal-zodiac
zero-point model:

```text
sidereal longitude = normalize(tropical longitude - ayanamsha)
```

It is not an ephemeris route, a nutation model, or a replacement for the
precession model selected by `NativeCalcContext`.

## Built-In Models

The `ayanamsha_id` argument currently accepts:

- Fagan/Bradley
- Lahiri
- Raman
- Krishnamurti
- Galactic Center 0 Sagittarius, anchored to built-in Sgr A* astrometry
- True Chitra, anchored to built-in Spica astrometry

Reference-epoch models retain their historical reference precession model.
Star-anchored models propagate their built-in ICRF astrometry through the
normal fixed-star position pipeline and do not depend on a caller-installed
star catalog.

## Precession Policy Flags

Sidereal calls receive the native context, ayanamsha ID, and a `uint64_t`
flags word directly. Historical definitions use the native context's selected
precession model with mismatch compensation by default. Two mutually exclusive
high-word flags override that default:

- `TAIYIN_SIDEREAL_RAW_REFERENCE_OFFSET` keeps the native precession model and
  omits mismatch compensation.
- `TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION` uses the ayanamsha model entry's
  `reference_precession_model_id` when one is defined.

No policy mutates process-global precession configuration.

## Custom Models

Custom IDs start at `TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START`. Register an
`AyanamshaModelEntry` with `add_ayanamsha_model()`. Built-in and already
registered IDs cannot be replaced, and unknown IDs are rejected rather than
silently mapped to another convention.

An evaluator receives `AyanamshaDispatchData`, including the borrowed native
context, ayanamsha ID, TT epoch, native position flags, sidereal flags, and the
entry's `model_data`. The registry does not own `model_data`. Evaluators may be
called concurrently, so callback code and `model_data` must remain loaded,
valid, and safe for concurrent access for the remainder of the process.
Evaluators return a `Status`, and successful finite results are normalized to
`[0, 2*pi)`.

`reference_precession_model_id` is optional. A negative value keeps the native
context's selected precession model even under
`TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION`. A nonnegative value must identify
an already registered precession model.

The registry is a low-level extension point. A typed convenience object for a
user-supplied reference epoch and offset can be layered over it later without
introducing global mutable sidereal state.

## Position APIs

`AstrologyContext` owns a configured copy of `NativeCalcContext`. Call
`configure_astrology_context()` once with an ayanamsha, reference-plane policy,
and optional reference epoch. The embedded `native_context` then exposes that
sidereal plane as `TAIYIN_APPARENT_FRAME_CUSTOM`, so ordinary
`calc_position_*()` and `calc_state_*()` calls use the same output-frame
pipeline as other native frames. This also applies to registered astrology
targets such as the lunar nodes and apsides.

Low native-position bits passed during configuration fix the correction
semantics of star-anchored ayanamsha models. Output-shape bits are masked for
that evaluation. Per-call native flags still control the target calculation;
changing them does not silently redefine an already configured reference
frame.

The custom-frame evaluator returns an ICRF-to-sidereal rotation. Matrix
derivatives are obtained by the existing apparent-position machinery, so
velocity and acceleration are transformed consistently. Copies of
`AstrologyContext` repair the callback's self-reference.

`calc_ayanamsha_tt()` follows the extended ayanamsha convention: by default it
returns the selected mean ayanamsha plus longitude nutation (`dpsi`); adding
`TAIYIN_NATIVE_POSITION_NONUT` returns the mean ayanamsha. Sidereal ecliptic
positions still use the mean ayanamsha internally, because nutation in a true
tropical longitude and in the extended ayanamsha cancels rather than being
applied twice.
`calc_sidereal_position_tt()` and `calc_sidereal_position_ut()` evaluate a
normal position through a temporary `AstrologyContext`; they remain convenience
wrappers for callers that also need the unshifted longitude. XYZ and
equatorial output flags are rejected because these result structures expose
ecliptic longitude. `calc_sidereal_coordinates_*()` is likewise a convenience
wrapper over the configured native context.

## Sidereal Reference Planes

Sidereal calls retain native-position flags in their low 32 bits. Their high
bits select the reference plane and epoch scale; conflicting plane bits are
rejected by one shared flag resolver:

- no reference-plane bit selects the default ordinary sidereal zodiac on the
  mean ecliptic of date;
- `TAIYIN_SIDEREAL_REFERENCE_J2000_ECLIPTIC` uses the fixed mean ecliptic of
  J2000.0. It is the direct non-nutated J2000 choice and requires no epoch
  argument;
- `TAIYIN_SIDEREAL_REFERENCE_ECL_T0` uses the mean ecliptic fixed at the
  finite `reference_epoch_jd` argument;
- `TAIYIN_SIDEREAL_REFERENCE_SSY_PLANE` uses the invariable plane and its zero
  direction at the supplied finite reference epoch;
- `TAIYIN_SIDEREAL_REFERENCE_EPOCH_UT1` marks that epoch as UT1 rather than
  TT, and is valid only with `ECL_T0` or `SSY_PLANE`.

The fixed and invariable choices are full three-dimensional rotations from
ICRF, not a scalar longitude correction. `NONUT` has no effect on these mean
ecliptic planes. `SiderealCoordinates::coordinate_frame_id` identifies the
returned plane. Equatorial sidereal-coordinate requests remain tropical
mean/true equator-of-date output, so their `NONUT` semantics remain unchanged.
The native equatorial output request overrides the configured custom sidereal
frame for that call; reference-plane and precession-policy settings remain
stored in the astrology context for subsequent ecliptic calls.

For `SiderealPosition` on the default ecliptic-of-date plane,
`tropical_longitude_rad` follows the public ayanamsha convention: it is the
apparent/true tropical longitude by default and the mean tropical longitude
with `NONUT`. Consequently,
`sidereal_longitude_rad + calc_ayanamsha_tt()` reconstructs that field modulo
`2*pi`. On a fixed or invariable plane, the field remains the unshifted
longitude in the reported `coordinate_frame_id`; it is not tropical
ecliptic-of-date longitude.

The C ABI currently keeps the stateless convenience shape: it uses the same
high-word bits with `TAIYIN_C_` prefixes and accepts an optional split-JD
`reference_epoch_jd` pointer on each sidereal position/coordinate call. Pass a
null pointer when the selected plane does not use a reference epoch.
