// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Targeting/MassSpatialTypes.h"

#include "MassSpatialSubsystem.generated.h"

UCLASS()
class MASSBILITY_API UMassSpatialSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual bool IsTickable() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// 开始构建新一帧/新一轮快照。旧 Entry 不会继续参与查询。
	void BeginBuild(float InCellSize);
	// 将一个可被选中的 Mass 实体加入当前快照。
	void Add(FMassEntityHandle Entity, const FVector& Location, int16 TeamId, const FMassTagMask& RuntimeTags,
		float HitRadius, float HitHalfHeight);
	/** Sweep a sphere against query-only upright capsules. Radius 0 is a raycast. */
	bool SweepHitShapes(const FVector& Start, const FVector& End, float Radius,
		FMassEntityHandle IgnoredEntity, FMassSpatialHit& OutHit) const;
	/** Draw current query capsules every game frame. Debug drawing is disabled by default. */
	void SetHitShapeDebugDraw(bool bEnabled, int32 MaxShapes = 512);
	bool IsHitShapeDebugDrawEnabled() const { return bDrawHitShapes; }
	// 在 MaxRange 内查找最近的敌对实体；结果来自最近一次 BeginBuild 后的快照。
	bool FindNearestHostile(FMassEntityHandle Source, const FVector& Location, int16 TeamId,
		float MaxRange, const FMassTagMask& RequiredTags, const FMassTagMask& BlockedTags,
		FMassEntityHandle& OutEntity, FVector& OutLocation) const;
	int32 Num() const { return Entries.Num(); }
	const FMassSpatialStats& GetStats() const { return Stats; }

private:
	// 仅在容量不足时扩容；扩容会重建 Bucket 链表，但不会改变 Entry 数据。
	void EnsureCapacity(int32 RequiredEntries);
	void Rehash(int32 NewBucketCount);
	int32 FindBucket(int32 CellX, int32 CellY, bool bCreate);
	int32 FindBucket(int32 CellX, int32 CellY) const;
	FIntPoint ToCell(const FVector& Location) const;
	static uint32 HashCell(int32 CellX, int32 CellY);

	TArray<FMassSpatialEntry> Entries;
	TArray<FMassSpatialBucket> Buckets;
	float CellSize = 400.0f;
	float InverseCellSize = 1.0f / 400.0f;
	uint32 Generation = 1;
	mutable FMassSpatialStats Stats;
	float MaxHitRadius = 0.0f;
	int32 MaxDebugHitShapes = 512;
	bool bDrawHitShapes = false;
};
