#!/usr/bin/env node

const cs_rEar = 6378.1366;
const cs_ba = 0.99664719;
const cs_ba2 = cs_ba * cs_ba;
const cs_AU = 1.49597870691e8;
const cs_k = 0.2725076;
const cs_k2 = 0.2722810;
const cs_k0 = 109.1222;
const pi2 = Math.PI * 2;
const rad = 180 * 3600 / Math.PI;
const cs_sMoon = cs_k * cs_rEar * 1.0000036 * rad;

function sqrt(x) { return Math.sqrt(x); }
function abs(x) { return Math.abs(x); }
function sin(x) { return Math.sin(x); }
function cos(x) { return Math.cos(x); }
function tan(x) { return Math.tan(x); }
function atan(x) { return Math.atan(x); }
function atan2(y, x) { return Math.atan2(y, x); }

function rad2rrad(x) {
  x = (x + Math.PI) % pi2;
  if (x < 0) x += pi2;
  return x - Math.PI;
}

function llr2xyz(z) {
  const c = cos(z[1]);
  return [z[2] * c * cos(z[0]), z[2] * c * sin(z[0]), z[2] * sin(z[1])];
}

function xyz2llr(z) {
  return [atan2(z[1], z[0]), atan2(z[2], sqrt(z[0] * z[0] + z[1] * z[1])), sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2])];
}

function llrConv(z, E) {
  const r = llr2xyz(z);
  return xyz2llr([r[0], cos(E) * r[1] - sin(E) * r[2], sin(E) * r[1] + cos(E) * r[2]]);
}

function lineEll(x1, y1, z1, x2, y2, z2, e, r) {
  const dx = x2 - x1, dy = y2 - y1, dz = z2 - z1, e2 = e * e;
  const p = {};
  const A = dx * dx + dy * dy + dz * dz / e2;
  const B = x1 * dx + y1 * dy + z1 * dz / e2;
  const C = x1 * x1 + y1 * y1 + z1 * z1 / e2 - r * r;
  p.D = B * B - A * C;
  if (p.D < 0) return p;
  let D = sqrt(p.D);
  if (B < 0) D = -D;
  const t = (-B + D) / A;
  p.x = x1 + dx * t;
  p.y = y1 + dy * t;
  p.z = z1 + dz * t;
  const R = sqrt(dx * dx + dy * dy + dz * dz);
  p.R1 = R * abs(t);
  p.R2 = R * abs(t - 1);
  return p;
}

function lineEar2(x1, y1, z1, x2, y2, z2, e, r, I) {
  const P = cos(I[1]), Q = sin(I[1]);
  const X1 = x1, Y1 = P * y1 - Q * z1, Z1 = Q * y1 + P * z1;
  const X2 = x2, Y2 = P * y2 - Q * z2, Z2 = Q * y2 + P * z2;
  const p = lineEll(X1, Y1, Z1, X2, Y2, Z2, e, r);
  p.J = p.W = 100;
  if (p.D < 0) return p;
  p.J = rad2rrad(atan2(p.y, p.x) + I[0] - I[2]);
  p.W = atan(p.z / e / e / sqrt(p.x * p.x + p.y * p.y));
  return p;
}

function lineEar(P, Q, gst) {
  const p = llr2xyz(P), q = llr2xyz(Q);
  const r = lineEll(p[0], p[1], p[2], q[0], q[1], q[2], cs_ba, cs_rEar);
  if (r.D < 0) { r.J = r.W = 100; return r; }
  r.W = atan(r.z / cs_ba2 / sqrt(r.x * r.x + r.y * r.y));
  r.J = rad2rrad(atan2(r.y, r.x) - gst);
  return r;
}

function cirOvl(R, ba, R2, x0, y0) {
  const re = {};
  const d = sqrt(x0 * x0 + y0 * y0);
  const sinB = y0 / d, cosB = x0 / d;
  let cosA = (R * R + d * d - R2 * R2) / (2 * d * R);
  if (abs(cosA) > 1) { re.n = 0; return re; }
  let sinA = sqrt(1 - cosA * cosA);
  const ba2 = ba * ba;
  for (let k = -1; k < 2; k += 2) {
    let S = cosA * sinB + sinA * cosB * k;
    const g = R - S * S * (1 / ba2 - 1) / 2;
    cosA = (g * g + d * d - R2 * R2) / (2 * d * g);
    if (abs(cosA) > 1) { re.n = 0; return re; }
    sinA = sqrt(1 - cosA * cosA);
    const C = cosA * cosB - sinA * sinB * k;
    S = cosA * sinB + sinA * cosB * k;
    if (k === 1) re.A = [g * C, g * S]; else re.B = [g * C, g * S];
  }
  re.n = 2;
  return re;
}

function lineOvl(x1, y1, dx, dy, r, ba) {
  const p = {};
  const f = ba * ba;
  const A = dx * dx + dy * dy / f;
  const B = x1 * dx + y1 * dy / f;
  const C = x1 * x1 + y1 * y1 / f - r * r;
  let D = B * B - A * C;
  if (D < 0) { p.n = 0; return p; }
  if (!D) p.n = 1; else p.n = 2;
  D = sqrt(D);
  const t1 = (-B + D) / A, t2 = (-B - D) / A;
  p.A = [x1 + dx * t1, y1 + dy * t1];
  p.B = [x1 + dx * t2, y1 + dy * t2];
  const L = sqrt(dx * dx + dy * dy);
  p.R1 = L * abs(t1);
  p.R2 = L * abs(t2);
  return p;
}

function bse2db(z, I, f) {
  let r = xyz2llr(z);
  r = llrConv(r, I[1]);
  r[0] = rad2rrad(r[0] + I[0] - I[2]);
  if (f) r[1] = atan(tan(r[1]) / cs_ba2);
  return r;
}

function bseXY2db(x, y, I, f) {
  const b = f ? cs_ba : 1;
  const F = lineEar2(x, y, 2, x, y, 0, b, 1, I);
  return [F.J, F.W];
}

function Vxy(x, y, s, vx, vy) {
  const r = {};
  let h = 1 - x * x - y * y;
  if (h < 0) h = 0; else h = sqrt(h);
  r.vx = pi2 * (sin(s) * h - cos(s) * y);
  r.vy = pi2 * x * cos(s);
  r.Vx = vx - r.vx;
  r.Vy = vy - r.vy;
  r.V = sqrt(r.Vx * r.Vx + r.Vy * r.Vy);
  return r;
}

function rSM(mR, tanf1, tanf2, dyj) {
  const re = {};
  re.r1 = cs_k + tanf1 * mR;
  re.r2 = cs_k2 - tanf2 * mR;
  re.ar2 = abs(re.r2);
  re.sf = cs_k2 / mR / cs_k0 * (dyj + mR);
  return re;
}

