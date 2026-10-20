// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassbilityTypes.h"
#include "Mass/EntityHandle.h"

struct MASSBILITY_API FMassbilitySpatialEntry
{
	// 空间索引中的一个候选目标。Entry 通过 NextEntry 串在对应 Bucket 的链表上。
	FMassEntityHandle Entity;
	// 使用 2D 平面位置；Z 轴不参与当前目标距离计算。
	FVector Location = FVector::ZeroVector;
	// 查询阶段用它做 Required/Blocked Tag 过滤，避免访问实体 Fragment。
	FMassbilityTagMask RuntimeTags;
	int32 NextEntry = INDEX_NONE;
	int16 TeamId = 0;
};

struct MASSBILITY_API FMassbilitySpatialBucket
{
	// 开放寻址哈希表中的一个格子。Generation 用于让 BeginBuild 快速逻辑清空表。
	int32 CellX = 0;
	int32 CellY = 0;
	int32 FirstEntry = INDEX_NONE;
	uint32 Generation = 0;
};

struct MASSBILITY_API FMassbilitySpatialStats
{
	// 当前索引构建后收集的规模和查询统计，用于性能诊断，不参与玩法逻辑。
	uint32 Entries = 0;
	uint32 ActiveBuckets = 0;
	uint64 Queries = 0;
	uint64 CandidatesTested = 0;
};
