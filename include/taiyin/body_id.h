#ifndef TAIYIN_BODY_ID_H
#define TAIYIN_BODY_ID_H

namespace taiyin {

// Stable solar-system target IDs follow the NAIF/SPICE integer convention used
// by SPK kernels and Taiyin source descriptors. Major-planet barycenters are
// distinct from planet centers such as Earth (399) and Mars (499).
const int TAIYIN_BODY_SOLAR_SYSTEM_BARYCENTER = 0;
const int TAIYIN_BODY_SSB = TAIYIN_BODY_SOLAR_SYSTEM_BARYCENTER;

const int TAIYIN_BODY_MERCURY_BARYCENTER = 1;
const int TAIYIN_BODY_VENUS_BARYCENTER = 2;
const int TAIYIN_BODY_EARTH_MOON_BARYCENTER = 3;
const int TAIYIN_BODY_EMB = TAIYIN_BODY_EARTH_MOON_BARYCENTER;
const int TAIYIN_BODY_MARS_BARYCENTER = 4;
const int TAIYIN_BODY_JUPITER_BARYCENTER = 5;
const int TAIYIN_BODY_SATURN_BARYCENTER = 6;
const int TAIYIN_BODY_URANUS_BARYCENTER = 7;
const int TAIYIN_BODY_NEPTUNE_BARYCENTER = 8;
const int TAIYIN_BODY_PLUTO_BARYCENTER = 9;

const int TAIYIN_BODY_SUN = 10;
const int TAIYIN_BODY_MERCURY = 199;
const int TAIYIN_BODY_VENUS = 299;
const int TAIYIN_BODY_MOON = 301;
const int TAIYIN_BODY_EARTH = 399;
const int TAIYIN_BODY_MARS = 499;
const int TAIYIN_BODY_JUPITER = 599;
const int TAIYIN_BODY_SATURN = 699;
const int TAIYIN_BODY_URANUS = 799;
const int TAIYIN_BODY_NEPTUNE = 899;
const int TAIYIN_BODY_PLUTO = 999;

const int TAIYIN_BODY_PHOBOS = 401;
const int TAIYIN_BODY_DEIMOS = 402;
const int TAIYIN_BODY_IO = 501;
const int TAIYIN_BODY_EUROPA = 502;
const int TAIYIN_BODY_GANYMEDE = 503;
const int TAIYIN_BODY_CALLISTO = 504;
const int TAIYIN_BODY_TITAN = 606;
const int TAIYIN_BODY_TRITON = 801;
const int TAIYIN_BODY_CHARON = 901;

// Selected asteroid and minor-body constants mirror the current source
// descriptor IDs used by Taiyin converters and oracle tests. Do not infer a
// complete asteroid numbering scheme from this short list; broad asteroid
// coverage should come from generated catalog metadata.
const int TAIYIN_BODY_CERES = 2000001;
const int TAIYIN_BODY_PALLAS = 2000002;
const int TAIYIN_BODY_JUNO = 2000003;
const int TAIYIN_BODY_VESTA = 2000004;
const int TAIYIN_BODY_EROS = 2000433;
const int TAIYIN_BODY_CHIRON = 20002060;
const int TAIYIN_BODY_PHOLUS = 20005145;
const int TAIYIN_BODY_NESSUS = 20007066;
const int TAIYIN_BODY_LILITH = 20001181;

// Dynamically registered stars and custom bodies live in a private non-NAIF
// range. Star catalog lookup should use catalog IDs, aliases, or provider
// resolution APIs; these dynamic IDs are runtime route keys, not universal star
// identifiers.
const int TAIYIN_PRIVATE_CELESTIAL_BODY_ID_START = 1000000000;

}  // namespace taiyin

#endif  // TAIYIN_BODY_ID_H
