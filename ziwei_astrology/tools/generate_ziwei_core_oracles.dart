// Generates the committed numeric differential corpora from the author's
// MIT-licensed ziwei_core oracle. Run from the oracle checkout with:
//
//   dart --packages=.dart_tool/package_config.json \
//     /path/to/generate_ziwei_core_oracles.dart natal
//   dart --packages=.dart_tool/package_config.json \
//     /path/to/generate_ziwei_core_oracles.dart limits
//   dart --packages=.dart_tool/package_config.json \
//     /path/to/generate_ziwei_core_oracles.dart exhaustive MODE 1984 2043
//   dart --packages=.dart_tool/package_config.json \
//     /path/to/generate_ziwei_core_oracles.dart finite MODE

import 'dart:io';

import 'package:ziwei_core/ziwei_core.dart';

class OracleCase {
  final AstroDateTime clock;
  final Gender gender;

  const OracleCase(this.clock, this.gender);
}

// Bit 0..3 are birth-year Lu/Quan/Ke/Ji; bit 4..7 are centrifugal/self; bit
// 8..11 are centripetal. This mirrors NatalChart::transformations.marks_by_star
// in the C++ producer while keeping the streaming corpus label-free.
List<int> palaceTransformMasks(ZiWeiPlate plate, ZiweiRuleset rules) {
  final masks = <String, int>{};
  for (final palace in plate.palaces) {
    for (final group in palace.stars.values) {
      for (final star in group) {
        if (star is! StaticStar) continue;
        var mask = 0;
        final birthYear = star.siHuaBuff[ZiweiScope.origin];
        if (birthYear != null) {
          mask |= 1 << birthYear.index;
        }
        if (star.selfSiHua != null) {
          mask |= 1 << (4 + star.selfSiHua!.index);
        }
        if (star.centripetalSiHua != null) {
          mask |= 1 << (8 + star.centripetalSiHua!.index);
        }
        masks[star.key] = mask;
      }
    }
  }
  return <int>[for (final star in rules.stars) masks[star.key] ?? 0];
}

class SyntheticLunarDate implements LunarDate {
  @override
  final int lunarYear;
  @override
  final int month;
  @override
  final int day;
  @override
  final bool isLeap;
  @override
  final String monthNameStr;
  @override
  final int monthSize;

  const SyntheticLunarDate(this.lunarYear, this.month, this.day)
    : isLeap = false,
      monthNameStr = '',
      monthSize = 30;

  @override
  AstroDateTime get toSolar =>
      throw UnsupportedError('the finite oracle has no physical solar date');
  @override
  String get dayName => day.toString();
  @override
  bool get isLastDay => day == monthSize;
  @override
  bool get isBCE => lunarYear <= 0;
  @override
  int? get bceYear => isBCE ? 1 - lunarYear : null;
  @override
  int get historicalYear => isBCE ? 1 - lunarYear : lunarYear;
  @override
  String toString() => '$lunarYear-$month-$day';
}

final List<OracleCase> cases = <OracleCase>[
  OracleCase(AstroDateTime(181, 8, 20, 8, 0), Gender.male),
  OracleCase(AstroDateTime(2003, 3, 13, 14, 15), Gender.female),
  OracleCase(AstroDateTime(2023, 3, 25, 10, 30), Gender.male),
  OracleCase(AstroDateTime(-100, 1, 15, 22, 30), Gender.female),
  OracleCase(AstroDateTime(1949, 10, 1, 15, 0), Gender.male),
  OracleCase(AstroDateTime(1984, 2, 4, 23, 30), Gender.male),
  OracleCase(AstroDateTime(1984, 2, 4, 23, 30), Gender.female),
  OracleCase(AstroDateTime(2000, 1, 1, 0, 30), Gender.male),
  OracleCase(AstroDateTime(2000, 1, 1, 23, 30), Gender.female),
  OracleCase(AstroDateTime(2023, 4, 5, 23, 30), Gender.female),
  OracleCase(AstroDateTime(2033, 12, 22, 12, 0), Gender.male),
  OracleCase(AstroDateTime(-720, 1, 15, 12, 0), Gender.male),
  OracleCase(AstroDateTime(-479, 1, 15, 12, 0), Gender.female),
  OracleCase(AstroDateTime(-220, 1, 15, 12, 0), Gender.male),
  OracleCase(AstroDateTime(-104, 1, 15, 12, 0), Gender.female),
  OracleCase(AstroDateTime(237, 1, 15, 12, 0), Gender.male),
  OracleCase(AstroDateTime(690, 1, 15, 12, 0), Gender.female),
  OracleCase(AstroDateTime(701, 1, 15, 12, 0), Gender.male),
  OracleCase(AstroDateTime(762, 1, 15, 12, 0), Gender.female),
  OracleCase(AstroDateTime(1582, 10, 4, 12, 0), Gender.male),
  OracleCase(AstroDateTime(1900, 1, 31, 12, 0), Gender.female),
  OracleCase(AstroDateTime(2100, 2, 4, 12, 0), Gender.male),
  OracleCase(AstroDateTime(2200, 12, 31, 12, 0), Gender.female),
];

