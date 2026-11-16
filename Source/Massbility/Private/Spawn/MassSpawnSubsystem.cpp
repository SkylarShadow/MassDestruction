// Copyright MassDestruction. All Rights Reserved.

// Spawn 子系统负责请求校验、批量消费、工厂调用和失败统计。

#include "Spawn/MassSpawnSubsystem.h"

#include "Core/MassRuntimeSettings.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassUnitDefinition.h"

// 初始化生成依赖和请求队列容量。
void UMassSpawnSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	MassSubsystem = Collection.InitializeDependency<UMassRuntimeSubsystem>();
	const UMassRuntimeSettings* Settings = GetDefault<UMassRuntimeSettings>();
	MaxPendingRequests = Settings ? FMath::Max(1, Settings->MaxPendingSpawnRequests) : 2048;
	MaxSpawnsPerTick = Settings ? FMath::Max(1, Settings->MaxSpawnsPerSimulationTick) : 256;
	PendingRequests.Reserve(FMath::Min(MaxPendingRequests, 256));
	CompletedResults.Reserve(FMath::Min(MaxSpawnsPerTick, 256));
}

// 清空未消费的生成请求和统计数据。
void UMassSpawnSubsystem::Deinitialize()
{
	PendingRequests.Reset();
	CompletedResults.Reset();
	PendingRequestHead = 0;
	Stats = {};
	MassSubsystem = nullptr;
	Super::Deinitialize();
}

// 判断当前 World 是否支持 Mass Spawn。
bool UMassSpawnSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

// 根据上下文对象取得对应 World 的 Spawn Subsystem。
UMassSpawnSubsystem* UMassSpawnSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassSpawnSubsystem>();
	}
	return nullptr;
}

bool UMassSpawnSubsystem::QueueSpawn(const FMassSpawnRequest& Request)
{
	// 校验单个生成请求后入队，不在调用方立即创建实体。
	return QueueSpawns(MakeArrayView(&Request, 1));
}

bool UMassSpawnSubsystem::QueueSpawns(const TConstArrayView<FMassSpawnRequest> Requests)
{
	// 批量入队接口；任一请求非法时拒绝整批，保持调用方可预测。
	if (!IsInGameThread())
	{
		RecordFailure(EMassSpawnFailure::WrongThread, FMath::Max(1, Requests.Num()));
		return false;
	}
	if (Requests.IsEmpty())
	{
		RecordFailure(EMassSpawnFailure::InvalidRequest);
		return false;
	}
	if (Requests.Num() > MaxPendingRequests - GetPendingRequestCount())
	{
		RecordFailure(EMassSpawnFailure::Capacity, Requests.Num());
		return false;
	}
	for (const FMassSpawnRequest& Request : Requests)
	{
		if (!Request.UnitDefinition || Request.Transform.ContainsNaN())
		{
			RecordFailure(EMassSpawnFailure::InvalidRequest, Requests.Num());
			return false;
		}
	}

	const uint32 CurrentTick = MassSubsystem
		? MassSubsystem->GetSimulationClock().GetCurrentTick() : 0;
	for (const FMassSpawnRequest& Request : Requests)
	{
		FMassSpawnRequest& Queued = PendingRequests.Add_GetRef(Request);
		Queued.QueuedSimulationTick = CurrentTick;
	}
	Stats.RequestsQueued += static_cast<uint64>(Requests.Num());
	Stats.QueueHighWaterMark = FMath::Max(Stats.QueueHighWaterMark,
		static_cast<uint64>(GetPendingRequestCount()));
	return true;
}

void UMassSpawnSubsystem::FlushRequests(const uint32 CurrentTick)
{
	// 消费本 Tick 请求并通过 Entity Factory 创建实体。
	CompletedResults.Reset();
	if (!MassSubsystem)
	{
		return;
	}

	int32 RequestsToProcess = 0;
	const int32 PendingCount = GetPendingRequestCount();
	while (RequestsToProcess < MaxSpawnsPerTick && RequestsToProcess < PendingCount
		&& PendingRequests[PendingRequestHead + RequestsToProcess].QueuedSimulationTick < CurrentTick)
	{
		++RequestsToProcess;
	}
	CompletedResults.Reserve(RequestsToProcess);
	for (int32 Index = 0; Index < RequestsToProcess; ++Index)
	{
		const FMassSpawnRequest& Request = PendingRequests[PendingRequestHead++];
		FMassSpawnResult& Result = CompletedResults.AddDefaulted_GetRef();
		Result.RequestId = Request.RequestId;
		Result.SimulationTick = CurrentTick;
		if (!Request.UnitDefinition)
		{
			Result.Failure = EMassSpawnFailure::InvalidRequest;
			RecordFailure(Result.Failure);
			continue;
		}

		Result.Entity = MassSubsystem->CreateUnit(*Request.UnitDefinition, Request.Transform,
			Request.TeamIdOverride);
		if (!Result.Entity.IsSet())
		{
			Result.Failure = EMassSpawnFailure::CreateFailed;
			RecordFailure(Result.Failure);
			continue;
		}
		++Stats.RequestsCreated;
	}

	const int32 DeferredCount = GetPendingRequestCount();
	Stats.RequestsDeferred += static_cast<uint64>(DeferredCount);
	CompactConsumedRequests();
}

// 累加指定生成失败原因的统计数量。
void UMassSpawnSubsystem::RecordFailure(const EMassSpawnFailure Failure, const uint64 Count)
{
	const uint8 Index = static_cast<uint8>(Failure);
	if (Failure != EMassSpawnFailure::None && Index < Stats.Failures.Num())
	{
		Stats.RequestsRejected += Count;
		Stats.Failures[Index] += Count;
	}
}

// 移除已经消费的请求，压缩队列以便后续继续使用。
void UMassSpawnSubsystem::CompactConsumedRequests()
{
	if (PendingRequestHead == PendingRequests.Num())
	{
		PendingRequests.Reset();
		PendingRequestHead = 0;
	}
	else if (PendingRequestHead > 0 && PendingRequestHead >= PendingRequests.Num() / 2)
	{
		PendingRequests.RemoveAt(0, PendingRequestHead, EAllowShrinking::No);
		PendingRequestHead = 0;
	}
}
