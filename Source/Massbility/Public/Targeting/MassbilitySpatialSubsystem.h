// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Targeting/MassbilitySpatialTypes.h"

#include "MassbilitySpatialSubsystem.generated.h"

UCLASS()
class MASSBILITY_API UMassbilitySpatialSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	// 开始构建新一帧/新一轮快照。旧 Entry 不会继续参与查询。
	void BeginBuild(float InCellSize);
	// 将一个可被选中的 Mass 实体加入当前快照。
	void Add(FMassEntityHandle Entity, const FVector& Location, int16 TeamId, const FMassbilityTagMask& RuntimeTags);
	// 在 MaxRange 内查找最近的敌对实体；结果来自最近一次 BeginBuild 后的快照。
	bool FindNearestHostile(FMassEntityHandle Source, const FVector& Location, int16 TeamId,
		float MaxRange, const FMassbilityTagMask& RequiredTags, const FMassbilityTagMask& BlockedTags,
		FMassEntityHandle& OutEntity, FVector& OutLocation) const;
	int32 Num() const { return Entries.Num(); }
	const FMassbilitySpatialStats& GetStats() const { return Stats; }

private:
	// 仅在容量不足时扩容；扩容会重建 Bucket 链表，但不会改变 Entry 数据。
	void EnsureCapacity(int32 RequiredEntries);
	void Rehash(int32 NewBucketCount);
	int32 FindBucket(int32 CellX, int32 CellY, bool bCreate);
	int32 FindBucket(int32 CellX, int32 CellY) const;
	FIntPoint ToCell(const FVector& Location) const;
	static uint32 HashCell(int32 CellX, int32 CellY);

	TArray<FMassbilitySpatialEntry> Entries;
	TArray<FMassbilitySpatialBucket> Buckets;
	float CellSize = 400.0f;
	float InverseCellSize = 1.0f / 400.0f;
	uint32 Generation = 1;
	mutable FMassbilitySpatialStats Stats;
};