ZiweiDate resolveDate(OracleCase value, ZiweiRuleset rules) {
  return ZiweiDate.fromSolar(
    value.clock,
    gender: value.gender,
    options: rules.calendarOptions,
    useTrueSolarTime: false,
  );
}

List<int> ganzhiPair(ZiweiDate date, ZiweiScope scope, Boundary boundary) {
  final value = date.getGanZhi(scope, b: boundary);
  return <int>[value.gan.index, value.zhi.index];
}

void dumpNatal() {
  final rules = ConfigLoader.getDefault();
  stdout.writeln(
    '# Generated from the author\'s MIT-licensed ziwei_core 0.13.0 default rules.',
  );
  stdout.writeln(
    '# Columns: gender,lunar(y,m,d,leap),effective(y,m),solar_day,',
  );
  stdout.writeln(
    '# solar pillars(8),lunar pillars(8),life,body,bureau,115 natal positions.',
  );
  for (final item in cases) {
    final date = resolveDate(item, rules);
    final plate = ZiweiEngine.calculate(date, rules);
    final positions = <String, int>{};
    for (final palace in plate.palaces) {
      for (final group in palace.stars.values) {
        for (final star in group) {
          positions[star.key] = palace.index;
        }
      }
    }
    final values = <int>[
      item.gender.index,
      date.lunar.lunarYear,
      date.lunar.month,
      date.lunar.day,
      date.lunar.isLeap ? 1 : 0,
      plate.effectiveYear,
      plate.effectiveMonth,
      date.solarDay,
      for (final scope in <ZiweiScope>[
        ZiweiScope.year,
        ZiweiScope.month,
        ZiweiScope.day,
        ZiweiScope.hour,
      ])
        ...ganzhiPair(date, scope, Boundary.solar),
      for (final scope in <ZiweiScope>[
        ZiweiScope.year,
        ZiweiScope.month,
        ZiweiScope.day,
        ZiweiScope.hour,
      ])
        ...ganzhiPair(date, scope, Boundary.lunar),
      plate.originMingIndex,
      plate.bodyPalaceIndex,
      plate.elementBureau.index,
      for (final star in rules.stars) positions[star.key] ?? -1,
    ];
    stdout.writeln(values.join(','));
  }
}

void dumpLimits() {
  final rules = ConfigLoader.getDefault();
  stdout.writeln(
    '# Generated from the author\'s MIT-licensed ziwei_core 0.13.0 default rules.',
  );
  stdout.writeln(
    '# case,birth_year,target_year,decade(index,start,end,stem,branch),',
  );
  stdout.writeln('# virtual_age,small(stem,branch),year(stem,branch),');
  stdout.writeln(
    '# month(logical,sequence,leap,stem,branch),day(day,physical_stem,stem,branch),',
  );
  stdout.writeln('# hour(index,stem,branch)');
  for (int index = 0; index < cases.length; ++index) {
    final date = resolveDate(cases[index], rules);
    final plate = ZiweiEngine.calculate(date, rules);
    final targetYear = plate.effectiveYear + 20;
    final decade = Decade.createByYear(targetYear, plate);
    final virtualAge = targetYear - plate.effectiveYear + 1;
    final small = SmallLimit.create(virtualAge, plate);
    final year = FlowYear.createByYear(targetYear, plate);
    final month = FlowMonth.create(
      5,
      targetYear,
      plate,
      sequence: 6,
      isLeap: true,
    );
    final day = FlowDay.create(12, date.bazi.day, month, plate);
    final hour = FlowHour.create(7, day, plate);
    stdout.writeln(
      <int>[
        index,
        plate.effectiveYear,
        targetYear,
        decade.decadeIndex,
        decade.startTime,
        decade.endTime,
        decade.ganzhi.gan.index,
        decade.ganzhi.zhi.index,
        virtualAge,
        small.ganzhi.gan.index,
        small.ganzhi.zhi.index,
        year.ganzhi.gan.index,
        year.ganzhi.zhi.index,
        month.month,
        month.sequence,
        month.isLeap ? 1 : 0,
        month.ganzhi.gan.index,
        month.ganzhi.zhi.index,
        day.day,
        date.bazi.day.gan.index,
        day.ganzhi.gan.index,
        day.ganzhi.zhi.index,
        hour.hourIndex,
        hour.ganzhi.gan.index,
        hour.ganzhi.zhi.index,
      ].join(','),
    );
  }
}

