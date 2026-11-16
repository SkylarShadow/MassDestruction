// Copyright MassDestruction. All Rights Reserved.

// Spawn Processor 在 Mass 阶段刷新生成请求，集中执行实体创建。

#include "Spawn/MassSpawnProcessor.h"

#include "Core/MassRuntimeSubsystem.h"
#include "MassCommandBuffer.h"
#include "MassCommands.h"
#include "MassExecutionContext.h"
#include "Spawn/MassSpawnSubsystem.h"

// 构造生成处理器并注册到 Spawn 执行阶段。
UMassSpawnProcessor::UMassSpawnProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.Spawn"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.Death")));
}

// 生成请求由全局队列驱动，不允许按实体查询裁剪。
bool UMassSpawnProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

// Spawn Processor 不需要遍历实体，因此查询保持为空。
void UMassSpawnProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

// 在当前 Tick 消费生成队列并创建新的 Mass Entity。
void UMassSpawnProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 在固定 Mass 阶段批量刷新 Spawn 请求，保证创建时序稳定。
	UWorld* World = Context.GetWorld();
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
	UMassSpawnSubsystem* SpawnSubsystem = World ? World->GetSubsystem<UMassSpawnSubsystem>() : nullptr;
	if (!CoreSubsystem || !SpawnSubsystem)
	{
		return;
	}
	const uint32 CurrentTick = CoreSubsystem->GetSimulationClock().GetCurrentTick();
	if (LastProcessedTick == CurrentTick)
	{
		return;
	}
	LastProcessedTick = CurrentTick;
	// 实体创建和组成变更必须等 Mass 结束处理后执行。
	const TWeakObjectPtr<UMassSpawnSubsystem> WeakSpawnSubsystem(SpawnSubsystem);
	Context.Defer().PushCommand<FMassDeferredCreateCommand>(
		[WeakSpawnSubsystem, CurrentTick](FMassEntityManager&)
		{
			if (UMassSpawnSubsystem* DeferredSubsystem = WeakSpawnSubsystem.Get())
			{
				DeferredSubsystem->FlushRequests(CurrentTick);
			}
		});
}
