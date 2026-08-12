#!/usr/bin/env node

const AU_KM = 149597870.7;
const EARTH_RADIUS_KM = 6378.137;
const SUN_RADIUS_KM = 695700.0;
const MOON_RADIUS_KM = 1737.4;

function lineT(x, y, vx, vy, r, n) {
  const A = vx * vx + vy * vy;
  const B = x * vx + y * vy;
  const C = x * x + y * y - r * r;
  const D = B * B - A * C;
  if (D < 0 || A < 1e-30) return null;
  const sqrtD = Math.sqrt(D);
  return n === 0 ? (-B - sqrtD) / A : (-B + sqrtD) / A;
}

function lecXY(input) {
  let diff = input.moonLon + Math.PI - input.sunLon;
  diff = ((diff + Math.PI) % (2 * Math.PI)) - Math.PI;
  if (diff < -Math.PI) diff += 2 * Math.PI;
  const x = diff * Math.cos((input.moonLat - input.sunLat) / 2);
  const y = input.moonLat + input.sunLat;
  const moonDistKm = input.moonDistAu * AU_KM;
  const sunDistKm = input.sunDistAu * AU_KM;
  const effEarthRadius = input.earthRadiusKm * input.shadowEarthScale;
  const penumbraKm = effEarthRadius
    + moonDistKm * (input.sunRadiusKm * input.shadowSunScale
      + input.earthRadiusKm * input.shadowParallaxScale) / sunDistKm;
  const umbraKm = effEarthRadius
    - moonDistKm * (input.sunRadiusKm * input.shadowSunScale
      - input.earthRadiusKm * input.shadowParallaxScale) / sunDistKm;
  return {
    x,
    y,
    rmin: Math.sqrt(x * x + y * y),
    moonRadius: Math.atan2(input.moonRadiusKm, moonDistKm),
    umbraRadius: umbraKm > 0 ? Math.atan2(umbraKm, moonDistKm) : -Math.atan2(-umbraKm, moonDistKm),
    penumbraRadius: Math.atan2(penumbraKm, moonDistKm),
    moonDistAu: input.moonDistAu,
    sunDistAu: input.sunDistAu
  };
}

function lecMax(z1, z2, dt) {
  const vx = (z2.x - z1.x) / dt;
  const vy = (z2.y - z1.y) / dt;
  const denom = vx * vx + vy * vy;
  const dtDays = denom < 1e-30 ? 0 : -(z1.x * vx + z1.y * vy) / denom;
  return { geometry: z1, vx, vy, dt: dtDays };
}

function input(moonLon, moonLat, moonDistAu, sunLon, sunLat, sunDistAu, shadowEarthScale, shadowSunScale, shadowParallaxScale) {
  return {
    moonLon,
    moonLat,
    moonDistAu,
    sunLon,
    sunLat,
    sunDistAu,
    earthRadiusKm: EARTH_RADIUS_KM,
    sunRadiusKm: SUN_RADIUS_KM,
    moonRadiusKm: MOON_RADIUS_KM,
    shadowEarthScale,
    shadowSunScale,
    shadowParallaxScale
  };
}

const cases = {
  lineT: [
    { args: [0.12, -0.08, -0.65, 0.21, 0.37, 0], value: lineT(0.12, -0.08, -0.65, 0.21, 0.37, 0) },
    { args: [0.12, -0.08, -0.65, 0.21, 0.37, 1], value: lineT(0.12, -0.08, -0.65, 0.21, 0.37, 1) },
    { args: [0.9, 0.7, 0.1, 0.2, 0.1, 0], value: lineT(0.9, 0.7, 0.1, 0.2, 0.1, 0) }
  ],
  lecXY: [
    (() => {
      const args = input(3.2201, -0.0043, 0.002569555, 0.0784, 0.00012, 1.0032, 1.01, 1.0, 1.0);
      return { args, value: lecXY(args) };
    })(),
    (() => {
      const args = input(0.02, 0.012, 0.00271, 3.14, -0.004, 0.985, 1.0, 1.0, 1.0);
      return { args, value: lecXY(args) };
    })(),
    (() => {
      const args = input(-2.9, 0.031, 0.00245, 0.24, -0.02, 1.015, 1.0, 1.0008, 0.998);
      return { args, value: lecXY(args) };
    })()
  ],
  lecMax: [
    (() => {
      const z1 = lecXY(input(3.2201, -0.0043, 0.002569555, 0.0784, 0.00012, 1.0032, 1.01, 1.0, 1.0));
      const z2 = lecXY(input(3.2206, -0.0041, 0.0025696, 0.07845, 0.00011, 1.0032, 1.01, 1.0, 1.0));
      return { args: { z1, z2, dt: 60 / 86400 }, value: lecMax(z1, z2, 60 / 86400) };
    })(),
    (() => {
      const z1 = lecXY(input(0.02, 0.012, 0.00271, 3.14, -0.004, 0.985, 1.0, 1.0, 1.0));
      const z2 = lecXY(input(0.0198, 0.0123, 0.0027102, 3.1401, -0.0041, 0.985, 1.0, 1.0, 1.0));
      return { args: { z1, z2, dt: 120 / 86400 }, value: lecMax(z1, z2, 120 / 86400) };
    })()
  ]
};

console.log(JSON.stringify(cases, null, 2));
