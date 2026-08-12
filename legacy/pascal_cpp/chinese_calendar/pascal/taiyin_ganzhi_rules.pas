unit taiyin_ganzhi_rules;

{$mode objfpc}{$H+}

{$IF DEFINED(DARWIN) OR (DEFINED(MSWINDOWS) AND DEFINED(CPUI386))}
  {$DEFINE TAIYIN_GANZHI_LEADING_UNDERSCORE}
{$ENDIF}

interface

function IsValidStem(stem_id: Byte): Boolean; inline;
function IsValidBranch(branch_id: Byte): Boolean; inline;
function StemOf(value: Byte): Byte; inline;
function BranchOf(value: Byte): Byte; inline;
function IsValidGanzhi(value: Byte): Boolean; inline;
function MakeGanzhi(stem_id, branch_id: Byte; out_value: PByte): LongInt;
function GanzhiIndex(value: Byte; out_index: PLongInt): LongInt;
function AdvanceGanzhi(value: Byte; delta: LongInt; out_value: PByte): LongInt;

function taiyin_ganzhi_rules_make(
  stem_id, branch_id: Byte;
  out_value: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_GANZHI_LEADING_UNDERSCORE}
  name '_taiyin_ganzhi_rules_make';
{$ELSE}
  name 'taiyin_ganzhi_rules_make';
{$ENDIF}

function taiyin_ganzhi_rules_advance(
  value: Byte;
  delta: LongInt;
  out_value: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_GANZHI_LEADING_UNDERSCORE}
  name '_taiyin_ganzhi_rules_advance';
{$ELSE}
  name 'taiyin_ganzhi_rules_advance';
{$ENDIF}

function taiyin_ganzhi_rules_month(
  year_stem_id, month_index: Byte;
  out_value: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_GANZHI_LEADING_UNDERSCORE}
  name '_taiyin_ganzhi_rules_month';
{$ELSE}
  name 'taiyin_ganzhi_rules_month';
{$ENDIF}

function taiyin_ganzhi_rules_hour(
  day_stem_id, hour_index: Byte;
  out_value: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_GANZHI_LEADING_UNDERSCORE}
  name '_taiyin_ganzhi_rules_hour';
{$ELSE}
  name 'taiyin_ganzhi_rules_hour';
{$ENDIF}

function taiyin_ganzhi_rules_nayin_element(
  value: Byte;
  out_element_id: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_GANZHI_LEADING_UNDERSCORE}
  name '_taiyin_ganzhi_rules_nayin_element';
{$ELSE}
  name 'taiyin_ganzhi_rules_nayin_element';
{$ENDIF}

function taiyin_ganzhi_rules_nayin_id(
  value: Byte;
  out_nayin_id: PByte
): LongInt; cdecl; public
{$IFDEF TAIYIN_GANZHI_LEADING_UNDERSCORE}
  name '_taiyin_ganzhi_rules_nayin_id';
{$ELSE}
  name 'taiyin_ganzhi_rules_nayin_id';
{$ENDIF}

implementation

const
  { Direct numeric transcription of bazi_core NayinHelper._nayinWuXing. }
  kNayinElementBySexagenaryIndex: array[0..59] of Byte = (
    2, 2, 4, 4, 1, 1, 3, 3, 2, 2,
    4, 4, 0, 0, 3, 3, 2, 2, 1, 1,
    0, 0, 3, 3, 4, 4, 1, 1, 0, 0,
    2, 2, 4, 4, 1, 1, 3, 3, 2, 2,
    4, 4, 0, 0, 3, 3, 2, 2, 1, 1,
    0, 0, 3, 3, 4, 4, 1, 1, 0, 0
  );

function IsValidStem(stem_id: Byte): Boolean; inline;
begin
  Result := stem_id < 10;
end;

function IsValidBranch(branch_id: Byte): Boolean; inline;
begin
  Result := branch_id < 12;
end;

function StemOf(value: Byte): Byte; inline;
begin
  Result := value shr 4;
end;

function BranchOf(value: Byte): Byte; inline;
begin
  Result := value and $0F;
end;

function IsValidGanzhi(value: Byte): Boolean; inline;
begin
  Result := IsValidStem(StemOf(value)) and IsValidBranch(BranchOf(value))
    and ((StemOf(value) and 1) = (BranchOf(value) and 1));
