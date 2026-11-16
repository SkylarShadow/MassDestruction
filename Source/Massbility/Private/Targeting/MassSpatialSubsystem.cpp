// Copyright MassDestruction. All Rights Reserved.

// Spatial 子系统维护二维空间哈希，用于快速查询附近实体。

#include "Targeting/MassSpatialSubsystem.h"
#include "DrawDebugHelpers.h"

namespace
{
	bool SweepUprightCapsule(const FVector& Start, const FVector& Delta, const FVector& Center,
		const float Radius, const float AxisHalfLength, float& OutTime, FVector& OutNormal)
	{
		const FVector Relative = Start - Center;
		const FVector AxisPoint(0.0f, 0.0f, FMath::Clamp(Relative.Z, -AxisHalfLength, AxisHalfLength));
		if ((Relative - AxisPoint).SizeSquared() <= FMath::Square(Radius))
		{
			OutTime = 0.0f;
			OutNormal = (Relative - AxisPoint).GetSafeNormal();
			return true;
		}

		float BestTime = 1.0f;
		bool bHit = false;
		auto TryCandidate = [&](const float Time, const FVector& Normal)
		{
			if (Time >= 0.0f && Time <= BestTime)
			{
				BestTime = Time;
				OutNormal = Normal.GetSafeNormal();
				bHit = true;
			}
		};

		const float PlanarA = FMath::Square(Delta.X) + FMath::Square(Delta.Y);
		if (PlanarA > UE_SMALL_NUMBER)
		{
			const float PlanarB = Relative.X * Delta.X + Relative.Y * Delta.Y;
			const float Discriminant = FMath::Square(PlanarB) - PlanarA
				* (FMath::Square(Relative.X) + FMath::Square(Relative.Y) - FMath::Square(Radius));
			if (Discriminant >= 0.0f)
			{
				const float Time = (-PlanarB - FMath::Sqrt(Discriminant)) / PlanarA;
				const float Z = Relative.Z + Delta.Z * Time;
				if (FMath::Abs(Z) <= AxisHalfLength)
				{
					TryCandidate(Time, FVector(Relative.X + Delta.X * Time, Relative.Y + Delta.Y * Time, 0.0f));
				}
			}
		}

		const float DirectionLengthSquared = Delta.SizeSquared();
		if (DirectionLengthSquared > UE_SMALL_NUMBER)
		{
			for (const float CapZ : {-AxisHalfLength, AxisHalfLength})
			{
				const FVector ToCap = Relative - FVector(0.0f, 0.0f, CapZ);
				const float B = FVector::DotProduct(ToCap, Delta);
				const float Discriminant = FMath::Square(B) - DirectionLengthSquared
					* (ToCap.SizeSquared() - FMath::Square(Radius));
				if (Discriminant >= 0.0f)
				{
					const float Time = (-B - FMath::Sqrt(Discriminant)) / DirectionLengthSquared;
					if ((Relative.Z + Delta.Z * Time - CapZ) * CapZ >= 0.0f || AxisHalfLength == 0.0f)
					{
						TryCandidate(Time, ToCap + Delta * Time);
					}
				}
			}
		}
		if (bHit)
		{
			OutTime = BestTime;
		}
		return bHit;
	}
}

bool UMassSpatialSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

bool UMassSpatialSubsystem::IsTickable() const
{
#if ENABLE_DRAW_DEBUG
	return bDrawHitShapes && MaxDebugHitShapes > 0 && !Entries.IsEmpty();
#else
	return false;
#endif
}

void UMassSpatialSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	int32 Drawn = 0;
	for (const FMassSpatialEntry& Entry : Entries)
	{
		if (Entry.HitRadius > 0.0f)
		{
			DrawDebugCapsule(World, Entry.Location, Entry.HitHalfHeight, Entry.HitRadius,
				FQuat::Identity, FColor::Cyan, false, 0.0f, 0, 1.0f);
			if (++Drawn >= MaxDebugHitShapes)
			{
				break;
			}
		}
	}
#endif
}

TStatId UMassSpatialSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMassSpatialSubsystem, STATGROUP_Tickables);
}

