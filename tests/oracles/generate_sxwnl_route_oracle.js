#!/usr/bin/env node

const fs = require("fs");
const path = require("path");
const vm = require("vm");

function usage() {
  process.stderr.write(
    "usage: generate_sxwnl_route_oracle.js SXWNL_SRC_ROOT [OUTPUT_JSON]\n"
  );
}

if (process.argv.length < 3 || process.argv.length > 4) {
  usage();
  process.exit(2);
}

const sourceRoot = path.resolve(process.argv[2]);
for (const filename of ["tools.js", "eph0.js", "eph.js"]) {
  const sourcePath = path.join(sourceRoot, filename);
  vm.runInThisContext(fs.readFileSync(sourcePath, "utf8"), {
    filename: sourcePath,
  });
}

const jd = JD.JD(2026, 2, 17) - J2000;
rsGS.init(jd, 7);
const result = rsGS.jieX(jd);
const curveNames = [
  "p1", "p2", "p3", "p4",
  "q1", "q2", "q3", "q4",
  "L0", "L1", "L2", "L3", "L4", "L5", "L6",
];

function serializeCurve(flat) {
  const points = [];
  for (let index = 0; index + 1 < flat.length; index += 2) {
    points.push({
      longitude_rad: flat[index],
      latitude_rad: flat[index + 1],
    });
  }
  return points;
}

const document = {
  schema: "taiyin.sxwnl-route-oracle.v1",
  source: {
    implementation: "sxwnl eph.js rsGS.jieX",
    files: ["tools.js", "eph0.js", "eph.js"],
  },
  event: {
    calendar_date: "2026-02-17",
    jd_offset_from_j2000: jd,
    sample_count: 400,
  },
  curves: {},
};
for (const name of curveNames) {
  document.curves[name] = serializeCurve(result[name]);
}

const json = JSON.stringify(document, null, 2) + "\n";
if (process.argv[3]) {
  fs.writeFileSync(path.resolve(process.argv[3]), json);
} else {
  process.stdout.write(json);
}
