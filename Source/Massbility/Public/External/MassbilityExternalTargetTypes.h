// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassbilityTypes.h"

#include "MassbilityExternalTargetTypes.generated.h"

/**
 * 由外部战斗运行时持有的目标身份，并通过代数号进行有效性校验。
 * 注销后 Index 槽位可能被复用；Serial 用于防止旧句柄误解析到该槽位的新目标。
 */
USTRUCT(BlueprintType)
struct MASSBILITY_API FMassbilityExternalTargetHandle
{
	GENERATED_BODY()

	uint32 Index = MAX_uint32;
	uint32 Serial = 0;

	bool IsSet() const { return Index != MAX_uint32 && Serial != 0; }
	void Reset() { *this = {}; }

	friend bool operator==(const FMassbilityExternalTargetHandle& A, const FMassbilityExternalTargetHandle& B)
	{
		return A.Index == B.Index && A.Serial == B.Serial;
	}
};

/**
 * 从外部持有者复制到轻量级 Mass 代理实体的无 UObject 状态快照。
 * Mass 目标系统只读取这份快照，不直接访问外部 Actor 或 ASC。
 */
struct MASSBILITY_API FMassbilityExternalTargetSnapshot
{
	FTransform Transform = FTransform::Identity;
	FMassbilityTagMask RuntimeTags;
	int16 TeamId = 0;
	bool bTargetable = true;
};

struct MASSBILITY_API FMassbilityExternalTargetStats
{
	/** 用于诊断注册、同步和输出路由的生命周期统计。 */
	uint64 TargetsRegistered = 0;
	uint64 TargetsUnregistered = 0;
	uint64 SnapshotUpdates = 0;
	uint64 OutputRequestsQueued = 0;
	uint64 OutputRequestsRejectedInvalidTarget = 0;
	uint64 OutputRequestsRejectedNoRoute = 0;
};

