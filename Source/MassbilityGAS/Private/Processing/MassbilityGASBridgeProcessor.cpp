// Copyright MassDestruction. All Rights Reserved.

#include "Processing/MassbilityGASBridgeProcessor.h"

#include "Core/MassbilityGASSubsystem.h"
#include "MassExecutionContext.h"

UMassbilityGASProxySyncProcessor::UMassbilityGASProxySyncProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.GASProxySync"));
	ExecutionOrder.ExecuteBefore.Add(FName(TEXT("Massbility.SpatialBuild")));
}

bool UMassbilityGASProxySyncProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassbilityGASProxySyncProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UMassbilityGASProxySyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// ASC Avatar 的变换和拥有的 GameplayTag 必须在 SpatialBuild 前完成同步。
	if (UWorld* World = Context.GetWorld())
	{
		if (UMassbilityGASSubsystem* Subsystem = World->GetSubsystem<UMassbilityGASSubsystem>())
		{
			Subsystem->SyncEndpoints();
		}
	}
}

UMassbilityGASBridgeProcessor::UMassbilityGASBridgeProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.GASBridge"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.OutputDispatch")));
}

bool UMassbilityGASBridgeProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassbilityGASBridgeProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UMassbilityGASBridgeProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 在 Mass Dispatch 后消费输出请求，保持队列的单向所有权。
	if (UWorld* World = Context.GetWorld())
	{
		if (UMassbilityGASSubsystem* Subsystem = World->GetSubsystem<UMassbilityGASSubsystem>())
		{
			Subsystem->FlushOutboundRequests();
		}
	}
}
