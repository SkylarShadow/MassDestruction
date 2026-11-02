// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Spawn/MassSpawnTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassSpawnSubsystem.generated.h"

class UMassRuntimeSubsystem;

/**
 * 通用的单位生成队列。它只负责按预算创建已经决定好的单位，
 * 不包含 Encounter、波次、出生点采样、随机或项目玩法规则。
 */
UCLASS()
class MASSBILITY_API UMassSpawnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassSpawnSubsystem* Get(const UObject* WorldContextObject);

	/** Game-thread only。请求会在后续 Simulation Tick 按 FIFO 顺序消费。 */
	bool QueueSpawn(const FMassSpawnRequest& Request);
	/** Game-thread only。批次要么整体入队，要么整体拒绝，避免半批次语义。 */
	bool QueueSpawns(TConstArrayView<FMassSpawnRequest> Requests);

	/** 本 Tick 的结果快照会在下一次 Spawn Processor 执行前清空。 */
	const TArray<FMassSpawnResult>& GetCompletedResults() const { return CompletedResults; }
	const FMassSpawnStats& GetStats() const { return Stats; }
	int32 GetPendingRequestCount() const { return PendingRequests.Num() - PendingRequestHead; }

private:
	friend class UMassSpawnProcessor;

	void FlushRequests(uint32 CurrentTick);
	void RecordFailure(EMassSpawnFailure Failure, uint64 Count = 1);
	void CompactConsumedRequests();

	UPROPERTY(Transient)
	TObjectPtr<UMassRuntimeSubsystem> MassSubsystem;

	UPROPERTY(Transient)
	TArray<FMassSpawnRequest> PendingRequests;

	TArray<FMassSpawnResult> CompletedResults;
	int32 PendingRequestHead = 0;
	int32 MaxPendingRequests = 2048;
	int32 MaxSpawnsPerTick = 256;
	FMassSpawnStats Stats;
};
