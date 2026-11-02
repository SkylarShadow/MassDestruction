// Copyright MassDestruction. All Rights Reserved.

#include "Movement/MassForcedMovementProcessor.h"

#include "Core/MassRuntimeSubsystem.h"
#include "Entity/MassFragments.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"
#include "Movement/MassForcedMovementFragments.h"
#include "Movement/MassForcedMovementSubsystem.h"

UMassForcedMovementProcessor::UMassForcedMovementProcessor()
	: MovementQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.ForcedMovement"));
	ExecutionOrder.ExecuteBefore.Add(FName(TEXT("Massbility.SpatialBuild")));
}

bool UMassForcedMovementProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassForcedMovementProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	MovementQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadOnly);
	MovementQuery.AddRequirement<FMassForcedMovementFragment>(EMassFragmentAccess::ReadWrite);
	MovementQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	MovementQuery.AddTagRequirement<FMassHasForcedMovementTag>(EMassFragmentPresence::All);
}

void UMassForcedMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
	UMassForcedMovementSubsystem* MovementSubsystem =
		World ? World->GetSubsystem<UMassForcedMovementSubsystem>() : nullptr;
	if (!CoreSubsystem || !MovementSubsystem)
	{
		return;
	}
	const FMassSimulationClock& Clock = CoreSubsystem->GetSimulationClock();
	const uint32 CurrentTick = Clock.GetCurrentTick();
	if (LastProcessedTick == CurrentTick)
	{
		return;
	}
	LastProcessedTick = CurrentTick;
	MovementSubsystem->Stats.Active = 0;
	MovementSubsystem->FlushRequests(EntityManager, Context, CurrentTick);
	const float TickSeconds = 1.0f / static_cast<float>(Clock.GetTicksPerSecond());
	MovementQuery.ForEachEntityChunk(Context,
		[MovementSubsystem, CurrentTick, TickSeconds](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassStateFragment> States =
			ChunkContext.GetFragmentView<FMassStateFragment>();
		TArrayView<FMassForcedMovementFragment> Movements =
			ChunkContext.GetMutableFragmentView<FMassForcedMovementFragment>();
		TArrayView<FTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			const FMassEntityHandle Entity = ChunkContext.GetEntity(It);
			if (!MovementSubsystem->TickEntity(Entity, CurrentTick, TickSeconds,
				States[It], Transforms[It], Movements[It]))
			{
				ChunkContext.Defer().RemoveTag<FMassHasForcedMovementTag>(Entity);
			}
		}
	});
}
