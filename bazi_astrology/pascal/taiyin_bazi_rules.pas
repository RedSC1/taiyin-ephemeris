unit taiyin_bazi_rules;

{$mode objfpc}{$H+}

{$IF DEFINED(DARWIN) OR (DEFINED(MSWINDOWS) AND DEFINED(CPUI386))}
  {$DEFINE TAIYIN_BAZI_LEADING_UNDERSCORE}
{$ENDIF}

interface

uses taiyin_ganzhi_rules, taiyin_dynamic_bitset;

function taiyin_bazi_rules_kong_wang(
  value: Byte;
  out_branches: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_kong_wang';
{$ELSE}
  name 'taiyin_bazi_rules_kong_wang';
{$ENDIF}

function taiyin_bazi_rules_ten_god(
  day_stem_id, target_stem_id: Byte;
  out_ten_god_id: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_ten_god';
{$ELSE}
  name 'taiyin_bazi_rules_ten_god';
{$ENDIF}

function taiyin_bazi_rules_stem_relation(
  stem_a, stem_b: Byte;
  out_flags: PLongWord;
  out_combined_element_id: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_stem_relation';
{$ELSE}
  name 'taiyin_bazi_rules_stem_relation';
{$ENDIF}

function taiyin_bazi_rules_branch_relation(
  branch_a, branch_b: Byte;
  out_flags: PLongWord;
  out_combined_element_id: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_branch_relation';
{$ELSE}
  name 'taiyin_bazi_rules_branch_relation';
{$ENDIF}

function taiyin_bazi_rules_branch_triple_relation(
  branch_a, branch_b, branch_c: Byte;
  out_flags: PLongWord;
  out_combined_element_id: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_branch_triple_relation';
{$ELSE}
  name 'taiyin_bazi_rules_branch_triple_relation';
{$ENDIF}

function taiyin_bazi_rules_collect_relations(
  pillars: PByte;
  pillar_mask, relation_mask: LongWord;
  out_relations: Pointer;
  capacity: PtrUInt;
  out_count: PPtrUInt
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_collect_relations';
{$ELSE}
  name 'taiyin_bazi_rules_collect_relations';
{$ENDIF}

function taiyin_bazi_rules_life_stage(
  stem_id, branch_id: Byte;
  earth_palace_mode: LongInt;
  out_life_stage_id: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_life_stage';
{$ELSE}
  name 'taiyin_bazi_rules_life_stage';
{$ENDIF}

function taiyin_bazi_rules_hidden_stems(
  branch_id: Byte;
  out_stems: PByte;
  out_count: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_hidden_stems';
{$ELSE}
  name 'taiyin_bazi_rules_hidden_stems';
{$ENDIF}

function taiyin_bazi_rules_extra_pillars(
  year_pillar, month_pillar, day_pillar, hour_pillar: Byte;
  out_ming_gong, out_shen_gong, out_tai_yuan, out_tai_xi: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_extra_pillars';
{$ELSE}
  name 'taiyin_bazi_rules_extra_pillars';
{$ENDIF}

function taiyin_bazi_rules_qiyun_direction(
  year_pillar: Byte;
  gender, direction_mode: LongInt;
  out_direction: PLongInt
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_qiyun_direction';
{$ELSE}
  name 'taiyin_bazi_rules_qiyun_direction';
{$ENDIF}

function taiyin_bazi_rules_dayun_ganzhi(
  month_pillar: Byte;
  direction: LongInt;
  one_based_index: LongWord;
  out_ganzhi: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_dayun_ganzhi';
{$ELSE}
  name 'taiyin_bazi_rules_dayun_ganzhi';
{$ENDIF}

function taiyin_bazi_rules_siling_segment(
  table_model: LongInt;
  month_branch_id, segment_index: Byte;
  out_segment_count, out_stem_id, out_origin_kind: PByte;
  out_duration_days: PDouble
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_siling_segment';
{$ELSE}
  name 'taiyin_bazi_rules_siling_segment';
{$ENDIF}

function taiyin_bazi_rules_select_siling(
  table_model: LongInt;
  month_branch_id: Byte;
  day_coordinate: Double;
  out_segment_index, out_stem_id, out_origin_kind: PByte;
  out_start_day, out_end_day: PDouble
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_select_siling';
{$ELSE}
  name 'taiyin_bazi_rules_select_siling';
{$ENDIF}

function taiyin_bazi_rules_collect_shen_sha(
  pillars: PByte;
  target_ganzhi: Byte;
  target_kind: LongInt;
  out_words: PQWord;
  word_capacity: PtrUInt;
  out_word_count: PPtrUInt
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_collect_shen_sha';
{$ELSE}
  name 'taiyin_bazi_rules_collect_shen_sha';
{$ENDIF}

function taiyin_bazi_rules_collect_shen_sha_with_gender(
  pillars: PByte;
  target_ganzhi: Byte;
  target_kind, gender: LongInt;
  out_words: PQWord;
  word_capacity: PtrUInt;
  out_word_count: PPtrUInt
): LongInt; cdecl; public
{$IFDEF TAIYIN_BAZI_LEADING_UNDERSCORE}
  name '_taiyin_bazi_rules_collect_shen_sha_with_gender';
{$ELSE}
  name 'taiyin_bazi_rules_collect_shen_sha_with_gender';
{$ENDIF}

implementation

{$I generated/taiyin_bazi_shen_sha_tables.inc}

const
  kInvalid = $FF;
  kShenShaStableIdCount = 66;
  kShenShaTargetDay = 2;
  kHiddenStemCount: array[0..11] of Byte = (1, 3, 3, 1, 3, 3, 2, 3, 3, 1, 3, 2);
  kHiddenStems: array[0..11, 0..2] of Byte = (
    (9, kInvalid, kInvalid),
    (5, 9, 7),
    (0, 2, 4),
    (1, kInvalid, kInvalid),
    (4, 1, 9),
    (2, 6, 4),
    (3, 5, kInvalid),
    (5, 3, 1),
    (6, 8, 4),
    (7, kInvalid, kInvalid),
    (4, 7, 3),
    (8, 0, kInvalid)
  );
  kBranchCombinationPartner: array[0..11] of Byte = (1, 0, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2);
  kStemCombinationPartner: array[0..9] of Byte = (5, 6, 7, 8, 9, 0, 1, 2, 3, 4);
  { Keep the bazi_core WuXing enum order: water, wood, metal, earth, fire. }
  kStemCombinationElement: array[0..9] of Byte = (3, 2, 0, 1, 4, 3, 2, 0, 1, 4);
  kStemClashPartner: array[0..9] of ShortInt = (6, 7, 8, 9, -1, -1, 0, 1, 2, 3);
  kBranchCombinationElement: array[0..11] of Byte = (3, 3, 1, 4, 2, 0, 3, 3, 0, 2, 4, 1);
  kBranchClashPartner: array[0..11] of Byte = (6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5);
  kBranchHarmPartner: array[0..11] of Byte = (7, 6, 5, 4, 3, 2, 1, 0, 11, 10, 9, 8);
  kBranchDestructionPartner: array[0..11] of Byte = (9, 4, 11, 6, 1, 8, 3, 10, 5, 0, 7, 2);
  kBranchSeverancePartner: array[0..11] of ShortInt = (5, -1, 9, 8, -1, 0, 11, -1, 3, 2, -1, 6);
  kBranchHiddenCombinationPartner: array[0..11] of ShortInt =
    (5, 2, 1, 8, -1, 0, 11, -1, 3, -1, -1, 6);
  kBranchTripleCombination: array[0..3, 0..2] of Byte = (
    (8, 0, 4), (11, 3, 7), (2, 6, 10), (5, 9, 1)
  );
  kBranchTripleDirection: array[0..3, 0..2] of Byte = (
    (11, 0, 1), (2, 3, 4), (5, 6, 7), (8, 9, 10)
  );
  kTripleElement: array[0..3] of Byte = (0, 1, 4, 2);
  kBranchTriplePunishment: array[0..1, 0..2] of Byte = (
    (2, 5, 8), (1, 10, 7)
  );
  kLifeStageStartFireEarth: array[0..9] of Byte = (11, 6, 2, 9, 2, 9, 5, 0, 8, 3);
  { Keep the bazi_core Nayin and ancient five-element stage conventions. }
  kNayinElementByPair: array[0..29] of Byte = (
    2, 4, 1, 3, 2, 4, 0, 3, 2, 1,
    0, 3, 4, 1, 0, 2, 4, 1, 3, 2,
    4, 0, 3, 2, 1, 0, 3, 4, 1, 0
  );
  kStemElement: array[0..9] of Byte = (1, 1, 4, 4, 3, 3, 2, 2, 0, 0);
  kNayinZhangSheng: array[0..4] of Byte = (8, 11, 5, 8, 2);
  kNayinLinGuan: array[0..4] of Byte = (11, 2, 8, 11, 5);
  kNayinDiWang: array[0..4] of Byte = (0, 3, 9, 0, 6);
  kOfficialElement: array[0..4] of Byte = (3, 2, 4, 1, 0);
  kOfficialStem: array[0..9] of Byte = (7, 6, 9, 8, 1, 0, 3, 2, 5, 4);

  kStemRelationCombination = LongWord(1) shl 0;
  kStemRelationClash = LongWord(1) shl 1;
  kStemRelationRestraint = LongWord(1) shl 2;
  kBranchRelationCombination = LongWord(1) shl 0;
  kBranchRelationClash = LongWord(1) shl 1;
  kBranchRelationHarm = LongWord(1) shl 2;
  kBranchRelationDestruction = LongWord(1) shl 3;
  kBranchRelationPunishment = LongWord(1) shl 4;
  kBranchRelationSelfPunishment = LongWord(1) shl 5;
  kBranchRelationHiddenCombination = LongWord(1) shl 6;
  kBranchRelationSeverance = LongWord(1) shl 7;
  kBranchTripleRelationCombination = LongWord(1) shl 0;
  kBranchTripleRelationDirection = LongWord(1) shl 1;
  kBranchTripleRelationPunishment = LongWord(1) shl 2;

  kRelationStemCombination = 0;
  kRelationStemClash = 1;
  kRelationStemRestraint = 2;
  kRelationBranchCombination = 3;
  kRelationBranchClash = 4;
  kRelationBranchHarm = 5;
  kRelationBranchDestruction = 6;
  kRelationBranchTriplePunishment = 7;
  kRelationBranchPunishment = 8;
  kRelationBranchSelfPunishment = 9;
  kRelationBranchTripleCombination = 10;
  kRelationBranchTripleDirection = 11;
  kRelationBranchHalfCombination = 12;
  kRelationBranchArchingCombination = 13;
  kRelationBranchHiddenCombination = 14;
  kRelationBranchSeverance = 15;
  kRelationKindMaskAll = LongWord($0000FFFF);
  kRelationPillarAll = LongWord($000000FF);
  kMaxRelationNodes = 8;
  kMaxRelations = 512;
  kSilingOriginStem = 0;
  kSilingOriginGenEarth = 1;
  kSilingOriginKunEarth = 2;
  kSilingSegmentCount: array[0..1, 0..11] of Byte = (
    (2, 3, 3, 2, 3, 3, 2, 3, 3, 2, 3, 3),
    (2, 3, 3, 2, 3, 3, 3, 3, 3, 2, 3, 3)
  );
  { Branch order is Zi through Hai; stem order follows taiyin_ganzhi_rules. }
  kSilingStem: array[0..1, 0..11, 0..2] of Byte = (
    (
      (8, 9, kInvalid), (9, 6, 5), (4, 2, 0), (0, 1, kInvalid),
      (1, 8, 4), (4, 6, 2), (2, 3, kInvalid), (3, 0, 5),
      (4, 8, 6), (6, 7, kInvalid), (7, 2, 4), (4, 0, 8)
    ),
    (
      (8, 9, kInvalid), (9, 7, 5), (4, 2, 0), (0, 1, kInvalid),
      (1, 9, 4), (4, 6, 2), (2, 5, 3), (3, 1, 5),
      (4, 8, 6), (6, 7, kInvalid), (7, 3, 4), (4, 0, 8)
    )
  );
  kSilingDuration: array[0..1, 0..11, 0..2] of Byte = (
    (
      (7, 23, 0), (7, 5, 18), (5, 5, 20), (7, 23, 0),
      (7, 5, 18), (7, 5, 18), (7, 23, 0), (7, 5, 18),
      (5, 5, 20), (7, 23, 0), (7, 5, 18), (5, 5, 20)
    ),
    (
      (10, 20, 0), (9, 3, 18), (7, 7, 16), (10, 20, 0),
      (9, 3, 18), (5, 9, 16), (10, 9, 11), (9, 3, 18),
      (10, 3, 17), (10, 20, 0), (9, 3, 18), (7, 5, 18)
    )
  );

type
  TRelationNode = packed record
    value: Byte;
    source_id: Byte;
    pillar_flag: LongWord;
  end;
  TRelationNodeArray = array[0..kMaxRelationNodes - 1] of TRelationNode;
  TPendingRelation = packed record
    kind: LongInt;
    pillar_mask: LongWord;
    value_mask: Word;
    combined_element_id: Byte;
  end;
  TPendingRelationArray = array[0..kMaxRelations - 1] of TPendingRelation;
  TSuppressedPairs = array[0..kMaxRelationNodes - 1, 0..kMaxRelationNodes - 1] of Boolean;

function IsPair(a, b, left, right: Byte): Boolean; inline;
begin
  Result := ((a = left) and (b = right)) or ((a = right) and (b = left));
end;

function IsBranchPunishment(a, b: Byte): Boolean; inline;
begin
  Result := IsPair(a, b, 0, 3)
    or IsPair(a, b, 2, 5)
    or IsPair(a, b, 2, 8)
    or IsPair(a, b, 5, 8)
    or IsPair(a, b, 1, 10)
    or IsPair(a, b, 1, 7)
    or IsPair(a, b, 7, 10);
end;

function IsSelfPunishment(branch_id: Byte): Boolean; inline;
begin
  Result := (branch_id = 4) or (branch_id = 6)
    or (branch_id = 9) or (branch_id = 11);
end;

function MatchesTriple(
  a, b, c: Byte;
  const group: array of Byte
): Boolean; inline;
begin
  Result := (a <> b) and (a <> c) and (b <> c)
    and (((a = group[0]) or (a = group[1]) or (a = group[2]))
    and ((b = group[0]) or (b = group[1]) or (b = group[2]))
    and ((c = group[0]) or (c = group[1]) or (c = group[2])));
end;

function taiyin_bazi_rules_kong_wang(
  value: Byte;
  out_branches: PByte
): LongInt; cdecl;
var
  index, k1, k2: LongInt;
  stem_id: Byte;
begin
  if out_branches = nil then
  begin
    Result := -1;
    Exit;
  end;
  Result := GanzhiIndex(value, @index);
  if Result <> 0 then Exit;
  k1 := (10 - (index div 10) * 2) mod 12;
  k2 := (k1 + 1) mod 12;
  stem_id := StemOf(value);
  { Direct transcription of GanZhi.getKongWang(), including its ordering. }
  if (((stem_id and 1) = 0) and ((k1 and 1) = 0))
    or (((stem_id and 1) = 1) and ((k1 and 1) = 1)) then
  begin
    out_branches[0] := Byte(k1);
    out_branches[1] := Byte(k2);
  end
  else
  begin
    out_branches[0] := Byte(k2);
    out_branches[1] := Byte(k1);
  end;
  Result := 0;
end;

function taiyin_bazi_rules_ten_god(
  day_stem_id, target_stem_id: Byte;
  out_ten_god_id: PByte
): LongInt; cdecl;
var
  element_delta: Byte;
begin
  if (out_ten_god_id = nil) or (not IsValidStem(day_stem_id))
    or (not IsValidStem(target_stem_id)) then
  begin
    Result := -1;
    Exit;
  end;
  element_delta := ((target_stem_id shr 1) + 5 - (day_stem_id shr 1)) mod 5;
  out_ten_god_id^ := (element_delta shl 1) or ((day_stem_id xor target_stem_id) and 1);
  Result := 0;
end;

function taiyin_bazi_rules_stem_relation(
  stem_a, stem_b: Byte;
  out_flags: PLongWord;
  out_combined_element_id: PByte
): LongInt; cdecl;
var
  flags: LongWord;
begin
  if (out_flags = nil) or (out_combined_element_id = nil)
    or (not IsValidStem(stem_a)) or (not IsValidStem(stem_b)) then
  begin
    Result := -1;
    Exit;
  end;
  flags := 0;
  out_combined_element_id^ := kInvalid;
  if kStemCombinationPartner[stem_a] = stem_b then
  begin
    flags := flags or kStemRelationCombination;
    out_combined_element_id^ := kStemCombinationElement[stem_a];
  end;
  if (kStemClashPartner[stem_a] >= 0)
    and (kStemClashPartner[stem_a] = stem_b) then
    flags := flags or kStemRelationClash;
  if (((stem_a + 4) mod 10) = stem_b)
    or (((stem_b + 4) mod 10) = stem_a) then
    flags := flags or kStemRelationRestraint;
  out_flags^ := flags;
  Result := 0;
end;

function taiyin_bazi_rules_branch_relation(
  branch_a, branch_b: Byte;
  out_flags: PLongWord;
  out_combined_element_id: PByte
): LongInt; cdecl;
var
  flags: LongWord;
begin
  if (out_flags = nil) or (out_combined_element_id = nil)
    or (not IsValidBranch(branch_a)) or (not IsValidBranch(branch_b)) then
  begin
    Result := -1;
    Exit;
  end;
  flags := 0;
  out_combined_element_id^ := kInvalid;
  if kBranchCombinationPartner[branch_a] = branch_b then
  begin
    flags := flags or kBranchRelationCombination;
    out_combined_element_id^ := kBranchCombinationElement[branch_a];
  end;
  if kBranchClashPartner[branch_a] = branch_b then
    flags := flags or kBranchRelationClash;
  if kBranchHarmPartner[branch_a] = branch_b then
    flags := flags or kBranchRelationHarm;
  if kBranchDestructionPartner[branch_a] = branch_b then
    flags := flags or kBranchRelationDestruction;
  if (branch_a <> branch_b) and IsBranchPunishment(branch_a, branch_b) then
    flags := flags or kBranchRelationPunishment;
  if (branch_a = branch_b) and IsSelfPunishment(branch_a) then
    flags := flags or kBranchRelationSelfPunishment;
  if (kBranchHiddenCombinationPartner[branch_a] >= 0)
    and (kBranchHiddenCombinationPartner[branch_a] = branch_b) then
    flags := flags or kBranchRelationHiddenCombination;
  if (kBranchSeverancePartner[branch_a] >= 0)
    and (kBranchSeverancePartner[branch_a] = branch_b) then
    flags := flags or kBranchRelationSeverance;
  out_flags^ := flags;
  Result := 0;
end;

function taiyin_bazi_rules_branch_triple_relation(
  branch_a, branch_b, branch_c: Byte;
  out_flags: PLongWord;
  out_combined_element_id: PByte
): LongInt; cdecl;
var
  group_index: LongInt;
  flags: LongWord;
begin
  if (out_flags = nil) or (out_combined_element_id = nil)
    or (not IsValidBranch(branch_a)) or (not IsValidBranch(branch_b))
    or (not IsValidBranch(branch_c)) then
  begin
    Result := -1;
    Exit;
  end;
  flags := 0;
  out_combined_element_id^ := kInvalid;
  for group_index := 0 to 3 do
  begin
    if MatchesTriple(branch_a, branch_b, branch_c, kBranchTripleCombination[group_index]) then
    begin
      flags := flags or kBranchTripleRelationCombination;
      out_combined_element_id^ := kTripleElement[group_index];
    end;
    if MatchesTriple(branch_a, branch_b, branch_c, kBranchTripleDirection[group_index]) then
    begin
      flags := flags or kBranchTripleRelationDirection;
      out_combined_element_id^ := kTripleElement[group_index];
    end;
  end;
  for group_index := 0 to 1 do
  begin
    if MatchesTriple(branch_a, branch_b, branch_c, kBranchTriplePunishment[group_index]) then
      flags := flags or kBranchTripleRelationPunishment;
  end;
  out_flags^ := flags;
  Result := 0;
end;

function RelationKindEnabled(relation_mask: LongWord; kind: LongInt): Boolean; inline;
begin
  Result := (relation_mask and (LongWord(1) shl kind)) <> 0;
end;

function NodeMask(const nodes: TRelationNodeArray; node_count: LongInt): LongWord;
var
  i: LongInt;
begin
  Result := 0;
  for i := 0 to node_count - 1 do
    Result := Result or nodes[i].pillar_flag;
end;

function ValueMask(const nodes: TRelationNodeArray; node_count: LongInt): Word;
var
  i: LongInt;
begin
  Result := 0;
  for i := 0 to node_count - 1 do
    Result := Result or (Word(1) shl nodes[i].value);
end;

procedure BuildNodes(
  pillars: PByte;
  pillar_mask: LongWord;
  use_stem: Boolean;
  var nodes: TRelationNodeArray;
  var node_count: LongInt
);
var
  i: LongInt;
begin
  node_count := 0;
  for i := 0 to kMaxRelationNodes - 1 do
  begin
    if (pillar_mask and (LongWord(1) shl i)) = 0 then Continue;
    if use_stem then
      nodes[node_count].value := pillars[i] shr 4
    else
      nodes[node_count].value := pillars[i] and $0F;
    nodes[node_count].source_id := Byte(i);
    nodes[node_count].pillar_flag := LongWord(1) shl i;
    Inc(node_count);
  end;
end;

function GatherNodes(
  const source: TRelationNodeArray;
  source_count: LongInt;
  first_value, second_value: Byte;
  include_second: Boolean;
  var matched: TRelationNodeArray
): LongInt;
var
  i: LongInt;
begin
  Result := 0;
  for i := 0 to source_count - 1 do
  begin
    if (source[i].value = first_value)
      or (include_second and (source[i].value = second_value)) then
    begin
      matched[Result].value := source[i].value;
      matched[Result].source_id := source[i].source_id;
      matched[Result].pillar_flag := source[i].pillar_flag;
      Inc(Result);
    end;
  end;
end;

function AddRelation(
  var relations: TPendingRelationArray;
  var relation_count: LongInt;
  kind: LongInt;
  const nodes: TRelationNodeArray;
  node_count: LongInt;
  combined_element_id: Byte
): Boolean;
var
  candidate: TPendingRelation;
  i: LongInt;
begin
  candidate.kind := kind;
  candidate.pillar_mask := NodeMask(nodes, node_count);
  candidate.value_mask := ValueMask(nodes, node_count);
  candidate.combined_element_id := combined_element_id;
  for i := 0 to relation_count - 1 do
  begin
    if (relations[i].kind = candidate.kind)
      and (relations[i].combined_element_id = candidate.combined_element_id)
      and ((relations[i].value_mask and candidate.value_mask) <> 0) then
    begin
      relations[i].pillar_mask := relations[i].pillar_mask or candidate.pillar_mask;
      relations[i].value_mask := relations[i].value_mask or candidate.value_mask;
      Result := True;
      Exit;
    end;
  end;
  if relation_count >= kMaxRelations then
  begin
    Result := False;
    Exit;
  end;
  relations[relation_count].kind := candidate.kind;
  relations[relation_count].pillar_mask := candidate.pillar_mask;
  relations[relation_count].value_mask := candidate.value_mask;
  relations[relation_count].combined_element_id := candidate.combined_element_id;
  Inc(relation_count);
  Result := True;
end;

function AppendStemRelations(
  const stems: TRelationNodeArray;
  stem_count: LongInt;
  relation_mask: LongWord;
  var relations: TPendingRelationArray;
  var relation_count: LongInt
): Boolean;
var
  first_value, second_value: Byte;
  matched: TRelationNodeArray;
  matched_count, i: LongInt;
  expected_mask: Word;
begin
  for first_value := 0 to 4 do
  begin
    if not RelationKindEnabled(relation_mask, kRelationStemCombination) then Continue;
    second_value := first_value + 5;
    matched_count := GatherNodes(stems, stem_count, first_value, second_value, True, matched);
    expected_mask := (Word(1) shl first_value) or (Word(1) shl second_value);
    if ValueMask(matched, matched_count) = expected_mask then
      if not AddRelation(relations, relation_count, kRelationStemCombination,
        matched, matched_count, kStemCombinationElement[first_value]) then
      begin
        Result := False;
        Exit;
      end;
  end;

  for i := 0 to 3 do
  begin
    if not RelationKindEnabled(relation_mask, kRelationStemClash) then Continue;
    first_value := Byte(i);
    second_value := Byte(i + 6);
    matched_count := GatherNodes(stems, stem_count, first_value, second_value, True, matched);
    expected_mask := (Word(1) shl first_value) or (Word(1) shl second_value);
    if ValueMask(matched, matched_count) = expected_mask then
      if not AddRelation(relations, relation_count, kRelationStemClash,
        matched, matched_count, kInvalid) then
      begin
        Result := False;
        Exit;
      end;
  end;

  for first_value := 0 to 9 do
  begin
    if not RelationKindEnabled(relation_mask, kRelationStemRestraint) then Continue;
    second_value := (first_value + 4) mod 10;
    matched_count := GatherNodes(stems, stem_count, first_value, second_value, True, matched);
    expected_mask := (Word(1) shl first_value) or (Word(1) shl second_value);
    if ValueMask(matched, matched_count) = expected_mask then
      if not AddRelation(relations, relation_count, kRelationStemRestraint,
        matched, matched_count, kInvalid) then
      begin
        Result := False;
        Exit;
      end;
  end;
  Result := True;
end;

procedure SuppressPair(var suppressed: TSuppressedPairs; first_id, second_id: Byte); inline;
begin
  suppressed[first_id, second_id] := True;
  suppressed[second_id, first_id] := True;
end;

function ValueInTriple(value: Byte; const group: array of Byte): Boolean; inline;
begin
  Result := (value = group[0]) or (value = group[1]) or (value = group[2]);
end;

function AppendBranchRelations(
  const branches: TRelationNodeArray;
  branch_count: LongInt;
  relation_mask: LongWord;
  var relations: TPendingRelationArray;
  var relation_count: LongInt
): Boolean;
var
  suppressed: TSuppressedPairs;
  trio, pair_nodes, matched: TRelationNodeArray;
  i, j, k, group_index, matched_count, first_id, second_id: LongInt;
  flags: LongWord;
  element: Byte;
  kind: LongInt;
begin
  for first_id := 0 to kMaxRelationNodes - 1 do
    for second_id := 0 to kMaxRelationNodes - 1 do
      suppressed[first_id, second_id] := False;
  for i := 0 to branch_count - 1 do
    for j := i + 1 to branch_count - 1 do
      for k := j + 1 to branch_count - 1 do
      begin
        trio[0].value := branches[i].value;
        trio[0].source_id := branches[i].source_id;
        trio[0].pillar_flag := branches[i].pillar_flag;
        trio[1].value := branches[j].value;
        trio[1].source_id := branches[j].source_id;
        trio[1].pillar_flag := branches[j].pillar_flag;
        trio[2].value := branches[k].value;
        trio[2].source_id := branches[k].source_id;
        trio[2].pillar_flag := branches[k].pillar_flag;
        for group_index := 0 to 3 do
        begin
          if MatchesTriple(trio[0].value, trio[1].value, trio[2].value,
            kBranchTripleDirection[group_index]) then
          begin
            if RelationKindEnabled(relation_mask, kRelationBranchTripleDirection) then
              if not AddRelation(relations, relation_count,
                kRelationBranchTripleDirection, trio, 3, kTripleElement[group_index]) then
              begin
                Result := False;
                Exit;
              end;
            SuppressPair(suppressed, branches[i].source_id, branches[j].source_id);
            SuppressPair(suppressed, branches[i].source_id, branches[k].source_id);
            SuppressPair(suppressed, branches[j].source_id, branches[k].source_id);
          end;
        end;
        for group_index := 0 to 3 do
        begin
          if MatchesTriple(trio[0].value, trio[1].value, trio[2].value,
            kBranchTripleCombination[group_index]) then
          begin
            if RelationKindEnabled(relation_mask, kRelationBranchTripleCombination) then
              if not AddRelation(relations, relation_count,
                kRelationBranchTripleCombination, trio, 3, kTripleElement[group_index]) then
              begin
                Result := False;
                Exit;
              end;
            SuppressPair(suppressed, branches[i].source_id, branches[j].source_id);
            SuppressPair(suppressed, branches[i].source_id, branches[k].source_id);
            SuppressPair(suppressed, branches[j].source_id, branches[k].source_id);
          end;
        end;
        for group_index := 0 to 1 do
        begin
          if MatchesTriple(trio[0].value, trio[1].value, trio[2].value,
            kBranchTriplePunishment[group_index]) then
          begin
            if RelationKindEnabled(relation_mask, kRelationBranchTriplePunishment) then
              if not AddRelation(relations, relation_count,
                kRelationBranchTriplePunishment, trio, 3, kInvalid) then
              begin
                Result := False;
                Exit;
              end;
            SuppressPair(suppressed, branches[i].source_id, branches[j].source_id);
            SuppressPair(suppressed, branches[i].source_id, branches[k].source_id);
            SuppressPair(suppressed, branches[j].source_id, branches[k].source_id);
          end;
        end;
      end;

  for i := 0 to branch_count - 1 do
    for j := i + 1 to branch_count - 1 do
    begin
      pair_nodes[0].value := branches[i].value;
      pair_nodes[0].source_id := branches[i].source_id;
      pair_nodes[0].pillar_flag := branches[i].pillar_flag;
      pair_nodes[1].value := branches[j].value;
      pair_nodes[1].source_id := branches[j].source_id;
      pair_nodes[1].pillar_flag := branches[j].pillar_flag;
      if not suppressed[branches[i].source_id, branches[j].source_id] then
      begin
        for group_index := 0 to 3 do
        begin
          if (branches[i].value <> branches[j].value)
            and ValueInTriple(branches[i].value, kBranchTripleCombination[group_index])
            and ValueInTriple(branches[j].value, kBranchTripleCombination[group_index]) then
          begin
            if (branches[i].value = kBranchTripleCombination[group_index, 1])
              or (branches[j].value = kBranchTripleCombination[group_index, 1]) then
              kind := kRelationBranchHalfCombination
            else
              kind := kRelationBranchArchingCombination;
            if RelationKindEnabled(relation_mask, kind) then
              if not AddRelation(relations, relation_count, kind, pair_nodes, 2,
                kTripleElement[group_index]) then
              begin
                Result := False;
                Exit;
              end;
          end;
        end;
      end;

      if taiyin_bazi_rules_branch_relation(branches[i].value, branches[j].value,
        @flags, @element) <> 0 then
      begin
        Result := False;
        Exit;
      end;
      if (not suppressed[branches[i].source_id, branches[j].source_id])
        and ((flags and kBranchRelationPunishment) <> 0)
        and RelationKindEnabled(relation_mask, kRelationBranchPunishment) then
        if not AddRelation(relations, relation_count, kRelationBranchPunishment,
          pair_nodes, 2, kInvalid) then begin Result := False; Exit; end;
      if ((flags and kBranchRelationCombination) <> 0)
        and RelationKindEnabled(relation_mask, kRelationBranchCombination) then
        if not AddRelation(relations, relation_count, kRelationBranchCombination,
          pair_nodes, 2, element) then begin Result := False; Exit; end;
      if ((flags and kBranchRelationClash) <> 0)
        and RelationKindEnabled(relation_mask, kRelationBranchClash) then
        if not AddRelation(relations, relation_count, kRelationBranchClash,
          pair_nodes, 2, kInvalid) then begin Result := False; Exit; end;
      if ((flags and kBranchRelationHarm) <> 0)
        and RelationKindEnabled(relation_mask, kRelationBranchHarm) then
        if not AddRelation(relations, relation_count, kRelationBranchHarm,
          pair_nodes, 2, kInvalid) then begin Result := False; Exit; end;
      if ((flags and kBranchRelationDestruction) <> 0)
        and RelationKindEnabled(relation_mask, kRelationBranchDestruction) then
        if not AddRelation(relations, relation_count, kRelationBranchDestruction,
          pair_nodes, 2, kInvalid) then begin Result := False; Exit; end;
      if ((flags and kBranchRelationHiddenCombination) <> 0)
        and RelationKindEnabled(relation_mask, kRelationBranchHiddenCombination) then
        if not AddRelation(relations, relation_count, kRelationBranchHiddenCombination,
          pair_nodes, 2, kInvalid) then begin Result := False; Exit; end;
      if ((flags and kBranchRelationSeverance) <> 0)
        and RelationKindEnabled(relation_mask, kRelationBranchSeverance) then
        if not AddRelation(relations, relation_count, kRelationBranchSeverance,
          pair_nodes, 2, kInvalid) then begin Result := False; Exit; end;
    end;

  for i := 0 to 3 do
  begin
    if not RelationKindEnabled(relation_mask, kRelationBranchSelfPunishment) then Continue;
    case i of
      0: kind := 4;
      1: kind := 6;
      2: kind := 9;
    else
      kind := 11;
    end;
    matched_count := GatherNodes(branches, branch_count, Byte(kind), 0, False, matched);
    if matched_count >= 2 then
      if not AddRelation(relations, relation_count, kRelationBranchSelfPunishment,
        matched, matched_count, kInvalid) then
      begin
        Result := False;
        Exit;
      end;
  end;
  Result := True;
end;

function taiyin_bazi_rules_collect_relations(
  pillars: PByte;
  pillar_mask, relation_mask: LongWord;
  out_relations: Pointer;
  capacity: PtrUInt;
  out_count: PPtrUInt
): LongInt; cdecl;
var
  stems, branches: TRelationNodeArray;
  stem_count, branch_count, relation_count, i: LongInt;
  relations: TPendingRelationArray;
  output: PByte;
begin
  if out_count <> nil then out_count^ := 0;
  if (pillars = nil) or (out_count = nil) or (pillar_mask = 0)
    or ((pillar_mask and (not kRelationPillarAll)) <> 0)
    or ((relation_mask and (not kRelationKindMaskAll)) <> 0)
    or ((out_relations = nil) and (capacity <> 0)) then
  begin
    Result := -1;
    Exit;
  end;
  for i := 0 to kMaxRelationNodes - 1 do
    if ((pillar_mask and (LongWord(1) shl i)) <> 0)
      and (not IsValidGanzhi(pillars[i])) then
    begin
      Result := -1;
      Exit;
    end;

  BuildNodes(pillars, pillar_mask, True, stems, stem_count);
  BuildNodes(pillars, pillar_mask, False, branches, branch_count);
  relation_count := 0;
  if not AppendStemRelations(stems, stem_count, relation_mask, relations, relation_count)
    or not AppendBranchRelations(branches, branch_count, relation_mask,
      relations, relation_count) then
  begin
    Result := -2;
    Exit;
  end;
  out_count^ := PtrUInt(relation_count);
  if (out_relations = nil) and (capacity = 0) then
  begin
    Result := 0;
    Exit;
  end;
  if capacity < PtrUInt(relation_count) then
  begin
    Result := -2;
    Exit;
  end;
  output := PByte(out_relations);
  for i := 0 to relation_count - 1 do
  begin
    PLongInt(output + PtrUInt(i) * 12)^ := relations[i].kind;
    PLongWord(output + PtrUInt(i) * 12 + 4)^ := relations[i].pillar_mask;
    (output + PtrUInt(i) * 12 + 8)^ := relations[i].combined_element_id;
    (output + PtrUInt(i) * 12 + 9)^ := 0;
    (output + PtrUInt(i) * 12 + 10)^ := 0;
    (output + PtrUInt(i) * 12 + 11)^ := 0;
  end;
  Result := 0;
end;

function taiyin_bazi_rules_life_stage(
  stem_id, branch_id: Byte;
  earth_palace_mode: LongInt;
  out_life_stage_id: PByte
): LongInt; cdecl;
var
  start_branch: Byte;
begin
  if (out_life_stage_id = nil) or (not IsValidStem(stem_id))
    or (not IsValidBranch(branch_id))
    or (earth_palace_mode < 0) or (earth_palace_mode > 1) then
  begin
    Result := -1;
    Exit;
  end;
  start_branch := kLifeStageStartFireEarth[stem_id];
  if earth_palace_mode = 1 then
  begin
    if stem_id = 4 then start_branch := 8;
    if stem_id = 5 then start_branch := 3;
  end;
  if (stem_id and 1) = 0 then
    out_life_stage_id^ := (branch_id + 12 - start_branch) mod 12
  else
    out_life_stage_id^ := (start_branch + 12 - branch_id) mod 12;
  Result := 0;
end;

function taiyin_bazi_rules_hidden_stems(
  branch_id: Byte;
  out_stems: PByte;
  out_count: PByte
): LongInt; cdecl;
var
  index: LongInt;
begin
  if (out_stems = nil) or (out_count = nil) or (not IsValidBranch(branch_id)) then
  begin
    Result := -1;
    Exit;
  end;
  for index := 0 to 2 do out_stems[index] := kHiddenStems[branch_id, index];
  out_count^ := kHiddenStemCount[branch_id];
  Result := 0;
end;

function taiyin_bazi_rules_extra_pillars(
  year_pillar, month_pillar, day_pillar, hour_pillar: Byte;
  out_ming_gong, out_shen_gong, out_tai_yuan, out_tai_xi: PByte
): LongInt; cdecl;
var
  year_stem, month_branch, hour_branch, month_number, month_position: LongInt;
  ming_branch, shen_branch, ming_stem, shen_stem, start_stem: Byte;
  tai_yuan, tai_xi: Byte;
begin
  if (out_ming_gong = nil) or (out_shen_gong = nil) or (out_tai_yuan = nil)
    or (out_tai_xi = nil) or (not IsValidGanzhi(year_pillar))
    or (not IsValidGanzhi(month_pillar)) or (not IsValidGanzhi(day_pillar))
    or (not IsValidGanzhi(hour_pillar)) then
  begin
    Result := -1;
    Exit;
  end;

  year_stem := StemOf(year_pillar);
  month_branch := BranchOf(month_pillar);
  hour_branch := BranchOf(hour_pillar);
  month_number := ((month_branch - 2 + 12) mod 12) + 1;
  month_position := (12 - (month_number - 1)) mod 12;
  ming_branch := (month_position + ((3 - hour_branch + 12) mod 12)) mod 12;
  shen_branch := (month_branch + hour_branch + 1) mod 12;
  start_stem := ((year_stem mod 5) * 2 + 2) mod 10;
  ming_stem := (start_stem + ((ming_branch - 2 + 12) mod 12)) mod 10;
  shen_stem := (start_stem + ((shen_branch - 2 + 12) mod 12)) mod 10;

  Result := MakeGanzhi(ming_stem, ming_branch, out_ming_gong);
  if Result <> 0 then Exit;
  Result := MakeGanzhi(shen_stem, shen_branch, out_shen_gong);
  if Result <> 0 then Exit;
  Result := AdvanceGanzhi(month_pillar, -9, @tai_yuan);
  if Result <> 0 then Exit;
  Result := MakeGanzhi(
    (StemOf(day_pillar) + 5) mod 10,
    kBranchCombinationPartner[BranchOf(day_pillar)],
    @tai_xi);
  if Result <> 0 then Exit;
  out_tai_yuan^ := tai_yuan;
  out_tai_xi^ := tai_xi;
end;

function taiyin_bazi_rules_qiyun_direction(
  year_pillar: Byte;
  gender, direction_mode: LongInt;
  out_direction: PLongInt
): LongInt; cdecl;
var
  yang_year, male: Boolean;
begin
  if (out_direction = nil) or (not IsValidGanzhi(year_pillar))
    or (gender < 0) or (gender > 1) or (direction_mode <> 0) then
  begin
    Result := -1;
    Exit;
  end;
  yang_year := (StemOf(year_pillar) and 1) = 0;
  male := gender = 1;
  if yang_year = male then
    out_direction^ := 1
  else
    out_direction^ := -1;
  Result := 0;
end;

function taiyin_bazi_rules_dayun_ganzhi(
  month_pillar: Byte;
  direction: LongInt;
  one_based_index: LongWord;
  out_ganzhi: PByte
): LongInt; cdecl;
var
  signed_index: Int64;
begin
  if (out_ganzhi = nil) or (not IsValidGanzhi(month_pillar))
    or ((direction <> -1) and (direction <> 1)) or (one_based_index = 0) then
  begin
    Result := -1;
    Exit;
  end;
  signed_index := Int64(direction) * Int64(one_based_index mod 60);
  Result := AdvanceGanzhi(month_pillar, LongInt(signed_index), out_ganzhi);
end;

function SilingOrigin(table_model: LongInt; branch_id, segment_index: Byte): Byte; inline;
begin
  Result := kSilingOriginStem;
  if table_model <> 0 then Exit;
  if (branch_id = 2) and (segment_index = 0) then
    Result := kSilingOriginGenEarth
  else if (branch_id = 8) and (segment_index = 0) then
    Result := kSilingOriginKunEarth;
end;

function taiyin_bazi_rules_siling_segment(
  table_model: LongInt;
  month_branch_id, segment_index: Byte;
  out_segment_count, out_stem_id, out_origin_kind: PByte;
  out_duration_days: PDouble
): LongInt; cdecl;
begin
  if (out_segment_count = nil) or (out_stem_id = nil)
    or (out_origin_kind = nil) or (out_duration_days = nil)
    or (table_model < 0) or (table_model > 1)
    or (not IsValidBranch(month_branch_id)) then
  begin
    Result := -1;
    Exit;
  end;
  if segment_index >= kSilingSegmentCount[table_model, month_branch_id] then
  begin
    Result := -1;
    Exit;
  end;
  out_segment_count^ := kSilingSegmentCount[table_model, month_branch_id];
  out_stem_id^ := kSilingStem[table_model, month_branch_id, segment_index];
  out_origin_kind^ := SilingOrigin(table_model, month_branch_id, segment_index);
  out_duration_days^ := kSilingDuration[table_model, month_branch_id, segment_index];
  Result := 0;
end;

function taiyin_bazi_rules_select_siling(
  table_model: LongInt;
  month_branch_id: Byte;
  day_coordinate: Double;
  out_segment_index, out_stem_id, out_origin_kind: PByte;
  out_start_day, out_end_day: PDouble
): LongInt; cdecl;
var
  segment_count, segment_index: Byte;
  start_day, end_day: Double;
begin
  if (out_segment_index = nil) or (out_stem_id = nil)
    or (out_origin_kind = nil) or (out_start_day = nil) or (out_end_day = nil)
    or (table_model < 0) or (table_model > 1)
    or (not IsValidBranch(month_branch_id))
    or (day_coordinate <> day_coordinate) or (day_coordinate < 0.0) then
  begin
    Result := -1;
    Exit;
  end;
  segment_count := kSilingSegmentCount[table_model, month_branch_id];
  start_day := 0.0;
  for segment_index := 0 to segment_count - 1 do
  begin
    end_day := start_day + kSilingDuration[
      table_model, month_branch_id, segment_index];
    if (day_coordinate < end_day) or (segment_index + 1 = segment_count) then
    begin
      out_segment_index^ := segment_index;
      out_stem_id^ := kSilingStem[table_model, month_branch_id, segment_index];
      out_origin_kind^ := SilingOrigin(
        table_model, month_branch_id, segment_index);
      out_start_day^ := start_day;
      out_end_day^ := end_day;
      Result := 0;
      Exit;
    end;
    start_day := end_day;
  end;
  Result := -1;
end;

function MaskContainsBranch(mask: Word; branch_id: Byte): Boolean; inline;
begin
  Result := (mask and (Word(1) shl branch_id)) <> 0;
end;

function MaskContainsStem(mask: Word; stem_id: Byte): Boolean; inline;
begin
  Result := (mask and (Word(1) shl stem_id)) <> 0;
end;

function MaskContainsGanzhi(mask: QWord; ganzhi: Byte): Boolean; inline;
var
  index: LongInt;
begin
  Result := (GanzhiIndex(ganzhi, @index) = 0)
    and ((mask and (QWord(1) shl index)) <> 0);
end;

function SeasonOfMonthBranch(branch_id: Byte): Byte; inline;
begin
  Result := ((branch_id + 10) mod 12) div 3;
end;

function SameXun(first_ganzhi, second_ganzhi: Byte): Boolean; inline;
var
  first_index, second_index: LongInt;
begin
  Result := (GanzhiIndex(first_ganzhi, @first_index) = 0)
    and (GanzhiIndex(second_ganzhi, @second_index) = 0)
    and ((first_index div 10) = (second_index div 10));
end;

function KongWangContains(base_ganzhi, target_ganzhi: Byte): Boolean; inline;
var
  base_index: LongInt;
  first_empty, second_empty: Byte;
begin
  if GanzhiIndex(base_ganzhi, @base_index) <> 0 then
  begin
    Result := False;
    Exit;
  end;
  first_empty := Byte((10 - (base_index div 10) * 2 + 12) mod 12);
  second_empty := Byte((first_empty + 1) mod 12);
  Result := (BranchOf(target_ganzhi) = first_empty)
    or (BranchOf(target_ganzhi) = second_empty);
end;

function IsXunFoodGod(base_ganzhi, target_ganzhi: Byte): Boolean; inline;
begin
  Result := SameXun(base_ganzhi, target_ganzhi)
    and (StemOf(target_ganzhi) = ((StemOf(base_ganzhi) + 2) mod 10));
end;

function IsTianDeHe(month_branch, target_ganzhi: Byte): Boolean; inline;
begin
  case month_branch of
    2: Result := StemOf(target_ganzhi) = 8;
    3: Result := BranchOf(target_ganzhi) = 5;
    4: Result := StemOf(target_ganzhi) = 3;
    5: Result := StemOf(target_ganzhi) = 2;
    6: Result := BranchOf(target_ganzhi) = 2;
    7: Result := StemOf(target_ganzhi) = 5;
    8: Result := StemOf(target_ganzhi) = 4;
    9: Result := BranchOf(target_ganzhi) = 11;
    10: Result := StemOf(target_ganzhi) = 7;
    11: Result := StemOf(target_ganzhi) = 6;
    0: Result := BranchOf(target_ganzhi) = 8;
    1: Result := StemOf(target_ganzhi) = 1;
  else
    Result := False;
  end;
end;

function NayinElementOf(ganzhi: Byte): Byte; inline;
var
  index: LongInt;
begin
  if GanzhiIndex(ganzhi, @index) <> 0 then
  begin
    Result := kInvalid;
    Exit;
  end;
  Result := kNayinElementByPair[index div 2];
end;

function IsTianDeGuiRen(month_branch, target_ganzhi: Byte): Boolean; inline;
begin
  case month_branch of
    2: Result := StemOf(target_ganzhi) = 3;
    3: Result := BranchOf(target_ganzhi) = 8;
    4: Result := StemOf(target_ganzhi) = 8;
    5: Result := StemOf(target_ganzhi) = 7;
    6: Result := BranchOf(target_ganzhi) = 11;
    7: Result := StemOf(target_ganzhi) = 0;
    8: Result := StemOf(target_ganzhi) = 9;
    9: Result := BranchOf(target_ganzhi) = 2;
    10: Result := StemOf(target_ganzhi) = 2;
    11: Result := StemOf(target_ganzhi) = 1;
    0: Result := BranchOf(target_ganzhi) = 5;
    1: Result := StemOf(target_ganzhi) = 6;
  else
    Result := False;
  end;
end;

function IsNayinSchoolStage(
  base_ganzhi, target_ganzhi: Byte;
  stage: Byte
): Boolean; inline;
var
  base_element, target_element, expected_branch: Byte;
begin
  base_element := NayinElementOf(base_ganzhi);
  target_element := NayinElementOf(target_ganzhi);
  if (base_element = kInvalid) or (target_element = kInvalid)
    or (base_element <> target_element) then
  begin
    Result := False;
    Exit;
  end;
  if stage = 0 then
    expected_branch := kNayinZhangSheng[base_element]
  else
    expected_branch := kNayinLinGuan[base_element];
  Result := BranchOf(target_ganzhi) = expected_branch;
end;

function IsOfficialSchoolStage(
  day_stem, target_ganzhi: Byte;
  stage: Byte
): Boolean; inline;
var
  day_element, official_element, expected_branch: Byte;
begin
  day_element := kStemElement[day_stem];
  official_element := kOfficialElement[day_element];
  if stage = 0 then
    expected_branch := kNayinZhangSheng[official_element]
  else
    expected_branch := kNayinLinGuan[official_element];
  Result := BranchOf(target_ganzhi) = expected_branch;
end;

function IsOfficialStarSchool(day_stem, target_ganzhi: Byte): Boolean; inline;
begin
  Result := (BranchOf(target_ganzhi) = kNayinZhangSheng[kStemElement[day_stem]])
    and (StemOf(target_ganzhi) = kOfficialStem[day_stem]);
end;

function IsNayinNoble(year_pillar, day_pillar, target_ganzhi: Byte): Boolean; inline;
var
  target_branch: Byte;
begin
  target_branch := BranchOf(target_ganzhi);
  Result := (target_branch = kNayinDiWang[NayinElementOf(year_pillar)])
    and (MaskContainsBranch(
      kShenShaTianYiGuiRenMasks[StemOf(year_pillar)], target_branch)
      or MaskContainsBranch(
        kShenShaTianYiGuiRenMasks[StemOf(day_pillar)], target_branch));
end;

function IsUnorderedBranchPair(
  first_branch, second_branch, left_branch, right_branch: Byte
): Boolean; inline;
begin
  Result := ((first_branch = left_branch) and (second_branch = right_branch))
    or ((first_branch = right_branch) and (second_branch = left_branch));
end;

function CollectShenShaImpl(
  pillars: PByte;
  target_ganzhi: Byte;
  target_kind, gender: LongInt;
  out_words: PQWord;
  word_capacity: PtrUInt;
  out_word_count: PPtrUInt
): LongInt;
var
  year_pillar, month_pillar, day_pillar, hour_pillar: Byte;
  target_branch, season: Byte;
  year_branch, target_stem, forward_branch, backward_branch: Byte;
  is_forward, has_required_stems, has_other_branch: Boolean;
  year_nayin, day_stem, day_branch, hour_stem, hour_branch: Byte;
begin
  if out_word_count <> nil then out_word_count^ := 0;
  if (pillars = nil) or (out_word_count = nil)
    or ((out_words = nil) and (word_capacity <> 0))
    or (target_kind < 0) or (target_kind > 12)
    or (gender < -1) or (gender > 1)
    or (not IsValidGanzhi(target_ganzhi)) then
  begin
    Result := -1;
    Exit;
  end;
  year_pillar := PByte(PtrUInt(pillars))^;
  month_pillar := PByte(PtrUInt(pillars) + 1)^;
  day_pillar := PByte(PtrUInt(pillars) + 2)^;
  hour_pillar := PByte(PtrUInt(pillars) + 3)^;
  if (not IsValidGanzhi(year_pillar)) or (not IsValidGanzhi(month_pillar))
    or (not IsValidGanzhi(day_pillar)) then
  begin
    Result := -1;
    Exit;
  end;
  { The legacy no-gender entry is also used by callers that only provide
    enough pillars for the gender-independent rules.  The gender-aware
    entry enables hour-dependent rules and therefore requires a valid hour. }
  if (gender >= 0) and (not IsValidGanzhi(hour_pillar)) then
  begin
    Result := -1;
    Exit;
  end;
  out_word_count^ := DynamicBitSetWordCount(kShenShaStableIdCount);
  if out_words = nil then
  begin
    Result := 0;
    Exit;
  end;
  if not DynamicBitSetStorageValid(
      out_words, word_capacity, kShenShaStableIdCount) then
  begin
    Result := -2;
    Exit;
  end;
  DynamicBitSetClear(out_words, kShenShaStableIdCount);
  target_branch := BranchOf(target_ganzhi);
  season := SeasonOfMonthBranch(BranchOf(month_pillar));

  if MaskContainsBranch(
      kShenShaTianYiGuiRenMasks[StemOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaTianYiGuiRenMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 0);

  if MaskContainsBranch(
      kShenShaYiMaMasks[BranchOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaYiMaMasks[BranchOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 1);

  if MaskContainsBranch(
      kShenShaXianChiTaoHuaMasks[BranchOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaXianChiTaoHuaMasks[BranchOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 2);

  if MaskContainsBranch(
      kShenShaHongLuanMasks[BranchOf(year_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 3);

  if MaskContainsBranch(
      kShenShaTianXiMasks[BranchOf(year_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 4);

  if MaskContainsBranch(
      kShenShaYangRenMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 5);

  if MaskContainsBranch(
      kShenShaFeiRenMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 6);

  if MaskContainsBranch(
      kShenShaFuXingGuiRenMasks[StemOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaFuXingGuiRenMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 7);

  if MaskContainsBranch(
      kShenShaZaiShaMasks[BranchOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaZaiShaMasks[BranchOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 8);

  if MaskContainsBranch(
      kShenShaJieShaMasks[BranchOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaJieShaMasks[BranchOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 9);

  if MaskContainsBranch(
      kShenShaWangShenMasks[BranchOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaWangShenMasks[BranchOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 10);

  if KongWangContains(year_pillar, target_ganzhi)
    or KongWangContains(day_pillar, target_ganzhi) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 11);

  if IsXunFoodGod(year_pillar, target_ganzhi)
    or IsXunFoodGod(day_pillar, target_ganzhi) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 12);

  if MaskContainsBranch(
      kShenShaTianChuGuiRenMasks[StemOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaTianChuGuiRenMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 13);

  if MaskContainsStem(
      kShenShaDeXiuGuiRenMasks[BranchOf(month_pillar)], StemOf(target_ganzhi)) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 14);

  if MaskContainsBranch(
      kShenShaTianYiMedicineMasks[BranchOf(month_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 15);

  if MaskContainsBranch(
      kShenShaXueRenMasks[BranchOf(month_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 16);

  if MaskContainsStem(
      kShenShaYueDeHeMasks[BranchOf(month_pillar)], StemOf(target_ganzhi)) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 17);

  { The following rules need either the caller's gender or the complete
    natal chart.  The legacy entry point passes gender=-1 and therefore keeps
    its historical gender-neutral behavior; the *_with_gender entry point
    enables these rules without changing the bitset layout. }
  if gender >= 0 then
  begin
    year_branch := BranchOf(year_pillar);
    target_stem := StemOf(target_ganzhi);
    is_forward := ((gender = 1) = ((StemOf(year_pillar) and 1) = 0));
    forward_branch := (year_branch + 3) mod 12;
    backward_branch := (year_branch + 9) mod 12;

    { 阳男阴女：前三为勾、后三为绞；逆行时相反。 }
    if is_forward then
    begin
      if target_branch = forward_branch then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 18)
      else if target_branch = backward_branch then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 19);
    end
    else
    begin
      if target_branch = backward_branch then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 18)
      else if target_branch = forward_branch then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 19);
    end;

    { 元辰：对冲支顺行取前一位，逆行取后一位。 }
    if is_forward then
      forward_branch := (year_branch + 7) mod 12
    else
      forward_branch := (year_branch + 5) mod 12;
    if target_branch = forward_branch then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 20);

    day_stem := StemOf(day_pillar);
    day_branch := BranchOf(day_pillar);
    hour_stem := StemOf(hour_pillar);
    hour_branch := BranchOf(hour_pillar);

    { 金神：甲/己日，时柱癸酉、己巳、乙丑。 }
    if (target_kind = 3) and ((day_stem = 0) or (day_stem = 5))
      and (((target_stem = 9) and (target_branch = 9))
        or ((target_stem = 5) and (target_branch = 5))
        or ((target_stem = 1) and (target_branch = 1))) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 25);

    { 童子：只在日、时支查，先查季令，再查年柱纳音。 }
    year_nayin := NayinElementOf(year_pillar);
    if (target_kind = 2) or (target_kind = 3) then
    begin
      if (((season = 0) or (season = 2))
          and ((target_branch = 2) or (target_branch = 0)))
        or (((season = 1) or (season = 3))
          and ((target_branch = 3) or (target_branch = 7)
            or (target_branch = 4)))
        or (((year_nayin = 2) or (year_nayin = 1))
          and ((target_branch = 6) or (target_branch = 3)))
        or (((year_nayin = 0) or (year_nayin = 4))
          and ((target_branch = 9) or (target_branch = 10)))
        or ((year_nayin = 3)
          and ((target_branch = 4) or (target_branch = 5))) then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 31);
    end;

    { 三奇贵人：四柱天干包含指定三干，只在日柱显示。 }
    if target_kind = 2 then
    begin
      has_required_stems :=
        (StemOf(year_pillar) = 0)
          or (StemOf(month_pillar) = 0) or (day_stem = 0)
          or (hour_stem = 0);
      if has_required_stems
        and ((StemOf(year_pillar) = 4)
          or (StemOf(month_pillar) = 4) or (day_stem = 4)
          or (hour_stem = 4))
        and ((StemOf(year_pillar) = 6)
          or (StemOf(month_pillar) = 6) or (day_stem = 6)
          or (hour_stem = 6)) then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 33);

      if ((StemOf(year_pillar) = 1)
          or (StemOf(month_pillar) = 1) or (day_stem = 1)
          or (hour_stem = 1))
        and ((StemOf(year_pillar) = 2)
          or (StemOf(month_pillar) = 2) or (day_stem = 2)
          or (hour_stem = 2))
        and ((StemOf(year_pillar) = 3)
          or (StemOf(month_pillar) = 3) or (day_stem = 3)
          or (hour_stem = 3)) then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 34);

      if ((StemOf(year_pillar) = 8)
          or (StemOf(month_pillar) = 8) or (day_stem = 8)
          or (hour_stem = 8))
        and ((StemOf(year_pillar) = 9)
          or (StemOf(month_pillar) = 9) or (day_stem = 9)
          or (hour_stem = 9))
        and ((StemOf(year_pillar) = 7)
          or (StemOf(month_pillar) = 7) or (day_stem = 7)
          or (hour_stem = 7)) then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 35);
    end;

    { 天罗地网：保留旧 Dart 的年柱纳音、性别和成对地支条件。 }
    if (year_nayin <> 1) and (year_nayin <> 2) then
    begin
      if (target_branch = 10) or (target_branch = 11) then
      begin
        has_other_branch := False;
        if target_branch = 10 then
          forward_branch := 11
        else
          forward_branch := 10;
        if BranchOf(year_pillar) = forward_branch then has_other_branch := True;
        if BranchOf(month_pillar) = forward_branch then has_other_branch := True;
        if day_branch = forward_branch then has_other_branch := True;
        if hour_branch = forward_branch then has_other_branch := True;
        if (year_nayin = 4) and (gender = 1) and has_other_branch then
          DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 45);
      end
      else if (target_branch = 4) or (target_branch = 5) then
      begin
        has_other_branch := False;
        if target_branch = 4 then
          forward_branch := 5
        else
          forward_branch := 4;
        if BranchOf(year_pillar) = forward_branch then has_other_branch := True;
        if BranchOf(month_pillar) = forward_branch then has_other_branch := True;
        if day_branch = forward_branch then has_other_branch := True;
        if hour_branch = forward_branch then has_other_branch := True;
        if ((year_nayin = 0) or (year_nayin = 3))
          and (gender = 0) and has_other_branch then
          DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 45);
      end;
    end;

    { 拱禄、拱贵：日时同干，日时支成旧 Dart 中的拱夹组合。 }
    if (target_kind = 2) and (day_stem = hour_stem)
      and (day_branch <> hour_branch) then
    begin
      if ((day_stem = 9) and IsUnorderedBranchPair(day_branch, hour_branch, 11, 1))
        or ((day_stem = 3) and IsUnorderedBranchPair(day_branch, hour_branch, 5, 7))
        or ((day_stem = 5) and IsUnorderedBranchPair(day_branch, hour_branch, 7, 5))
        or ((day_stem = 4) and IsUnorderedBranchPair(day_branch, hour_branch, 4, 6)) then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 48);

      if ((day_stem = 0) and
          (IsUnorderedBranchPair(day_branch, hour_branch, 8, 10)
            or IsUnorderedBranchPair(day_branch, hour_branch, 2, 0)))
        or ((day_stem = 1) and IsUnorderedBranchPair(day_branch, hour_branch, 7, 9))
        or ((day_stem = 4) and IsUnorderedBranchPair(day_branch, hour_branch, 8, 6))
        or ((day_stem = 7) and IsUnorderedBranchPair(day_branch, hour_branch, 1, 3)) then
        DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 49);
    end;
  end;

  if MaskContainsBranch(
      kShenShaGuChenMasks[BranchOf(year_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 21);

  if MaskContainsBranch(
      kShenShaGuaSuMasks[BranchOf(year_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 22);

  if MaskContainsBranch(
      kShenShaHongYanShaMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 23);

  if MaskContainsBranch(
      kShenShaJinYuMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 24);

  if MaskContainsBranch(
      kShenShaLiuXiaMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 27);

  if MaskContainsBranch(
      kShenShaSangMenMasks[BranchOf(year_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 28);

  if MaskContainsBranch(
      kShenShaDiaoKeMasks[BranchOf(year_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 29);

  if MaskContainsBranch(
      kShenShaPiMaMasks[BranchOf(year_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 30);

  if MaskContainsBranch(
      kShenShaJiangXingMasks[BranchOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaJiangXingMasks[BranchOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 36);

  if MaskContainsBranch(
      kShenShaHuaGaiMasks[BranchOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaHuaGaiMasks[BranchOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 37);

  if MaskContainsBranch(
      kShenShaTaiJiGuiRenMasks[StemOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaTaiJiGuiRenMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 52);

  if MaskContainsBranch(
      kShenShaWenChangGuiRenMasks[StemOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaWenChangGuiRenMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 53);

  if MaskContainsBranch(
      kShenShaGuoYinGuiRenMasks[StemOf(year_pillar)], target_branch)
    or MaskContainsBranch(
      kShenShaGuoYinGuiRenMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 54);

  if MaskContainsStem(
      kShenShaYueDeGuiRenMasks[BranchOf(month_pillar)], StemOf(target_ganzhi)) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 56);

  if IsTianDeHe(BranchOf(month_pillar), target_ganzhi) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 32);

  if IsTianDeGuiRen(BranchOf(month_pillar), target_ganzhi) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 55);

  if MaskContainsBranch(
      kShenShaLuShenMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 57);

  if MaskContainsBranch(
      kShenShaRiGanXueTangMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 58);

  if MaskContainsBranch(
      kShenShaRiGanCiGuanMasks[StemOf(day_pillar)], target_branch) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 59);

  if IsNayinSchoolStage(year_pillar, target_ganzhi, 0) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 60);

  if IsNayinSchoolStage(year_pillar, target_ganzhi, 1) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 61);

  if IsOfficialSchoolStage(StemOf(day_pillar), target_ganzhi, 0) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 62);

  if IsOfficialSchoolStage(StemOf(day_pillar), target_ganzhi, 1) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 63);

  if IsOfficialStarSchool(StemOf(day_pillar), target_ganzhi) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 64);

  if IsNayinNoble(year_pillar, day_pillar, target_ganzhi) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 65);

  if MaskContainsGanzhi(kShenShaDiZhuanMasks[season], target_ganzhi) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 50);

  if MaskContainsGanzhi(kShenShaTianZhuanMasks[season], target_ganzhi) then
    DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 51);

  if target_kind = kShenShaTargetDay then
  begin
    if MaskContainsGanzhi(kShenShaTianSheDayMasks[season], target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 26);
    if MaskContainsGanzhi(kShenShaKuiGangGanzhiMask, target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 38);
    if MaskContainsGanzhi(kShenShaShiLingDayGanzhiMask, target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 39);
    if MaskContainsGanzhi(kShenShaBaZhuanDayGanzhiMask, target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 40);
    if MaskContainsGanzhi(kShenShaLiuXiuDayGanzhiMask, target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 41);
    if MaskContainsGanzhi(kShenShaJiuChouDayGanzhiMask, target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 42);
    if MaskContainsGanzhi(kShenShaSiFeiDayMasks[season], target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 43);
    if MaskContainsGanzhi(kShenShaShiEDaBaiGanzhiMask, target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 44);
    if MaskContainsGanzhi(kShenShaYinChaYangCuoGanzhiMask, target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 46);
    if MaskContainsGanzhi(kShenShaGuLuanShaGanzhiMask, target_ganzhi) then
      DynamicBitSetSetBit(out_words, kShenShaStableIdCount, 47);
  end;
  Result := 0;
end;

function taiyin_bazi_rules_collect_shen_sha(
  pillars: PByte;
  target_ganzhi: Byte;
  target_kind: LongInt;
  out_words: PQWord;
  word_capacity: PtrUInt;
  out_word_count: PPtrUInt
): LongInt; cdecl;
begin
  Result := CollectShenShaImpl(
    pillars, target_ganzhi, target_kind, -1,
    out_words, word_capacity, out_word_count);
end;

function taiyin_bazi_rules_collect_shen_sha_with_gender(
  pillars: PByte;
  target_ganzhi: Byte;
  target_kind, gender: LongInt;
  out_words: PQWord;
  word_capacity: PtrUInt;
  out_word_count: PPtrUInt
): LongInt; cdecl;
begin
  if (gender < 0) or (gender > 1) then
  begin
    if out_word_count <> nil then out_word_count^ := 0;
    Result := -1;
    Exit;
  end;
  Result := CollectShenShaImpl(
    pillars, target_ganzhi, target_kind, gender,
    out_words, word_capacity, out_word_count);
end;

end.