function nanbei(M, vx0, vy0, h, r, I, bba) {
  let x = M[0] - vy0 / vx0 * r * h, y = M[1] + h * r;
  let z, vx, vy, v, sinA = 0, cosA = 1, js = 0;
  for (let i = 0; i < 3; i++) {
    z = 1 - x * x - y * y;
    if (z < 0) { if (js) break; z = 0; js++; }
    z = sqrt(z);
    x -= (x - M[0]) * z / M[2];
    y -= (y - M[1]) * z / M[2];
    vx = vx0 - pi2 * (sin(I[1]) * z - cos(I[1]) * y);
    vy = vy0 - pi2 * cos(I[1]) * x;
    v = sqrt(vx * vx + vy * vy);
    sinA = h * vy / v;
    cosA = h * vx / v;
    x = M[0] - r * sinA;
    y = M[1] + r * cosA;
  }
  const X = M[0] - cs_k * sinA, Y = M[1] + cs_k * cosA;
  const p = lineEar2(X, Y, M[2], x, y, 0, cs_ba, 1, I);
  return [p.J, p.W, x, y];
}

function mQie(M, vx0, vy0, h, r, I, bba, A) {
  const p = nanbei(M, vx0, vy0, h, r, I, bba);
  if (!A.f2) A.f2 = 0;
  A.f = p[1] == 100 ? 0 : 1;
  const pushed = [];
  if (A.f2 != A.f) {
    const g = lineOvl(p[2], p[3], vx0, vy0, 1, bba);
    let dj, F;
    if (g.n) {
      if (A.f) { dj = g.R2; F = [g.B[0], g.B[1]]; }
      else { dj = g.R1; F = [g.A[0], g.A[1]]; }
      F[2] = 0;
      const I2 = [I[0], I[1], I[2] - dj / sqrt(vx0 * vx0 + vy0 * vy0) * 6.28];
      const db = bse2db(F, I2, 1);
      pushed.push(db[0], db[1]);
    }
  }
  A.f2 = A.f;
  if (p[1] != 100) pushed.push(p[0], p[1]);
  return { point: p, f: A.f, f2: A.f2, pushed };
}

function mDian(M, vx0, vy0, AB, r, I, bba) {
  let p, a = M.slice(), R = 0;
  for (let i = 0; i < 2; i++) {
    const c = Vxy(a[0], a[1], I[1], vx0, vy0);
    p = lineOvl(M[0], M[1], c.Vy, -c.Vx, 1, bba);
    if (!p.n) break;
    if (AB) { a = [p.A[0], p.A[1], 0]; R = p.R1; }
    else { a = [p.B[0], p.B[1], 0]; R = p.R2; }
  }
  if (p && p.n && R <= r) {
    const db = bse2db([a[0], a[1], 0], I, 1);
    return { found: 1, value: [db[0], db[1]], R };
  }
  return { found: 0, value: [], R };
}

function jieX3(M, vx, vy, B, I, bba) {
  const r = B.r1;
  const out = {};
  let p = nanbei(M, vx, vy, +1, r, I, bba);
  out.penumbraNorth = [p[0], p[1]];
  p = nanbei(M, vx, vy, +1, B.r2, I, bba);
  out.coreNorth = [p[0], p[1]];
  p = bseXY2db(M[0], M[1], I, 1);
  out.center = [p[0], p[1]];
  p = nanbei(M, vx, vy, -1, B.r2, I, bba);
  out.coreSouth = [p[0], p[1]];
  p = nanbei(M, vx, vy, -1, r, I, bba);
  out.penumbraSouth = [p[0], p[1]];
  return out;
}

function parallax(z, H, fa, high) {
  let dw = 1;
  if (z[2] < 500) dw = cs_AU;
  z[2] *= dw;
  const f = cs_ba;
  const u = atan(f * tan(fa));
  const g = z[0] + H;
  const r0 = cs_rEar * cos(u) + high * cos(fa);
  const z0 = cs_rEar * sin(u) * f + high * sin(fa);
  const x0 = r0 * cos(g);
  const y0 = r0 * sin(g);
  let s = llr2xyz(z);
  s[0] -= x0;
  s[1] -= y0;
  s[2] -= z0;
  s = xyz2llr(s);
  z[0] = s[0];
  z[1] = s[1];
  z[2] = s[2] / dw;
}

function rspl_zbXY(P, L, fa) {
  const p = { S: P.S.slice(), M: P.M.slice(), g: P.g };
  const s = [p.S[0], p.S[1], p.S[2]];
  const m = [p.M[0], p.M[1], p.M[2]];
  parallax(s, p.g + L - p.S[0], fa, 0);
  parallax(m, p.g + L - p.M[0], fa, 0);
  p.mr = cs_sMoon / m[2] / rad;
  p.sr = 959.63 / s[2] / rad * cs_AU;
  p.x = rad2rrad(m[0] - s[0]) * Math.cos((m[1] + s[1]) / 2);
  p.y = m[1] - s[1];
  p.S_topo = s;
  p.M_topo = m;
  return p;
}

function finiteOrNull(x) {
  return Number.isFinite(x) ? x : null;
}

function lineEllFixture(args) {
  const p = lineEll(...args);
  return { args, D: p.D, valid: p.D >= 0, x: finiteOrNull(p.x), y: finiteOrNull(p.y), z: finiteOrNull(p.z), R1: finiteOrNull(p.R1), R2: finiteOrNull(p.R2) };
}

function lineT_contact(G, v, u, r, n) {
  const b = G.y * v - G.x * u;
  const A = u * u + v * v;
  const B = u * b;
  const C = b * b - r * r * v * v;
  let D = B * B - A * C;
  if (D < 0) return 0;
  D = sqrt(D);
  if (!n) D = -D;
  return G.t + ((-B + D) / A - G.x) / v;
}

function llr_to_xyz(lon, lat, r) {
  const c = cos(lat);
  return [r * c * cos(lon), r * c * sin(lon), r * sin(lat)];
}

function xyz_to_llr(x, y, z) {
  return [atan2(y, x), atan2(z, sqrt(x * x + y * y)), sqrt(x * x + y * y + z * z)];
}

function CD2DP(z, L, fa, gst) {
  let a = [z[0] + Math.PI / 2 - gst - L, z[1], z[2]];
  a = llrConv(a, Math.PI / 2 - fa);
  const azimuth = (-Math.PI / 2 - a[0] + pi2) % pi2;
  return [azimuth, a[1], a[2]];
}

function rspl_lineEar_cone(point_llr, apex, gst) {
  const p = llr2xyz(point_llr);
  const a = llr2xyz([apex.longitude_rad, apex.latitude_rad, apex.radius]);
  const r = lineEll(p[0], p[1], p[2], a[0], a[1], a[2], cs_ba, cs_rEar);
  if (r.D < 0) return { valid: false, longitude_rad: null, latitude_rad: null };
  const fixed_x = r.x * cos(gst) + r.y * sin(gst);
  const fixed_y = -r.x * sin(gst) + r.y * cos(gst);
  return {
    valid: true,
    longitude_rad: atan2(fixed_y, fixed_x),
    latitude_rad: atan(r.z / cs_ba2 / sqrt(r.x * r.x + r.y * r.y))
  };
}

