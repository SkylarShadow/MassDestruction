// Copyright MassDestruction. All Rights Reserved.

#include "Output/MassbilityOutputProcessor.h"

#include "Core/MassbilitySubsystem.h"
#include "MassExecutionContext.h"
#include "Output/MassbilityOutputSubsystem.h"

UMassbilityOutputProcessor::UMassbilityOutputProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.OutputDispatch"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.Ability")));
}

bool UMassbilityOutputProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassbilityOutputProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

void UMassbilityOutputProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassbilityOutputSubsystem* OutputSubsystem = World ? World->GetSubsystem<UMassbilityOutputSubsystem>() : nullptr;
	UMassbilitySubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassbilitySubsystem>() : nullptr;
	if (OutputSubsystem && CoreSubsystem)
	{
		OutputSubsystem->FlushRequests(EntityManager, CoreSubsystem->GetSimulationClock().GetCurrentTick());
	}
}

