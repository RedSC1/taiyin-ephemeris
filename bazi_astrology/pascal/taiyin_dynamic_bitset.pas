unit taiyin_dynamic_bitset;

{$mode objfpc}{$H+}

interface

{ Runtime-sized bitset operations over caller-owned 64-bit words. The unit has
  no managed Pascal storage, object RTTI, allocator, or exception dependency. }
function DynamicBitSetWordCount(BitCapacity: PtrUInt): PtrUInt; inline;
function DynamicBitSetStorageValid(
  Words: PQWord; WordCapacity, BitCapacity: PtrUInt): Boolean; inline;
procedure DynamicBitSetClear(Words: PQWord; BitCapacity: PtrUInt);
procedure DynamicBitSetSetBit(
  Words: PQWord; BitCapacity, Index: PtrUInt; Value: Boolean = True);
procedure DynamicBitSetClearBit(Words: PQWord; BitCapacity, Index: PtrUInt);
function DynamicBitSetTestBit(
  Words: PQWord; BitCapacity, Index: PtrUInt): Boolean;
procedure DynamicBitSetOr(
  Destination: PQWord; DestinationBits: PtrUInt;
  Source: PQWord; SourceBits: PtrUInt);
procedure DynamicBitSetAnd(
  Destination: PQWord; DestinationBits: PtrUInt;
  Source: PQWord; SourceBits: PtrUInt);
procedure DynamicBitSetAndNot(
  Destination: PQWord; DestinationBits: PtrUInt;
  Source: PQWord; SourceBits: PtrUInt);
procedure DynamicBitSetXor(
  Destination: PQWord; DestinationBits: PtrUInt;
  Source: PQWord; SourceBits: PtrUInt);
function DynamicBitSetContainsAll(
  Value: PQWord; ValueBits: PtrUInt;
  Required: PQWord; RequiredBits: PtrUInt): Boolean;
function DynamicBitSetCount(Words: PQWord; BitCapacity: PtrUInt): PtrUInt;
function DynamicBitSetNext(
  Words: PQWord; BitCapacity, StartIndex: PtrUInt; out Index: PtrUInt): Boolean;
function DynamicBitSetWordAt(
  Words: PQWord; BitCapacity, Index: PtrUInt): QWord; inline;

implementation

const
  kBitsPerWord = 64;

function WordPointer(Words: PQWord; Index: PtrUInt): PQWord; inline;
begin
  Result := PQWord(PtrUInt(Words) + Index * SizeOf(QWord));
end;

function LastWordMask(BitCapacity: PtrUInt): QWord; inline;
var
  remainder: PtrUInt;
begin
  remainder := BitCapacity mod kBitsPerWord;
  if remainder = 0 then Result := not QWord(0)
  else Result := (QWord(1) shl remainder) - 1;
end;

function DynamicBitSetWordCount(BitCapacity: PtrUInt): PtrUInt; inline;
begin
  if BitCapacity = 0 then Result := 0
  else Result := ((BitCapacity - 1) div kBitsPerWord) + 1;
end;

function DynamicBitSetStorageValid(
  Words: PQWord; WordCapacity, BitCapacity: PtrUInt
): Boolean; inline;
var
  required_words: PtrUInt;
begin
  required_words := DynamicBitSetWordCount(BitCapacity);
  Result := (required_words <= WordCapacity)
    and ((required_words = 0) or (Words <> nil));
end;

function DynamicBitSetWordAt(
  Words: PQWord; BitCapacity, Index: PtrUInt
): QWord; inline;
begin
  if (Words = nil) or (Index >= DynamicBitSetWordCount(BitCapacity)) then
    Result := 0
  else
  begin
    Result := WordPointer(Words, Index)^;
    if Index + 1 = DynamicBitSetWordCount(BitCapacity) then
      Result := Result and LastWordMask(BitCapacity);
  end;
end;

procedure DynamicBitSetClear(Words: PQWord; BitCapacity: PtrUInt);
var
  I, word_count: PtrUInt;
begin
  word_count := DynamicBitSetWordCount(BitCapacity);
  if (Words = nil) or (word_count = 0) then Exit;
  for I := 0 to word_count - 1 do WordPointer(Words, I)^ := 0;
end;

procedure DynamicBitSetSetBit(
  Words: PQWord; BitCapacity, Index: PtrUInt; Value: Boolean
);
var
  word_value: PQWord;
  mask: QWord;
begin
  if (Words = nil) or (Index >= BitCapacity) then Exit;
  word_value := WordPointer(Words, Index div kBitsPerWord);
  mask := QWord(1) shl (Index mod kBitsPerWord);
  if Value then word_value^ := word_value^ or mask
  else word_value^ := word_value^ and not mask;
end;

procedure DynamicBitSetClearBit(Words: PQWord; BitCapacity, Index: PtrUInt);
begin
  DynamicBitSetSetBit(Words, BitCapacity, Index, False);
end;

function DynamicBitSetTestBit(
  Words: PQWord; BitCapacity, Index: PtrUInt
): Boolean;
begin
  Result := (Index < BitCapacity)
    and ((DynamicBitSetWordAt(Words, BitCapacity, Index div kBitsPerWord)
      and (QWord(1) shl (Index mod kBitsPerWord))) <> 0);
end;

procedure DynamicBitSetOr(
  Destination: PQWord; DestinationBits: PtrUInt;
  Source: PQWord; SourceBits: PtrUInt
);
var
  I, limit: PtrUInt;