end;

function MakeGanzhi(stem_id, branch_id: Byte; out_value: PByte): LongInt;
begin
  if (out_value = nil) or (not IsValidStem(stem_id)) or (not IsValidBranch(branch_id))
    or ((stem_id and 1) <> (branch_id and 1)) then
  begin
    Result := -1;
    Exit;
  end;
  out_value^ := (stem_id shl 4) or branch_id;
  Result := 0;
end;

function GanzhiIndex(value: Byte; out_index: PLongInt): LongInt;
var
  stem_id, branch_id: LongInt;
begin
  if (out_index = nil) or (not IsValidGanzhi(value)) then
  begin
    Result := -1;
    Exit;
  end;
  stem_id := StemOf(value);
  branch_id := BranchOf(value);
  { Direct transcription of sxwnl_spa_dart GanZhi.index. }
  out_index^ := (6 * stem_id - 5 * branch_id + 60) mod 60;
  Result := 0;
end;

function AdvanceGanzhi(value: Byte; delta: LongInt; out_value: PByte): LongInt;
var
  index, next_index: LongInt;
begin
  if out_value = nil then
  begin
    Result := -1;
    Exit;
  end;
  Result := GanzhiIndex(value, @index);
  if Result <> 0 then Exit;
  next_index := (index + (delta mod 60)) mod 60;
  if next_index < 0 then next_index := next_index + 60;
  out_value^ := ((next_index mod 10) shl 4) or (next_index mod 12);
end;

function taiyin_ganzhi_rules_make(
  stem_id, branch_id: Byte;
  out_value: PByte
): LongInt; cdecl;
begin
  Result := MakeGanzhi(stem_id, branch_id, out_value);
end;

function taiyin_ganzhi_rules_advance(
  value: Byte;
  delta: LongInt;
  out_value: PByte
): LongInt; cdecl;
begin
  Result := AdvanceGanzhi(value, delta, out_value);
end;

function taiyin_ganzhi_rules_month(
  year_stem_id, month_index: Byte;
  out_value: PByte
): LongInt; cdecl;
var
  start_stem_id, stem_id, branch_id: Byte;
begin
  if (not IsValidStem(year_stem_id)) or (month_index >= 12) then
  begin
    Result := -1;
    Exit;
  end;
  { Direct transcription of sxwnl_spa_dart monthGanZhi(): 0 is Yin month. }
  start_stem_id := ((year_stem_id mod 5) * 2 + 2) mod 10;
  stem_id := (start_stem_id + month_index) mod 10;
  branch_id := (month_index + 2) mod 12;
  Result := MakeGanzhi(stem_id, branch_id, out_value);
end;

function taiyin_ganzhi_rules_hour(
  day_stem_id, hour_index: Byte;
  out_value: PByte
): LongInt; cdecl;
var
  start_stem_id, stem_id: Byte;
begin
  if (not IsValidStem(day_stem_id)) or (hour_index >= 12) then
  begin
    Result := -1;
    Exit;
  end;
  { Direct transcription of sxwnl_spa_dart hourGanZhi(): 0 is Zi hour. }
  start_stem_id := (day_stem_id mod 5) * 2;
  stem_id := (start_stem_id + hour_index) mod 10;
  Result := MakeGanzhi(stem_id, hour_index, out_value);
end;

function taiyin_ganzhi_rules_nayin_element(
  value: Byte;
  out_element_id: PByte
): LongInt; cdecl;
var
  index: LongInt;
begin
  if out_element_id = nil then
  begin
    Result := -1;
    Exit;
  end;
  Result := GanzhiIndex(value, @index);
  if Result <> 0 then Exit;
  out_element_id^ := kNayinElementBySexagenaryIndex[index];
end;

function taiyin_ganzhi_rules_nayin_id(
  value: Byte;
  out_nayin_id: PByte
): LongInt; cdecl;
var
  index: LongInt;
begin
  if out_nayin_id = nil then
  begin
    Result := -1;
    Exit;
  end;
  Result := GanzhiIndex(value, @index);
  if Result <> 0 then Exit;
  out_nayin_id^ := index div 2;
end;

end.
