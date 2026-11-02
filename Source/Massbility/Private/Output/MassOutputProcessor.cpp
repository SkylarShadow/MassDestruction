// Copyright MassDestruction. All Rights Reserved.

#include "Output/MassOutputProcessor.h"

#include "Core/MassRuntimeSubsystem.h"
#include "MassExecutionContext.h"
#include "Output/MassOutputSubsystem.h"

UMassOutputProcessor::UMassOutputProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.OutputDispatch"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.Ability")));
}

bool UMassOutputProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassOutputProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UMassOutputProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassOutputSubsystem* OutputSubsystem = World ? World->GetSubsystem<UMassOutputSubsystem>() : nullptr;
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
	if (OutputSubsystem && CoreSubsystem)
	{
		OutputSubsystem->FlushRequests(EntityManager, CoreSubsystem->GetSimulationClock().GetCurrentTick());
	}
}

