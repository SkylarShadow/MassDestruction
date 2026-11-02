// Copyright MassDestruction. All Rights Reserved.

#include "Spawn/MassSpawnProcessor.h"

#include "Core/MassRuntimeSubsystem.h"
#include "MassCommandBuffer.h"
#include "MassCommands.h"
#include "MassExecutionContext.h"
#include "Spawn/MassSpawnSubsystem.h"

UMassSpawnProcessor::UMassSpawnProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.Spawn"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.Death")));
}

bool UMassSpawnProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassSpawnProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UMassSpawnProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
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
