// Copyright MassDestruction. All Rights Reserved.

#include "Targeting/MassSpatialSubsystem.h"

bool UMassSpatialSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UMassSpatialSubsystem::Deinitialize()
{
	Entries.Reset();
	Buckets.Reset();
	Generation = 1;
	Stats = {};
	Super::Deinitialize();
}

void UMassSpatialSubsystem::BeginBuild(const float InCellSize)
{
	// 采用代数 Generation 清空 Bucket，避免每轮把整个哈希表逐项写零。
	CellSize = FMath::Max(50.0f, InCellSize);
	InverseCellSize = 1.0f / CellSize;
	const int32 PreviousCount = Entries.Num();
	Entries.Reset();
	Stats = {};
	++Generation;
	if (Generation == 0)
	{
		for (FMassSpatialBucket& Bucket : Buckets)
		{
			Bucket.Generation = 0;
		}
		Generation = 1;
	}
	EnsureCapacity(FMath::Max(PreviousCount, 32));
}

void UMassSpatialSubsystem::Add(const FMassEntityHandle Entity, const FVector& Location, const int16 TeamId,
	const FMassTagMask& RuntimeTags)
{
	if (!Entity.IsSet())
	{
		return;
	}
	EnsureCapacity(Entries.Num() + 1);
	const FIntPoint Cell = ToCell(Location);
	const int32 BucketIndex = FindBucket(Cell.X, Cell.Y, true);
	if (BucketIndex == INDEX_NONE)
	{
		return;
	}
	FMassSpatialEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Entity = Entity;
	Entry.Location = Location;
	Entry.TeamId = TeamId;
	Entry.RuntimeTags = RuntimeTags;
	Entry.NextEntry = Buckets[BucketIndex].FirstEntry;
	Buckets[BucketIndex].FirstEntry = Entries.Num() - 1;
	Stats.Entries = static_cast<uint32>(Entries.Num());
}

bool UMassSpatialSubsystem::FindNearestHostile(const FMassEntityHandle Source, const FVector& Location,
	const int16 TeamId, const float MaxRange, const FMassTagMask& RequiredTags,
	const FMassTagMask& BlockedTags, FMassEntityHandle& OutEntity, FVector& OutLocation) const
{
	// 查询只遍历覆盖 MaxRange 的格子，最后用精确 2D 距离排除格子边界带来的候选误差。
	++Stats.Queries;
	OutEntity = {};
	OutLocation = FVector::ZeroVector;
	if (Buckets.IsEmpty() || MaxRange <= 0.0f)
	{
		return false;
	}
	const FIntPoint Center = ToCell(Location);
	const int32 Radius = FMath::CeilToInt(MaxRange * InverseCellSize);
	float BestDistanceSquared = FMath::Square(MaxRange);
	for (int32 Y = Center.Y - Radius; Y <= Center.Y + Radius; ++Y)
	{
		for (int32 X = Center.X - Radius; X <= Center.X + Radius; ++X)
		{
			const int32 BucketIndex = FindBucket(X, Y);
			if (BucketIndex == INDEX_NONE)
			{
				continue;
			}
			for (int32 EntryIndex = Buckets[BucketIndex].FirstEntry;
				Entries.IsValidIndex(EntryIndex); EntryIndex = Entries[EntryIndex].NextEntry)
			{
				const FMassSpatialEntry& Entry = Entries[EntryIndex];
				++Stats.CandidatesTested;
				if (Entry.Entity == Source || Entry.TeamId == TeamId
					|| !Entry.RuntimeTags.ContainsAll(RequiredTags)
					|| Entry.RuntimeTags.ContainsAny(BlockedTags))
				{
					continue;
				}
				const float DistanceSquared = FVector::DistSquared2D(Location, Entry.Location);
				if (DistanceSquared <= BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					OutEntity = Entry.Entity;
					OutLocation = Entry.Location;
				}
			}
		}
	}
	return OutEntity.IsSet();
}

void UMassSpatialSubsystem::EnsureCapacity(const int32 RequiredEntries)
{
	const int32 RequiredBuckets = FMath::RoundUpToPowerOfTwo(FMath::Max(64, RequiredEntries * 2));
	if (Buckets.Num() < RequiredBuckets)
	{
		Rehash(RequiredBuckets);
	}
}

void UMassSpatialSubsystem::Rehash(const int32 NewBucketCount)
{
	// Bucket 使用二次幂容量，FindBucket 可用位掩码代替取模。
	Buckets.SetNumZeroed(NewBucketCount);
	Stats.ActiveBuckets = 0;
	++Generation;
	if (Generation == 0)
	{
		Generation = 1;
	}
	for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
	{
		FMassSpatialEntry& Entry = Entries[EntryIndex];
		const FIntPoint Cell = ToCell(Entry.Location);
		const int32 BucketIndex = FindBucket(Cell.X, Cell.Y, true);
		Entry.NextEntry = Buckets[BucketIndex].FirstEntry;
		Buckets[BucketIndex].FirstEntry = EntryIndex;
	}
}

int32 UMassSpatialSubsystem::FindBucket(const int32 CellX, const int32 CellY, const bool bCreate)
{
	if (Buckets.IsEmpty())
	{
		return INDEX_NONE;
	}
	const uint32 Mask = static_cast<uint32>(Buckets.Num() - 1);
	uint32 Index = HashCell(CellX, CellY) & Mask;
	for (int32 Probe = 0; Probe < Buckets.Num(); ++Probe)
	{
		FMassSpatialBucket& Bucket = Buckets[Index];
		if (Bucket.Generation != Generation)
		{
			if (!bCreate)
			{
				return INDEX_NONE;
			}
			Bucket.CellX = CellX;
			Bucket.CellY = CellY;
			Bucket.FirstEntry = INDEX_NONE;
			Bucket.Generation = Generation;
			++Stats.ActiveBuckets;
			return static_cast<int32>(Index);
		}
		if (Bucket.CellX == CellX && Bucket.CellY == CellY)
		{
			return static_cast<int32>(Index);
		}
		Index = (Index + 1) & Mask;
	}
	return INDEX_NONE;
}

int32 UMassSpatialSubsystem::FindBucket(const int32 CellX, const int32 CellY) const
{
	if (Buckets.IsEmpty())
	{
		return INDEX_NONE;
	}
	const uint32 Mask = static_cast<uint32>(Buckets.Num() - 1);
	uint32 Index = HashCell(CellX, CellY) & Mask;
	for (int32 Probe = 0; Probe < Buckets.Num(); ++Probe)
	{
		const FMassSpatialBucket& Bucket = Buckets[Index];
		if (Bucket.Generation != Generation)
		{
			return INDEX_NONE;
		}
		if (Bucket.CellX == CellX && Bucket.CellY == CellY)
		{
			return static_cast<int32>(Index);
		}
		Index = (Index + 1) & Mask;
	}
	return INDEX_NONE;
}

FIntPoint UMassSpatialSubsystem::ToCell(const FVector& Location) const
{
	return FIntPoint(FMath::FloorToInt(Location.X * InverseCellSize), FMath::FloorToInt(Location.Y * InverseCellSize));
}

uint32 UMassSpatialSubsystem::HashCell(const int32 CellX, const int32 CellY)
{
	return HashCombineFast(GetTypeHash(CellX), GetTypeHash(CellY));
}
