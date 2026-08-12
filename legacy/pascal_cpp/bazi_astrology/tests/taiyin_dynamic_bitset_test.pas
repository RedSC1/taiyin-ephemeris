program taiyin_dynamic_bitset_test;

{$mode objfpc}{$H+}

uses taiyin_dynamic_bitset;

procedure Require(Condition: Boolean; const Message: ShortString);
begin
  if not Condition then
  begin
    WriteLn('FAIL: ', Message);
    Halt(1);
  end;
end;

var
  WordsA, WordsB, WordsC: array[0..3] of QWord;
  Index: PtrUInt;
begin
  Require(DynamicBitSetStorageValid(@WordsA[0], 4, 130), 'initialize A');
  Require(DynamicBitSetStorageValid(@WordsB[0], 4, 200), 'initialize B');
  Require(DynamicBitSetStorageValid(@WordsC[0], 4, 200), 'initialize C');
  DynamicBitSetClear(@WordsA[0], 130);
  DynamicBitSetClear(@WordsB[0], 200);
  DynamicBitSetClear(@WordsC[0], 200);

  DynamicBitSetSetBit(@WordsA[0], 130, 0);
  DynamicBitSetSetBit(@WordsA[0], 130, 63);
  DynamicBitSetSetBit(@WordsA[0], 130, 64);
  DynamicBitSetSetBit(@WordsA[0], 130, 129);
  WordsA[2] := WordsA[2] or (QWord(1) shl 63);
  Require(DynamicBitSetTestBit(@WordsA[0], 130, 0)
    and DynamicBitSetTestBit(@WordsA[0], 130, 63)
    and DynamicBitSetTestBit(@WordsA[0], 130, 64)
    and DynamicBitSetTestBit(@WordsA[0], 130, 129),
    '64-bit word boundaries');
  Require(DynamicBitSetCount(@WordsA[0], 130) = 4, 'count');
  Require(DynamicBitSetNext(@WordsA[0], 130, 1, Index) and (Index = 63),
    'next set bit');
  Require(DynamicBitSetNext(@WordsA[0], 130, 64, Index) and (Index = 64),
    'next set bit at boundary');
  Require(not DynamicBitSetTestBit(@WordsA[0], 130, 130), 'out-of-range read');

  DynamicBitSetSetBit(@WordsB[0], 200, 1);
  DynamicBitSetSetBit(@WordsB[0], 200, 64);
  DynamicBitSetSetBit(@WordsB[0], 200, 199);
  DynamicBitSetOr(@WordsC[0], 200, @WordsA[0], 130);
  DynamicBitSetOr(@WordsC[0], 200, @WordsB[0], 200);
  Require(DynamicBitSetTestBit(@WordsC[0], 200, 1)
    and DynamicBitSetTestBit(@WordsC[0], 200, 199)
    and not DynamicBitSetTestBit(@WordsC[0], 200, 191),
    'or ignores source padding');
  Require(DynamicBitSetContainsAll(@WordsC[0], 200, @WordsA[0], 130),
    'contains all after or');
  DynamicBitSetAndNot(@WordsC[0], 200, @WordsB[0], 200);
  Require(DynamicBitSetTestBit(@WordsC[0], 200, 0)
    and DynamicBitSetTestBit(@WordsC[0], 200, 129)
    and not DynamicBitSetTestBit(@WordsC[0], 200, 64), 'and-not');
  DynamicBitSetSetBit(@WordsC[0], 200, 191);
  DynamicBitSetAndNot(@WordsC[0], 200, @WordsA[0], 130);
  Require(DynamicBitSetTestBit(@WordsC[0], 200, 191),
    'and-not ignores source padding');
  DynamicBitSetClear(@WordsC[0], 200);
  DynamicBitSetOr(@WordsC[0], 200, @WordsA[0], 130);
  DynamicBitSetXor(@WordsC[0], 200, @WordsA[0], 130);
  Require(DynamicBitSetCount(@WordsC[0], 200) = 0, 'xor');
  DynamicBitSetSetBit(@WordsC[0], 200, 191);
  DynamicBitSetXor(@WordsC[0], 200, @WordsA[0], 130);
  Require(DynamicBitSetTestBit(@WordsC[0], 200, 191),
    'xor ignores source padding');

  Require(DynamicBitSetWordCount(0) = 0, 'zero word count');
  Require(DynamicBitSetWordCount(130) = 3, 'runtime word count');
  WordsA[0] := not QWord(0);
  WordsA[1] := not QWord(0);
  WordsA[2] := not QWord(0);
  Require(DynamicBitSetCount(@WordsA[0], 130) = 130,
    'unused tail bits are outside the bitset');
  Require(not DynamicBitSetStorageValid(nil, 0, 1), 'reject missing storage');
  Require(not DynamicBitSetStorageValid(@WordsC[0], 1, 65),
    'reject short storage');
  WriteLn('taiyin_dynamic_bitset_test: OK');
end.