function rspl_zb0(S, M, gast_val) {
  const s_xyz = llr2xyz(S);
  const m_xyz = llr2xyz(M);
  const k_ratio = 959.63 / (cs_k * cs_rEar) * cs_AU;
  const out = { S: S.slice(), M: M.slice(), gast: gast_val, A: {}, B: {} };
  {
    const fx = (s_xyz[0] - m_xyz[0]) / (1 - k_ratio) + m_xyz[0];
    const fy = (s_xyz[1] - m_xyz[1]) / (1 - k_ratio) + m_xyz[1];
    const fz = (s_xyz[2] - m_xyz[2]) / (1 - k_ratio) + m_xyz[2];
    out.A = { longitude_rad: atan2(fy, fx), latitude_rad: atan2(fz, sqrt(fx * fx + fy * fy)), radius: sqrt(fx * fx + fy * fy + fz * fz) };
  }
  {
    const fx = (s_xyz[0] - m_xyz[0]) / (1 + k_ratio) + m_xyz[0];
    const fy = (s_xyz[1] - m_xyz[1]) / (1 + k_ratio) + m_xyz[1];
    const fz = (s_xyz[2] - m_xyz[2]) / (1 + k_ratio) + m_xyz[2];
    out.B = { longitude_rad: atan2(fy, fx), latitude_rad: atan2(fz, sqrt(fx * fx + fy * fy)), radius: sqrt(fx * fx + fy * fy + fz * fz) };
  }
  return out;
}

function push_arr(arr, lon, lat) {
  if (Number.isFinite(lon) && Number.isFinite(lat)) arr.push([lon, lat]);
}

function elmCpy_arr(a, n, b, m) {
  if (!b.length) return;
  let ai = n;
  if (ai === -2) ai = a.length;
  else if (ai === -1) ai = a.length - 1;
  let bi = m;
  if (bi === -2) bi = b.length;
  else if (bi === -1) bi = b.length - 1;
  if (bi < 0 || bi >= b.length) return;
  if (ai < 0 || ai > a.length) return;
  if (ai === a.length) a.push(b[bi].slice());
  else a[ai] = b[bi].slice();
}

function rspl_pp0(state) {
  const p = lineEar(state.M, state.S, state.g);
  if (p.W == 100) return { valid: false, longitude_rad: null, latitude_rad: null };
  return { valid: true, longitude_rad: p.J, latitude_rad: p.W };
}

function rspl_p2p(state, lon0, lat0, useUmbra, side, iterations) {
  let lon = lon0;
  let lat = lat0;
  const out = { valid: false, longitude_rad: null, latitude_rad: null };
  for (let iter = 0; iter < iterations; ++iter) {
    const local = JSON.parse(JSON.stringify(state));
    const topo = rspl_zbXY(local, lon, lat);
    local.S = topo.S_topo;
    local.M = topo.M_topo;
    local.g = topo.g;
    const u = local.M[1] - local.S[1];
    const v = local.M[0] - local.S[0];
    const a = sqrt(u * u + v * v);
    if (a < 1e-14) break;
    const rSun = 959.63 / local.S[2] * cs_AU / cs_rEar / Math.PI * 180.0;
    const W = local.S[1] + side * rSun * v / a;
    const J = local.S[0] - side * rSun * u / a / Math.cos((W + local.S[1]) / 2);
    const R = local.S[2];
    const apex = useUmbra ? state.A : state.B;
    const pt = lineEar([J, W, R], [apex.longitude_rad, apex.latitude_rad, apex.radius], local.g);
    if (pt.W != 100) {
      out.valid = true;
      out.longitude_rad = pt.J;
      out.latitude_rad = pt.W;
      lon = pt.J;
      lat = pt.W;
    } else {
      break;
    }
  }
  return out;
}

function qrd(ctx, jd, dx, dy, fs) {
  const ba2 = ctx.bba * ctx.bba;
  const M = ctx.bseM(jd);
  let x = M[0], y = M[1];
  const B = rSM(M[2], ctx.tanf1, ctx.tanf2, ctx.dyj);
  let r = 0;
  if (fs === 1) r = B.r1;
  const d = 1 - (1 / ba2 - 1) * y * y / (x * x + y * y) * 0.5 + r;
  const t = (d * d - x * x - y * y) / (dx * x + dy * y) * 0.5;
  x += t * dx;
  y += t * dy;
  jd += t;
  const c = (1 - ba2) * r * x * y / (d * d * d);
  x += c * y;
  y -= c * x;
  const I = ctx.bse(jd);
  const re = bse2db([x / d, y / d, 0], I, true);
  return { valid: true, longitude_rad: re[0], latitude_rad: re[1], jd_tt: jd };
}

function rspl_nbj(S, M, gastValue, lon, lat) {
  const state = rspl_zb0(S, M, gastValue);
  state.g = state.gast;
  const out = {};
  out.center = rspl_pp0(state);
  if (out.center.valid) {
    const local = JSON.parse(JSON.stringify(state));
    const topo = rspl_zbXY(local, lon, lat);
    out.center_kind = topo.M_topo[2] >= topo.S_topo[2] ? 2 : 1;
  } else {
    out.center_kind = 0;
  }
  out.umbra_north = rspl_p2p(state, lon, lat, true, 1, 2);
  out.umbra_south = rspl_p2p(state, lon, lat, true, -1, 2);
  out.penumbra_north = rspl_p2p(state, lon, lat, false, -1, 3);
  out.penumbra_south = rspl_p2p(state, lon, lat, false, 1, 3);
  out.umbra_width_km = 0;
  if (out.umbra_north.valid && out.umbra_south.valid) {
    const dlon = out.umbra_north.longitude_rad - out.umbra_south.longitude_rad;
    const dlat = out.umbra_north.latitude_rad - out.umbra_south.latitude_rad;
    const avg = (out.umbra_north.latitude_rad + out.umbra_south.latitude_rad) / 2;
    out.umbra_width_km = cs_rEar * sqrt(dlon * dlon * cos(avg) * cos(avg) + dlat * dlat);
  }
  return out;
}

