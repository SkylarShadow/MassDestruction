// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"

#include "MassSpawnTypes.generated.h"

class UMassUnitDefinition;

/** 生成请求未被接受或执行失败的原因。 */
UENUM()
enum class EMassSpawnFailure : uint8
{
	None,
	WrongThread,
	InvalidRequest,
	Capacity,
	CreateFailed,
	MAX UMETA(Hidden),
};

/**
 * 由业务层、MassSpawner Adapter 或测试工具提交的单个单位生成请求。
 * 请求不表达波次、出生点或触发规则；调用方必须已经决定具体 Definition 与 Transform。
 */
USTRUCT()
struct MASSBILITY_API FMassSpawnRequest
{
	GENERATED_BODY()

	/** 已加载的单位定义。框架不会在模拟热路径同步加载软引用。 */
	UPROPERTY(Transient)
	TObjectPtr<UMassUnitDefinition> UnitDefinition = nullptr;

	/** 新实体的初始世界变换。 */
	FTransform Transform = FTransform::Identity;

	/** INDEX_NONE 表示使用 UnitDefinition 的默认阵营；否则覆盖初始 TeamId。 */
	int16 TeamIdOverride = INDEX_NONE;

	/** 调用方自定义的关联值，框架只在结果中原样返回。 */
	uint32 RequestId = 0;

	/** 仅由 Spawn Subsystem 写入，记录请求被接受时的模拟 Tick。 */
	uint32 QueuedSimulationTick = 0;
};

/** 已在本 Simulation Tick 完成的生成结果快照。 */
USTRUCT()
struct MASSBILITY_API FMassSpawnResult
{
	GENERATED_BODY()

	uint32 RequestId = 0;
	FMassEntityHandle Entity;
	EMassSpawnFailure Failure = EMassSpawnFailure::None;
	uint32 SimulationTick = 0;

	bool Succeeded() const
	{
		return Failure == EMassSpawnFailure::None && Entity.IsSet();
	}
};

/** 仅记录队列和创建路径状态；不包含业务层波次或 Encounter 统计。 */
USTRUCT()
struct MASSBILITY_API FMassSpawnStats
{
	GENERATED_BODY()

	uint64 RequestsQueued = 0;
	uint64 RequestsCreated = 0;
	uint64 RequestsRejected = 0;
	uint64 RequestsDeferred = 0;
	uint64 QueueHighWaterMark = 0;
	TStaticArray<uint64, static_cast<uint8>(EMassSpawnFailure::MAX)> Failures;
};