ZiweiRuleset rulesWithRatHourMode(RatHourMode mode) {
  final base = ConfigLoader.getDefault();
  final current = base.calendarOptions;
  return ZiweiRuleset(
    stars: base.stars,
    flowDefinitions: base.flowDefinitions,
    brightnessLabels: base.brightnessLabels,
    siHuaRules: base.siHuaRules,
    mingZhuRule: base.mingZhuRule,
    shenZhuRule: base.shenZhuRule,
    calendarOptions: CalendarOptions(
      ratHourMode: mode,
      leapRule: current.leapRule,
      wuHuDunBasedOn: current.wuHuDunBasedOn,
      siHuaBasedOn: current.siHuaBasedOn,
      childhoodRule: current.childhoodRule,
      flowLimitBasedOn: current.flowLimitBasedOn,
      enableHistorical: current.enableHistorical,
    ),
  );
}

void dumpExhaustive(
  int modeIndex,
  int startYear,
  int endYear, [
  int? maxRecords,
]) {
  if (modeIndex < 0 || modeIndex > 2 || endYear < startYear) {
    throw ArgumentError('invalid exhaustive range or Rat-hour mode');
  }
  final mode = RatHourMode.values[modeIndex];
  final rules = rulesWithRatHourMode(mode);
  final starIds = <String, int>{
    for (int i = 0; i < rules.stars.length; ++i) rules.stars[i].key: i,
  };
  final splitRat = mode != RatHourMode.noSplit;
  final hours = <int>[
    0,
    2,
    4,
    6,
    8,
    10,
    12,
    14,
    16,
    18,
    20,
    22,
    if (splitRat) 23,
  ];
  final start = AstroDateTime(startYear, 1, 1).toJ2000();
  final end = AstroDateTime(endYear + 1, 1, 1).toJ2000();
  stdout.writeln(
    '# taiyin-ziwei-exhaustive-v2 mode=$modeIndex years=$startYear..$endYear',
  );
  int recordCount = 0;
  for (int dayOffset = 0; dayOffset < (end - start).round(); ++dayOffset) {
    final date = AstroDateTime.fromJ2000(start + dayOffset);
    for (final hour in hours) {
      final local = AstroDateTime(
        date.year,
        date.month,
        date.day,
        hour,
        hour == 0 || hour == 23 ? 30 : 0,
      );
      for (final gender in Gender.values) {
        final ziweiDate = ZiweiDate.fromSolar(
          local,
          gender: gender,
          options: rules.calendarOptions,
          useTrueSolarTime: false,
        );
        final plate = ZiweiEngine.calculate(ziweiDate, rules);
        final positions = <String, int>{};
        for (final palace in plate.palaces) {
          for (final group in palace.stars.values) {
            for (final star in group) {
              positions[star.key] = palace.index;
            }
          }
        }
        final values = <int>[
          local.year,
          local.month,
          local.day,
          local.hour,
          local.minute,
          gender.index,
          plate.effectiveYear,
          plate.effectiveMonth,
          for (final scope in <ZiweiScope>[
            ZiweiScope.year,
            ZiweiScope.month,
            ZiweiScope.day,
            ZiweiScope.hour,
          ])
            ...ganzhiPair(ziweiDate, scope, Boundary.solar),
          for (final scope in <ZiweiScope>[
            ZiweiScope.year,
            ZiweiScope.month,
            ZiweiScope.day,
            ZiweiScope.hour,
          ])
            ...ganzhiPair(ziweiDate, scope, Boundary.lunar),
          plate.originMingIndex,
          plate.bodyPalaceIndex,
          plate.elementBureau.index,
          for (final palace in plate.palaces) palace.stem!.index,
          starIds[plate.mingZhu] ?? -1,
          starIds[plate.shenZhu] ?? -1,
          for (final star in rules.stars) positions[star.key] ?? -1,
          ...palaceTransformMasks(plate, rules),
        ];
        stdout.writeln(values.join(','));
        recordCount++;
        if (maxRecords != null && recordCount >= maxRecords) return;
      }
    }
  }
}

GanZhi cycleGanZhi(int index) =>
    GanZhi(TianGan.values[index % 10], DiZhi.values[index % 12]);