function jieX2Summary(sample, ctx, jd) {
  const re = { p1: [], p2: [], p3: [] };
  if (abs(jd - ctx.jd_suo_tt) > 0.5) return summaryCurves(re);
  const M = sample.M, B = sample.B, I = sample.I, Z = M[2];
  const a0 = M[0] * M[0] + M[1] * M[1];
  const a1 = a0 - B.r2 * B.r2;
  const a2 = a0 - B.r1 * B.r1;
  const N = 200;
  for (let i = 0; i < N; ++i) {
    const s = i / N * pi2;
    const X = M[0] + ctx.k * cos(s);
    const Y = M[1] + ctx.k * sin(s);
    let x = M[0] + B.r2 * cos(s);
    let y = M[1] + B.r2 * sin(s);
    let p = lineEar2(X, Y, Z, x, y, 0, ctx.earth_axis_ratio, 1, I);
    if (p.W != 100) push_arr(re.p1, p.J, p.W);
    else if (sqrt(x * x + y * y) > a1) {
      const db = bse2db([x, y, 0], I, true);
      push_arr(re.p1, db[0], db[1]);
    }
    x = M[0] + B.r1 * cos(s);
    y = M[1] + B.r1 * sin(s);
    p = lineEar2(X, Y, Z, x, y, 0, ctx.earth_axis_ratio, 1, I);
    if (p.W != 100) push_arr(re.p2, p.J, p.W);
    else if (sqrt(x * x + y * y) > a2) {
      const db = bse2db([x, y, 0], I, true);
      push_arr(re.p2, db[0], db[1]);
    }
    const dawn = llrConv([s, 0, 0], Math.PI / 2 - sample.sun[1]);
    dawn[0] = rad2rrad(dawn[0] + sample.sun[0] + Math.PI / 2 - I[2]);
    push_arr(re.p3, dawn[0], dawn[1]);
  }
  if (re.p1.length) push_arr(re.p1, re.p1[0][0], re.p1[0][1]);
  if (re.p2.length) push_arr(re.p2, re.p2[0][0], re.p2[0][1]);
  if (re.p3.length) push_arr(re.p3, re.p3[0][0], re.p3[0][1]);
  return summaryCurves(re);
}

function summaryCurves(re) {
  function summarize(points) {
    return {
      size: points.length,
      first: points.length ? points[0] : null,
      middle: points.length ? points[Math.floor(points.length / 2)] : null,
      last: points.length ? points[points.length - 1] : null
    };
  }
  return { p1: summarize(re.p1), p2: summarize(re.p2), p3: summarize(re.p3) };
}

const EclipseType = {
  None: 0,
  Partial: 1,
  PartialNoCenter: 2,
  CentralAnnular: 3,
  CentralTotal: 4,
  CentralAnnularTotal: 5,
  CentralAnnularTotalBeginTotal: 6,
  CentralAnnularTotalEndTotal: 7,
  CentralPartial: 8
};

function feature(ctx, jd_suo) {
  const re = { jd_suo_tt: jd_suo, delta_t: ctx.delta_t, ds: ctx.obliquity_rad };
  const tg = 0.04;
  const a = ctx.bseM(jd_suo - tg);
  const b = ctx.bseM(jd_suo);
  const c = ctx.bseM(jd_suo + tg);
  const vx = (c[0] - a[0]) / tg * 0.5;
  const vy = (c[1] - a[1]) / tg * 0.5;
  const vz = (c[2] - a[2]) / tg * 0.5;
  const ax = (c[0] + a[0] - 2 * b[0]) / (tg * tg);
  const ay = (c[1] + a[1] - 2 * b[1]) / (tg * tg);
  const v = sqrt(vx * vx + vy * vy);
  const v2 = v * v;
  re.vx = vx; re.vy = vy; re.ax = ax; re.ay = ay; re.v = v; re.k = abs(vx) > 1e-14 ? vy / vx : 0;
  const t0 = v2 > 1e-28 ? -(b[0] * vx + b[1] * vy) / v2 : 0;
  re.maximum_jd_tt = jd_suo + t0;
  re.xc = b[0] + vx * t0;
  re.yc = b[1] + vy * t0;
  re.zc = b[2] + vz * t0 - 1.37 * t0 * t0;
  re.D = abs(v) > 1e-14 ? (vx * b[1] - vy * b[0]) / v : 0;
  re.d = abs(re.D);
  const Imax = ctx.bse(re.maximum_jd_tt);
  re.I = Imax;
  const F = lineEar2(re.xc, re.yc, 2, re.xc, re.yc, 0, ctx.bba, 1, Imax);
  const Bc = rSM(re.zc, ctx.tanf1, ctx.tanf2, ctx.dyj);
  let Bp = Bc;
  if (F.W != 100) Bp = rSM(re.zc - F.R2, ctx.tanf1, ctx.tanf2, ctx.dyj);
  re.Bc = Bc;
  re.Bp = Bp;
  let B2 = Bc, B3 = Bc, t2 = 0, t3 = 0;
  if (re.d < 1) {
    const dtc = sqrt(Math.max(0, 1 - re.d * re.d)) / v;
    t2 = t0 - dtc;
    t3 = t0 + dtc;
    B2 = rSM(t2 * vz + b[2] - 1.37 * t2 * t2, ctx.tanf1, ctx.tanf2, ctx.dyj);
    B3 = rSM(t3 * vz + b[2] - 1.37 * t3 * t3, ctx.tanf1, ctx.tanf2, ctx.dyj);
  }
  const ls = 1;
  let dtp = 0;
  if (re.d < ls) dtp = sqrt(Math.max(0, ls * ls - re.d * re.d)) / v;
  const t4 = t0 - dtp;
  const t5 = t0 + dtp;
  const t6 = abs(vx) > 1e-14 ? -b[0] / vx : 0;
  if (re.d < 1) {
    re.gk1 = qrd(ctx, t2 + jd_suo, vx, vy, 0);
    re.gk2 = qrd(ctx, t3 + jd_suo, vx, vy, 0);
  }
  re.gk3 = qrd(ctx, t4 + jd_suo, vx, vy, 1);
  re.gk4 = qrd(ctx, t5 + jd_suo, vx, vy, 1);
  const Inoon = ctx.bse(t6 + jd_suo);
  const gp5 = bseXY2db(t6 * vx + b[0], t6 * vy + b[1], Inoon, true);
  re.gk5 = { valid: true, longitude_rad: gp5[0], latitude_rad: gp5[1], jd_tt: t6 + jd_suo };
  re.center_valid = F.W != 100;
  if (!re.center_valid) {
    const ls_pt = bse2db([re.xc, re.yc, 0], Imax, false);
    re.center_longitude_rad = ls_pt[0];
    re.center_latitude_rad = ls_pt[1];
    re.magnitude = (Bc.r1 - (re.d - 0.9972)) / (Bc.r1 - Bc.r2);
    if (re.d > 0.9972 + Bc.r1) re.type = EclipseType.None;
    else if (re.d > 0.9972 + Bc.ar2) re.type = EclipseType.Partial;
    else re.type = Bp.sf < 1 ? EclipseType.PartialNoCenter : EclipseType.CentralAnnularTotal;
  } else {
    re.center_longitude_rad = F.J;
    re.center_latitude_rad = F.W;
    re.center_r2 = F.R2;
    re.magnitude = Bp.sf;
    if (re.d > 0.9966 - Bp.ar2) re.type = EclipseType.CentralPartial;
    else if (Bp.sf >= 1) {
      re.type = EclipseType.CentralAnnularTotal;
      if (B2.sf > 1 && B3.sf > 1) re.type = EclipseType.CentralTotal;
      else if (B2.sf > 1) re.type = EclipseType.CentralAnnularTotalBeginTotal;
      else if (B3.sf > 1) re.type = EclipseType.CentralAnnularTotalEndTotal;
    } else re.type = EclipseType.CentralAnnular;
  }
  const sun = ctx.sun ? ctx.sun(re.maximum_jd_tt) : [Imax[0], Imax[1], 1];
  const sdp = CD2DP(sun, re.center_longitude_rad, re.center_latitude_rad, Imax[2]);
  re.sun_azimuth_rad = sdp[0];
  re.sun_altitude_rad = sdp[1];
  re.path_width_km = 0;
  re.duration_seconds = 0;
  if (re.center_valid) {
    const sinAlt = sin(re.sun_altitude_rad);
    if (abs(sinAlt) > 1e-12) re.path_width_km = abs(2 * Bp.r2 * cs_rEar) / sinAlt;
    const sv = Vxy(re.xc, re.yc, Imax[1], re.vx, re.vy);
    if (sv.V > 1e-14) re.duration_seconds = 2 * abs(Bp.r2) / sv.V * 86400;
  }
  return re;
}