begin
  limit := DynamicBitSetWordCount(DestinationBits);
  if DynamicBitSetWordCount(SourceBits) < limit then
    limit := DynamicBitSetWordCount(SourceBits);
  if (Destination = nil) or (Source = nil) or (limit = 0) then Exit;
  for I := 0 to limit - 1 do
    WordPointer(Destination, I)^ := WordPointer(Destination, I)^ or
      DynamicBitSetWordAt(Source, SourceBits, I);
  WordPointer(Destination, DynamicBitSetWordCount(DestinationBits) - 1)^ :=
    WordPointer(Destination, DynamicBitSetWordCount(DestinationBits) - 1)^ and
    LastWordMask(DestinationBits);
end;

procedure DynamicBitSetAnd(
  Destination: PQWord; DestinationBits: PtrUInt;
  Source: PQWord; SourceBits: PtrUInt
);
var
  I, destination_words: PtrUInt;
begin
  destination_words := DynamicBitSetWordCount(DestinationBits);
  if (Destination = nil) or (destination_words = 0) then Exit;
  for I := 0 to destination_words - 1 do
    WordPointer(Destination, I)^ := WordPointer(Destination, I)^ and
      DynamicBitSetWordAt(Source, SourceBits, I);
end;

procedure DynamicBitSetAndNot(
  Destination: PQWord; DestinationBits: PtrUInt;
  Source: PQWord; SourceBits: PtrUInt
);
var
  I, limit: PtrUInt;
begin
  limit := DynamicBitSetWordCount(DestinationBits);
  if DynamicBitSetWordCount(SourceBits) < limit then
    limit := DynamicBitSetWordCount(SourceBits);
  if (Destination = nil) or (Source = nil) or (limit = 0) then Exit;
  for I := 0 to limit - 1 do
    WordPointer(Destination, I)^ := WordPointer(Destination, I)^ and not
      DynamicBitSetWordAt(Source, SourceBits, I);
end;

procedure DynamicBitSetXor(
  Destination: PQWord; DestinationBits: PtrUInt;
  Source: PQWord; SourceBits: PtrUInt
);
var
  I, limit: PtrUInt;
begin
  limit := DynamicBitSetWordCount(DestinationBits);
  if DynamicBitSetWordCount(SourceBits) < limit then
    limit := DynamicBitSetWordCount(SourceBits);
  if (Destination = nil) or (Source = nil) or (limit = 0) then Exit;
  for I := 0 to limit - 1 do
    WordPointer(Destination, I)^ := WordPointer(Destination, I)^ xor
      DynamicBitSetWordAt(Source, SourceBits, I);
  WordPointer(Destination, DynamicBitSetWordCount(DestinationBits) - 1)^ :=
    WordPointer(Destination, DynamicBitSetWordCount(DestinationBits) - 1)^ and
    LastWordMask(DestinationBits);
end;

function DynamicBitSetContainsAll(
  Value: PQWord; ValueBits: PtrUInt;
  Required: PQWord; RequiredBits: PtrUInt
): Boolean;
var
  I, required_words: PtrUInt;
  required_word: QWord;
begin
  required_words := DynamicBitSetWordCount(RequiredBits);
  if required_words = 0 then
  begin
    Result := True;
    Exit;
  end;
  if Required = nil then
  begin
    Result := False;
    Exit;
  end;
  for I := 0 to required_words - 1 do
  begin
    required_word := DynamicBitSetWordAt(Required, RequiredBits, I);
    if (DynamicBitSetWordAt(Value, ValueBits, I) and required_word)
      <> required_word then
    begin
      Result := False;
      Exit;
    end;
  end;
  Result := True;
end;

function DynamicBitSetCount(Words: PQWord; BitCapacity: PtrUInt): PtrUInt;
var
  I, word_count: PtrUInt;
  word_value: QWord;
begin
  Result := 0;
  word_count := DynamicBitSetWordCount(BitCapacity);
  if (Words = nil) or (word_count = 0) then Exit;
  for I := 0 to word_count - 1 do
  begin
    word_value := DynamicBitSetWordAt(Words, BitCapacity, I);
    while word_value <> 0 do
    begin
      Inc(Result);
      word_value := word_value and (word_value - 1);
    end;
  end;
end;

function DynamicBitSetNext(
  Words: PQWord; BitCapacity, StartIndex: PtrUInt; out Index: PtrUInt
): Boolean;
var
  word_index, bit_index, word_count: PtrUInt;
  word_value: QWord;
begin
  if (Words = nil) or (StartIndex >= BitCapacity) then
  begin
    Result := False;
    Exit;
  end;
  word_count := DynamicBitSetWordCount(BitCapacity);
  word_index := StartIndex div kBitsPerWord;
  bit_index := StartIndex mod kBitsPerWord;
  word_value := WordPointer(Words, word_index)^ and
    (not QWord(0) shl bit_index);
  while True do
  begin
    if word_value <> 0 then
    begin
      bit_index := 0;
      while (word_value and (QWord(1) shl bit_index)) = 0 do Inc(bit_index);
      Index := word_index * kBitsPerWord + bit_index;
      Result := Index < BitCapacity;
      Exit;
    end;
    Inc(word_index);
    if word_index >= word_count then Break;
    word_value := WordPointer(Words, word_index)^;
  end;
  Result := False;
end;

end.