void UMassSpatialSubsystem::Deinitialize()
{
	Entries.Reset();
	Buckets.Reset();
	MaxHitRadius = 0.0f;
	bDrawHitShapes = false;
	Generation = 1;
	Stats = {};
	Super::Deinitialize();
}

void UMassSpatialSubsystem::SetHitShapeDebugDraw(const bool bEnabled, const int32 MaxShapes)
{
	bDrawHitShapes = bEnabled;
	MaxDebugHitShapes = FMath::Max(0, MaxShapes);
}

void UMassSpatialSubsystem::BeginBuild(const float InCellSize)
{
	// 开始新一轮空间索引构建，清空上一轮桶和实体引用。
	// 采用代数 Generation 清空 Bucket，避免每轮把整个哈希表逐项写零。
	CellSize = FMath::Max(50.0f, InCellSize);
	InverseCellSize = 1.0f / CellSize;
	const int32 PreviousCount = Entries.Num();
	Entries.Reset();
	MaxHitRadius = 0.0f;
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

bool UMassSpatialSubsystem::SweepHitShapes(const FVector& Start, const FVector& End, const float Radius,
	const FMassEntityHandle IgnoredEntity, FMassSpatialHit& OutHit) const
{
	OutHit = {};
	if (Buckets.IsEmpty() || Radius < 0.0f)
	{
		return false;
	}
	++Stats.Queries;
	const FVector Delta = End - Start;
	const float Padding = Radius + MaxHitRadius;
	const FIntPoint MinCell = ToCell(FVector(FMath::Min(Start.X, End.X) - Padding,
		FMath::Min(Start.Y, End.Y) - Padding, 0.0f));
	const FIntPoint MaxCell = ToCell(FVector(FMath::Max(Start.X, End.X) + Padding,
		FMath::Max(Start.Y, End.Y) + Padding, 0.0f));
	bool bFound = false;
	for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
	{
		for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
		{
			const int32 BucketIndex = FindBucket(X, Y);
			if (BucketIndex == INDEX_NONE)
			{
				continue;
			}
			for (int32 Index = Buckets[BucketIndex].FirstEntry; Entries.IsValidIndex(Index); Index = Entries[Index].NextEntry)
			{
				const FMassSpatialEntry& Entry = Entries[Index];
				++Stats.CandidatesTested;
				if (Entry.Entity == IgnoredEntity || Entry.HitRadius <= 0.0f)
				{
					continue;
				}
				float Time;
				FVector Normal;
				if (SweepUprightCapsule(Start, Delta, Entry.Location, Entry.HitRadius + Radius,
					Entry.HitHalfHeight - Entry.HitRadius, Time, Normal) && Time < OutHit.Time)
				{
					OutHit.Entity = Entry.Entity;
					OutHit.Time = Time;
					OutHit.ImpactNormal = Normal;
					OutHit.ImpactPoint = Start + Delta * Time - Normal * Radius;
					bFound = true;
				}
			}
		}
	}
	return bFound;
}

void UMassSpatialSubsystem::Add(const FMassEntityHandle Entity, const FVector& Location, const int16 TeamId,
	const FMassTagMask& RuntimeTags, const float HitRadius, const float HitHalfHeight)
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
	Entry.HitRadius = FMath::Max(0.0f, HitRadius);
	Entry.HitHalfHeight = FMath::Max(Entry.HitRadius, HitHalfHeight);
	MaxHitRadius = FMath::Max(MaxHitRadius, Entry.HitRadius);
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
	// 当预计元素数量超过容量时扩容，减少构建期间的重复分配。
	const int32 RequiredBuckets = FMath::RoundUpToPowerOfTwo(FMath::Max(64, RequiredEntries * 2));
	if (Buckets.Num() < RequiredBuckets)
	{
		Rehash(RequiredBuckets);
	}
}

void UMassSpatialSubsystem::Rehash(const int32 NewBucketCount)
{
	// 改变桶数量后重新插入已有条目，保持空间查询结果不变。
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
	// 通过开放寻址查找网格桶；bCreate 决定是否允许创建新桶。
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