function featureSummary(re) {
  return {
    maximum_jd_tt: re.maximum_jd_tt,
    vx: re.vx,
    vy: re.vy,
    ax: re.ax,
    ay: re.ay,
    v: re.v,
    k: re.k,
    xc: re.xc,
    yc: re.yc,
    zc: re.zc,
    D: re.D,
    d: re.d,
    center_valid: re.center_valid,
    center_longitude_rad: re.center_longitude_rad,
    center_latitude_rad: re.center_latitude_rad,
    center_r2: finiteOrNull(re.center_r2),
    Bc: re.Bc,
    Bp: re.Bp,
    gk1: re.gk1 || null,
    gk2: re.gk2 || null,
    gk3: re.gk3,
    gk4: re.gk4,
    gk5: re.gk5,
    magnitude: re.magnitude,
    type: re.type,
    sun_azimuth_rad: re.sun_azimuth_rad,
    sun_altitude_rad: re.sun_altitude_rad,
    path_width_km: re.path_width_km,
    duration_seconds: re.duration_seconds
  };
}

function emptyJieXSummary() {
  const empty = { size: 0, first: null, middle: null, last: null };
  return {
    p1: empty, p2: empty, p3: empty, p4: empty,
    q1: empty, q2: empty, q3: empty, q4: empty,
    L0: empty, L1: empty, L2: empty, L3: empty, L4: empty, L5: empty, L6: empty
  };
}

function summarizeCurveList(points) {
  return {
    size: points.length,
    first: points.length ? points[0] : null,
    middle: points.length ? points[Math.floor(points.length / 2)] : null,
    last: points.length ? points[points.length - 1] : null
  };
}

function summarizeJieXResult(re) {
  return {
    p1: summarizeCurveList(re.p1), p2: summarizeCurveList(re.p2), p3: summarizeCurveList(re.p3), p4: summarizeCurveList(re.p4),
    q1: summarizeCurveList(re.q1), q2: summarizeCurveList(re.q2), q3: summarizeCurveList(re.q3), q4: summarizeCurveList(re.q4),
    L0: summarizeCurveList(re.L0), L1: summarizeCurveList(re.L1), L2: summarizeCurveList(re.L2), L3: summarizeCurveList(re.L3),
    L4: summarizeCurveList(re.L4), L5: summarizeCurveList(re.L5), L6: summarizeCurveList(re.L6)
  };
}

function elmCpyCurve(a, n, b, m) {
  elmCpy_arr(a, n, b, m);
}

function jieXSummary(ctx, feat) {
  const re = {
    p1: [], p2: [], p3: [], p4: [],
    q1: [], q2: [], q3: [], q4: [],
    L0: [], L1: [], L2: [], L3: [], L4: [], L5: [], L6: []
  };
  if (!ctx.sample || !(feat.v > 0)) return summarizeJieXResult(re);
  let T = 1.7 * 1.7 - feat.d * feat.d;
  if (T < 0) T = 0;
  T = sqrt(T) / feat.v + 0.01;
  let t = feat.maximum_jd_tt - T;
  const N = 400;
  const dt = 2 * T / N;
  let n1 = 0, n4 = 0;
  let Ua = re.q1, Ub = re.q2;
  const L1s = { f2: 0, f: 0 }, L2s = { f2: 0, f: 0 }, L3s = { f2: 0, f: 0 };
  const L4s = { f2: 0, f: 0 }, L5s = { f2: 0, f: 0 }, L6s = { f2: 0, f: 0 };
  push_arr(re.q2, 0, 0);
  push_arr(re.q3, 0, 0);
  push_arr(re.q4, 0, 0);
  for (let i = 0; i <= N; ++i, t += dt) {
    const sample = ctx.sample(t);
    const vx = feat.vx + feat.ax * (t - feat.jd_suo_tt);
    const vy = feat.vy + feat.ay * (t - feat.jd_suo_tt);
    const M = sample.M, B = sample.B, I = sample.I, r = B.r1;
    const p = cirOvl(1, ctx.bba, r, M[0], M[1]);
    if (n1 % 2) { if (!p.n) ++n1; }
    else { if (p.n) ++n1; }
    if (p.n) {
      const a = bse2db([p.A[0], p.A[1], 0], I, true);
      const b = bse2db([p.B[0], p.B[1], 0], I, true);
      if (n1 === 1) { push_arr(re.p1, a[0], a[1]); push_arr(re.p2, b[0], b[1]); }
      if (n1 === 3) { push_arr(re.p3, a[0], a[1]); push_arr(re.p4, b[0], b[1]); }
    }
    let md = mDian(M, vx, vy, false, r, I, ctx.bba);
    if (!md.found) { if (Ua.length) Ua = re.q3; }
    else push_arr(Ua, md.value[0], md.value[1]);
    md = mDian(M, vx, vy, true, r, I, ctx.bba);
    if (!md.found) { if (Ub.length > 1) Ub = re.q4; }
    else push_arr(Ub, md.value[0], md.value[1]);
    if (t > feat.maximum_jd_tt) {
      if (!Ua.length) Ua = re.q3;
      if (Ub.length === 1) Ub = re.q4;
    }
    const center = bseXY2db(M[0], M[1], I, true);
    const centerValid = center[1] !== 100;
    if ((centerValid && n4 === 0) || (!centerValid && n4 === 1)) {
      const ls0 = lineOvl(M[0], M[1], vx, vy, 1, ctx.bba);
      if (ls0.n) {
        let dj, lx, ly;
        if (n4 === 0) { dj = ls0.R2; lx = ls0.B[0]; ly = ls0.B[1]; }
        else { dj = ls0.R1; lx = ls0.A[0]; ly = ls0.A[1]; }
        const vmag = sqrt(vx * vx + vy * vy);
        if (vmag > 1e-14) {
          const I2 = [I[0], I[1], I[2] - dj / vmag * 6.28];
          const gp = bse2db([lx, ly, 0], I2, true);
          push_arr(re.L0, gp[0], gp[1]);
        }
      }
      ++n4;
    }
    if (centerValid) push_arr(re.L0, center[0], center[1]);
    let mq = mQie(M, vx, vy, +1, r, I, ctx.bba, L1s);
    for (let j = 0; j < mq.pushed.length; j += 2) push_arr(re.L1, mq.pushed[j], mq.pushed[j + 1]);
    mq = mQie(M, vx, vy, -1, r, I, ctx.bba, L2s);
    for (let j = 0; j < mq.pushed.length; j += 2) push_arr(re.L2, mq.pushed[j], mq.pushed[j + 1]);
    mq = mQie(M, vx, vy, +1, B.r2, I, ctx.bba, L3s);
    for (let j = 0; j < mq.pushed.length; j += 2) push_arr(re.L3, mq.pushed[j], mq.pushed[j + 1]);
    mq = mQie(M, vx, vy, -1, B.r2, I, ctx.bba, L4s);
    for (let j = 0; j < mq.pushed.length; j += 2) push_arr(re.L4, mq.pushed[j], mq.pushed[j + 1]);
    mq = mQie(M, vx, vy, +1, (r + B.r2) / 2, I, ctx.bba, L5s);
    for (let j = 0; j < mq.pushed.length; j += 2) push_arr(re.L5, mq.pushed[j], mq.pushed[j + 1]);
    mq = mQie(M, vx, vy, -1, (r + B.r2) / 2, I, ctx.bba, L6s);
    for (let j = 0; j < mq.pushed.length; j += 2) push_arr(re.L6, mq.pushed[j], mq.pushed[j + 1]);
  }
  elmCpyCurve(re.q3, 0, re.q1, -1);
  elmCpyCurve(re.q4, 0, re.q2, -1);
  elmCpyCurve(re.q1, -2, re.L1, 0);
  elmCpyCurve(re.q2, -2, re.L2, 0);
  elmCpyCurve(re.q3, 0, re.L1, -1);
  elmCpyCurve(re.q4, 0, re.L2, -1);
  elmCpyCurve(re.q2, 0, re.q1, 0);
  elmCpyCurve(re.q3, -2, re.q4, -1);
  return summarizeJieXResult(re);
}