void dumpFinite(int modeIndex, [int? maxRecords]) {
  if (modeIndex < 0 || modeIndex > 2) {
    throw ArgumentError('invalid Rat-hour mode');
  }
  final rules = rulesWithRatHourMode(RatHourMode.values[modeIndex]);
  final starIds = <String, int>{
    for (int i = 0; i < rules.stars.length; ++i) rules.stars[i].key: i,
  };
  stdout.writeln('# taiyin-ziwei-finite-v2 mode=$modeIndex');
  int recordCount = 0;
  for (int yearCycle = 0; yearCycle < 60; ++yearCycle) {
    final year = cycleGanZhi(yearCycle);
    for (int month = 1; month <= 12; ++month) {
      final monthStem = ((year.gan.index % 5) * 2 + 2 + month - 1) % 10;
      final monthPillar = GanZhi(
        TianGan.values[monthStem],
        DiZhi.values[(month + 1) % 12],
      );
      for (int day = 1; day <= 30; ++day) {
        final dayPillar = cycleGanZhi(day - 1);
        for (int hour = 0; hour < 12; ++hour) {
          var hourDayStem = dayPillar.gan.index;
          if (hour == 0 && modeIndex == RatHourMode.tomorrowGan.index) {
            hourDayStem = (hourDayStem + 1) % 10;
          }
          final hourPillar = GanZhi(
            TianGan.values[((hourDayStem % 5) * 2 + hour) % 10],
            DiZhi.values[hour],
          );
          for (final gender in Gender.values) {
            final lunarYear = 1984 + yearCycle;
            final date = ZiweiDate(
              solar: AstroDateTime(lunarYear, month, day, hour * 2),
              location: const Location(120, 30),
              lunar: SyntheticLunarDate(lunarYear, month, day),
              bazi: BaZi(
                year: year,
                month: monthPillar,
                day: dayPillar,
                time: hourPillar,
              ),
              options: rules.calendarOptions,
              solarDay: day,
              gender: gender,
              timeZone: 8,
            );
            final plate = ZiweiEngine.calculate(date, rules);
            final positions = <String, int>{};
            for (final palace in plate.palaces) {
              for (final group in palace.stars.values) {
                for (final star in group) {
                  positions[star.key] = palace.index;
                }
              }
            }
            final values = <int>[
              lunarYear,
              month,
              day,
              hour,
              0,
              gender.index,
              plate.effectiveYear,
              plate.effectiveMonth,
              for (final scope in <ZiweiScope>[
                ZiweiScope.year,
                ZiweiScope.month,
                ZiweiScope.day,
                ZiweiScope.hour,
              ])
                ...ganzhiPair(date, scope, Boundary.solar),
              for (final scope in <ZiweiScope>[
                ZiweiScope.year,
                ZiweiScope.month,
                ZiweiScope.day,
                ZiweiScope.hour,
              ])
                ...ganzhiPair(date, scope, Boundary.lunar),
              plate.originMingIndex,
              plate.bodyPalaceIndex,
              plate.elementBureau.index,
              for (final palace in plate.palaces) palace.stem!.index,
              starIds[plate.mingZhu] ?? -1,
              starIds[plate.shenZhu] ?? -1,
              for (final star in rules.stars) positions[star.key] ?? -1,
              ...palaceTransformMasks(plate, rules),
            ];
            stdout.writeln(values.join(','));
            recordCount++;
            if (maxRecords != null && recordCount >= maxRecords) return;
          }
        }
      }
    }
  }
  if (recordCount != 518400) {
    throw StateError('finite oracle emitted $recordCount records');
  }
}

void main(List<String> arguments) {
  final fixedCorpus =
      arguments.length == 1 &&
      <String>{'natal', 'limits'}.contains(arguments[0]);
  final exhaustive =
      (arguments.length == 4 || arguments.length == 5) &&
      arguments[0] == 'exhaustive';
  final finite =
      (arguments.length == 2 || arguments.length == 3) &&
      arguments[0] == 'finite';
  if (!fixedCorpus && !exhaustive && !finite) {
    stderr.writeln(
      'usage: generate_ziwei_core_oracles.dart natal|limits\n'
      '   or: generate_ziwei_core_oracles.dart exhaustive MODE START END\n'
      '   or: generate_ziwei_core_oracles.dart finite MODE [MAX_RECORDS]',
    );
    exitCode = 64;
    return;
  }
  if (finite) {
    dumpFinite(
      int.parse(arguments[1]),
      arguments.length == 3 ? int.parse(arguments[2]) : null,
    );
  } else if (exhaustive) {
    dumpExhaustive(
      int.parse(arguments[1]),
      int.parse(arguments[2]),
      int.parse(arguments[3]),
      arguments.length == 5 ? int.parse(arguments[4]) : null,
    );
  } else {
    arguments[0] == 'natal' ? dumpNatal() : dumpLimits();
  }
}
