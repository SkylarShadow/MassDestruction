// Copyright MassDestruction. All Rights Reserved.

#include "Processing/MassGASBridgeProcessor.h"

#include "Core/MassGASSubsystem.h"
#include "MassExecutionContext.h"

UMassGASProxySyncProcessor::UMassGASProxySyncProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.GASProxySync"));
	ExecutionOrder.ExecuteBefore.Add(FName(TEXT("Massbility.SpatialBuild")));
}

bool UMassGASProxySyncProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassGASProxySyncProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UMassGASProxySyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// ASC Avatar 的变换和拥有的 GameplayTag 必须在 SpatialBuild 前完成同步。
	if (UWorld* World = Context.GetWorld())
	{
		if (UMassGASSubsystem* Subsystem = World->GetSubsystem<UMassGASSubsystem>())
		{
			Subsystem->SyncEndpoints();
		}
	}
}

UMassGASBridgeProcessor::UMassGASBridgeProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.GASBridge"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.OutputDispatch")));
}

bool UMassGASBridgeProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassGASBridgeProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UMassGASBridgeProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 在 Mass Dispatch 后消费输出请求，保持队列的单向所有权。
	if (UWorld* World = Context.GetWorld())
	{
		if (UMassGASSubsystem* Subsystem = World->GetSubsystem<UMassGASSubsystem>())
		{
			Subsystem->FlushOutboundRequests();
		}
	}
}