const fixtures = {
  constants: { cs_rEar, cs_ba, cs_ba2, cs_AU, cs_k, cs_k2, cs_k0, rad, pi2 },
  lineEll: [
    lineEllFixture([0.2, -0.1, 2.0, 0.2, -0.1, -2.0, cs_ba, 1.0]),
    lineEllFixture([1.7, 0.2, 0.1, -1.2, 0.3, 0.5, cs_ba, 1.0]),
    lineEllFixture([2.0, 2.0, 2.0, 3.0, 2.5, 2.2, cs_ba, 1.0])
  ],
  lineEar2: [
    (() => { const args = [0.15, -0.21, 2.0, 0.15, -0.21, 0.0, cs_ba, 1.0, [1.2, 0.41, 0.8]]; const p = lineEar2(...args); return { args, D: p.D, J: p.J, W: p.W, R1: finiteOrNull(p.R1), R2: finiteOrNull(p.R2) }; })(),
    (() => { const args = [1.4, 1.2, 2.0, 1.4, 1.2, 0.0, cs_ba, 1.0, [-2.2, -0.33, 0.1]]; const p = lineEar2(...args); return { args, D: p.D, J: p.J, W: p.W, R1: finiteOrNull(p.R1), R2: finiteOrNull(p.R2) }; })()
  ],
  lineEar: [
    (() => { const args = [[1.105, 0.118, 384400.0], [1.1, 0.12, cs_AU * 1.001], 1.4]; const p = lineEar(...args); return { args, D: p.D, J: p.J, W: p.W, R1: finiteOrNull(p.R1), R2: finiteOrNull(p.R2) }; })(),
    (() => { const args = [[0.2, 0.1, 1000.0], [2.7, -0.2, 1000.0], 0.3]; const p = lineEar(...args); return { args, D: p.D, J: p.J, W: p.W, R1: finiteOrNull(p.R1), R2: finiteOrNull(p.R2) }; })()
  ],
  cirOvl: [
    (() => { const args = [1.0, cs_ba, 0.35, 0.8, 0.15]; return { args, ...cirOvl(...args) }; })(),
    (() => { const args = [1.0, cs_ba, 0.25, 1.8, 0.1]; return { args, ...cirOvl(...args) }; })()
  ],
  lineOvl: [
    (() => { const args = [0.2, -0.3, 0.6, 0.9, 1.0, cs_ba]; return { args, ...lineOvl(...args) }; })(),
    (() => { const args = [1.5, 1.2, 0.1, 0.2, 1.0, cs_ba]; return { args, ...lineOvl(...args) }; })()
  ],
  bse2db: [
    (() => { const args = [[0.22, -0.31, 0.93], [1.2, 0.41, 0.8], true]; const r = bse2db(...args); return { args, value: r }; })(),
    (() => { const args = [[-0.62, 0.11, 0.78], [-2.2, -0.33, 0.1], false]; const r = bse2db(...args); return { args, value: r }; })()
  ],
  bseXY2db: [
    (() => { const args = [0.15, -0.21, [1.2, 0.41, 0.8], true]; const r = bseXY2db(...args); return { args, value: r }; })(),
    (() => { const args = [1.4, 1.2, [-2.2, -0.33, 0.1], true]; const r = bseXY2db(...args); return { args, value: r }; })()
  ],
  Vxy: [
    (() => { const args = [0.15, -0.21, 0.41, 0.02, -0.03]; return { args, ...Vxy(...args) }; })(),
    (() => { const args = [1.2, 0.9, -0.33, -0.2, 0.7]; return { args, ...Vxy(...args) }; })()
  ],
  rSM: [
    (() => { const args = [57.2, 0.0048, 0.0046, 23400.0]; return { args, ...rSM(...args) }; })(),
    (() => { const args = [61.5, 0.0047, 0.0045, 23100.0]; return { args, ...rSM(...args) }; })()
  ],
  llrConv: [
    (() => { const args = [[1.2, -0.4, 2.5], 0.4091]; return { args, value: llrConv(...args) }; })(),
    (() => { const args = [[-2.5, 0.2, 1.0], -0.21]; return { args, value: llrConv(...args) }; })()
  ],
  rspl_zbXY: [
    (() => { const P = { g: 1.4, S: [1.1, 0.12, cs_AU * 1.001], M: [1.105, 0.118, 384400.0] }; const args = [P, -1.3, 0.5]; return { args, value: rspl_zbXY(...args) }; })(),
    (() => { const P = { g: 3.2, S: [-2.4, -0.05, cs_AU * 0.99], M: [-2.392, -0.041, 405000.0] }; const args = [P, 2.1, -0.6]; return { args, value: rspl_zbXY(...args) }; })()
  ],
  nanbei: [
    (() => { const args = [[0.18, -0.12, 58.0], 0.42, -0.18, 1, 0.0092, [1.2, 0.41, 0.8], 0.99664719]; const r = nanbei(...args); return { args, value: r }; })(),
    (() => { const args = [[0.18, -0.12, 58.0], 0.42, -0.18, -1, 0.0092, [1.2, 0.41, 0.8], 0.99664719]; const r = nanbei(...args); return { args, value: r }; })(),
    (() => { const args = [[1.4, 1.2, 58.0], 0.42, -0.18, 1, 0.0092, [-2.2, -0.33, 0.1], 0.99664719]; const r = nanbei(...args); return { args, value: r }; })()
  ],
  mDian: [
    (() => { const args = [[0.18, -0.12, 58.0], 0.42, -0.18, true, 2.0, [1.2, 0.41, 0.8], 0.99664719]; const r = mDian(...args); return { args, value: r }; })(),
    (() => { const args = [[0.18, -0.12, 58.0], 0.42, -0.18, false, 0.1, [1.2, 0.41, 0.8], 0.99664719]; const r = mDian(...args); return { args, value: r }; })()
  ],
  mQie: [
    (() => { const A = { f2: 0, f: 0 }; const args = [[0.18, -0.12, 58.0], 0.42, -0.18, 1, 0.0092, [1.2, 0.41, 0.8], 0.99664719, A]; const r = mQie(...args); return { args: args.slice(0, 7).concat([{ f2: 0, f: 0 }]), value: r }; })(),
    (() => { const A = { f2: 1, f: 1 }; const args = [[1.4, 1.2, 58.0], 0.42, -0.18, 1, 0.0092, [-2.2, -0.33, 0.1], 0.99664719, A]; const r = mQie(...args); return { args: args.slice(0, 7).concat([{ f2: 1, f: 1 }]), value: r }; })()
  ],
  jieX3: [
    (() => { const args = [[0.18, -0.12, 58.0], 0.42, -0.18, { r1: 0.5470676, r2: 0.009160999999999975 }, [1.2, 0.41, 0.8], 0.99664719]; const r = jieX3(...args); return { args, value: r }; })(),
    (() => { const args = [[1.4, 1.2, 58.0], 0.42, -0.18, { r1: 0.5470676, r2: 0.009160999999999975 }, [-2.2, -0.33, 0.1], 0.99664719]; const r = jieX3(...args); return { args, value: r }; })()
  ],
  lineT_contact: [
    (() => { const G = { x: 0.15, y: -0.08, t: 2460409.25 }; return { args: [G, 0.42, -0.18, 0.55, 0], value: lineT_contact(G, 0.42, -0.18, 0.55, 0) }; })(),
    (() => { const G = { x: 0.15, y: -0.08, t: 2460409.25 }; return { args: [G, 0.42, -0.18, 0.55, 1], value: lineT_contact(G, 0.42, -0.18, 0.55, 1) }; })(),
    (() => { const G = { x: 0.15, y: -0.08, t: 2460409.25 }; return { args: [G, 0.42, -0.18, 0.01, 0], value: lineT_contact(G, 0.42, -0.18, 0.01, 0) }; })()
  ],
  rspl_pp0: [
    (() => { const state = { g: 1.4, S: [1.1, 0.12, cs_AU * 1.001], M: [1.105, 0.118, 384400.0] }; return { args: [state], value: rspl_pp0(state) }; })(),
    (() => { const state = { g: 3.2, S: [-2.4, -0.05, cs_AU * 0.99], M: [-2.392, -0.041, 405000.0] }; return { args: [state], value: rspl_pp0(state) }; })()
  ],
  rspl_p2p: [
    (() => {
      const state = {
        g: 1.4, S: [1.1, 0.12, cs_AU * 1.001], M: [1.105, 0.118, 384400.0],
        A: { longitude_rad: 1.102, latitude_rad: 0.119, radius: cs_AU * 1.5 },
        B: { longitude_rad: 1.098, latitude_rad: 0.121, radius: cs_AU * 0.8 }
      };
      return { args: [state, -1.3, 0.5, true, 1, 2], value: rspl_p2p(state, -1.3, 0.5, true, 1, 2) };
    })(),
    (() => {
      const state = {
        g: 1.4, S: [1.1, 0.12, cs_AU * 1.001], M: [1.105, 0.118, 384400.0],
        A: { longitude_rad: 1.102, latitude_rad: 0.119, radius: cs_AU * 1.5 },
        B: { longitude_rad: 1.098, latitude_rad: 0.121, radius: cs_AU * 0.8 }
      };
      return { args: [state, -1.3, 0.5, false, -1, 3], value: rspl_p2p(state, -1.3, 0.5, false, -1, 3) };
    })()
  ],
  rad2rrad: [
    { args: [0.0], value: rad2rrad(0.0) },
    { args: [3.5], value: rad2rrad(3.5) },
    { args: [-2.0], value: rad2rrad(-2.0) },
    { args: [7.0], value: rad2rrad(7.0) }
  ],
  llr_to_xyz: [
    { args: [1.2, -0.4, 2.5], value: llr_to_xyz(1.2, -0.4, 2.5) },
    { args: [-2.5, 0.2, 1.0], value: llr_to_xyz(-2.5, 0.2, 1.0) },
    { args: [0.0, 0.0, 1.0], value: llr_to_xyz(0.0, 0.0, 1.0) }
  ],
  xyz_to_llr: [
    { args: [0.5, -0.3, 2.1], value: xyz_to_llr(0.5, -0.3, 2.1) },
    { args: [-1.0, 0.0, 0.0], value: xyz_to_llr(-1.0, 0.0, 0.0) },
    { args: [0.0, 0.0, 0.0], value: xyz_to_llr(0.0, 0.0, 0.0) }
  ],
  CD2DP: [
    { args: [[1.5, 0.3, 1.0], 0.5, 0.7, 2.1], value: CD2DP([1.5, 0.3, 1.0], 0.5, 0.7, 2.1) },
    { args: [[-0.8, -0.1, 1.0], -1.2, 0.4, 0.3], value: CD2DP([-0.8, -0.1, 1.0], -1.2, 0.4, 0.3) }
  ],
  rspl_lineEar_cone: [
    (() => {
      const point = [1.105, 0.118, 384400.0];
      const apex = { longitude_rad: 1.102, latitude_rad: 0.119, radius: cs_AU * 1.5 };
      return { args: [point, apex, 1.4], value: rspl_lineEar_cone(point, apex, 1.4) };
    })(),
    (() => {
      const point = [0.2, 0.1, 1000.0];
      const apex = { longitude_rad: 2.7, latitude_rad: -0.2, radius: 1000.0 };
      return { args: [point, apex, 0.3], value: rspl_lineEar_cone(point, apex, 0.3) };
    })()
  ],
  rspl_zb0: [
    (() => {
      const S = [1.1, 0.12, cs_AU * 1.001];
      const M = [1.105, 0.118, 384400.0];
      return { args: [S, M, 1.4], value: rspl_zb0(S, M, 1.4) };
    })(),
    (() => {
      const S = [-2.4, -0.05, cs_AU * 0.99];
      const M = [-2.392, -0.041, 405000.0];
      return { args: [S, M, 3.2], value: rspl_zb0(S, M, 3.2) };
    })()
  ],
  push_elmCpy: [
    (() => {
      const arr1 = [];
      push_arr(arr1, 1.0, 2.0);
      push_arr(arr1, 3.0, 4.0);
      push_arr(arr1, NaN, 5.0);
      const arr2 = [[10.0, 20.0]];
      elmCpy_arr(arr1, 0, arr2, 1);
      elmCpy_arr(arr1, -1, arr2, 0);
      return { args: [], value: { arr1, arr2 } };
    })(),
    (() => {
      const arr1 = [[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]];
      const arr2 = [];
      elmCpy_arr(arr1, -2, arr2, 0);
      elmCpy_arr(arr1, 1, arr2, 1);
      return { args: [], value: { arr1, arr2 } };
    })()
  ],
  qrd: [
    (() => {
      const ctx = {
        bba: cs_ba,
        tanf1: 0.0048,
        tanf2: 0.0046,
        dyj: 23400.0,
        bseM: jd => [0.18 + 0.42 * (jd - 2460409.25), -0.12 - 0.18 * (jd - 2460409.25), 58.0],
        bse: jd => [1.2 + 0.01 * (jd - 2460409.25), 0.41 - 0.02 * (jd - 2460409.25), 0.8 + 0.03 * (jd - 2460409.25)]
      };
      return { args: [2460409.25, 0.42, -0.18, 1], value: qrd(ctx, 2460409.25, 0.42, -0.18, 1) };
    })(),
    (() => {
      const ctx = {
        bba: cs_ba,
        tanf1: 0.0048,
        tanf2: 0.0046,
        dyj: 23400.0,
        bseM: jd => [0.18 + 0.42 * (jd - 2460409.25), -0.12 - 0.18 * (jd - 2460409.25), 58.0],
        bse: jd => [1.2 + 0.01 * (jd - 2460409.25), 0.41 - 0.02 * (jd - 2460409.25), 0.8 + 0.03 * (jd - 2460409.25)]
      };
      return { args: [2460409.3, 0.42, -0.18, 0], value: qrd(ctx, 2460409.3, 0.42, -0.18, 0) };
    })()
  ],
  rspl_nbj: [
    (() => {
      const S = [1.1, 0.12, cs_AU * 1.001];
      const M = [1.105, 0.118, 384400.0];
      return { args: [S, M, 1.4, -1.3, 0.5], value: rspl_nbj(S, M, 1.4, -1.3, 0.5) };
    })(),
    (() => {
      const S = [-2.4, -0.05, cs_AU * 0.99];
      const M = [-2.392, -0.041, 405000.0];
      return { args: [S, M, 3.2, 2.1, -0.6], value: rspl_nbj(S, M, 3.2, 2.1, -0.6) };
    })()
  ],
  jieX2: [
    (() => {
      const sample = { M: [0.18, -0.12, 58.0], B: { r1: 0.5470676, r2: 0.009160999999999975 }, I: [1.2, 0.41, 0.8], sun: [1.05, 0.12, 1.0] };
      const ctx = { k: cs_k, earth_axis_ratio: cs_ba, jd_suo_tt: 2460409.25 };
      return { args: [2460409.25], value: jieX2Summary(sample, ctx, 2460409.25) };
    })(),
    (() => {
      const sample = { M: [0.18, -0.12, 58.0], B: { r1: 0.5470676, r2: 0.009160999999999975 }, I: [1.2, 0.41, 0.8], sun: [1.05, 0.12, 1.0] };
      const ctx = { k: cs_k, earth_axis_ratio: cs_ba, jd_suo_tt: 2460409.25 };
      return { args: [2460410.0], value: jieX2Summary(sample, ctx, 2460410.0) };
    })()
  ],
  feature: [
    (() => {
      const ctx = {
        bba: cs_ba,
        delta_t: 69.0 / 86400.0,
        obliquity_rad: 0.4091,
        tanf1: 0.0048,
        tanf2: 0.0046,
        dyj: 23400.0,
        bseM: jd => {
          const t = jd - 2460409.25;
          return [0.18 + 0.42 * t + 0.015 * t * t, -0.12 - 0.18 * t + 0.01 * t * t, 58.0 + 0.2 * t];
        },
        bse: jd => [1.2 + 0.01 * (jd - 2460409.25), 0.41 - 0.02 * (jd - 2460409.25), 0.8 + 0.03 * (jd - 2460409.25)],
        sun: jd => [1.05 + 0.02 * (jd - 2460409.25), 0.12 - 0.01 * (jd - 2460409.25), 1.0]
      };
      return { args: [2460409.25], value: featureSummary(feature(ctx, 2460409.25)) };
    })()
  ],
  jieX_guard: [
    { args: ["no sample callback"], value: emptyJieXSummary() },
    { args: ["zero velocity"], value: emptyJieXSummary() }
  ],
  jieX: [
    (() => {
      const feat = feature({
        bba: cs_ba,
        delta_t: 69.0 / 86400.0,
        obliquity_rad: 0.4091,
        tanf1: 0.0048,
        tanf2: 0.0046,
        dyj: 23400.0,
        bseM: jd => {
          const t = jd - 2460409.25;
          return [0.18 + 0.42 * t + 0.015 * t * t, -0.12 - 0.18 * t + 0.01 * t * t, 58.0 + 0.2 * t];
        },
        bse: jd => [1.2 + 0.01 * (jd - 2460409.25), 0.41 - 0.02 * (jd - 2460409.25), 0.8 + 0.03 * (jd - 2460409.25)],
        sun: jd => [1.05 + 0.02 * (jd - 2460409.25), 0.12 - 0.01 * (jd - 2460409.25), 1.0]
      }, 2460409.25);
      const ctx = {
        bba: cs_ba,
        k: cs_k,
        earth_axis_ratio: cs_ba,
        sample: jd => {
          const t = jd - 2460409.25;
          const M = [0.18 + 0.42 * t + 0.015 * t * t, -0.12 - 0.18 * t + 0.01 * t * t, 58.0 + 0.2 * t];
          return { M, B: rSM(M[2], 0.0048, 0.0046, 23400.0), I: [1.2 + 0.01 * t, 0.41 - 0.02 * t, 0.8 + 0.03 * t] };
        }
      };
      return { args: [2460409.25], value: jieXSummary(ctx, feat) };
    })()
  ]
};

process.stdout.write(JSON.stringify(fixtures, null, 2));
process.stdout.write('\n');
