// Copyright MassDestruction. All Rights Reserved.

#include "Movement/MassbilityForcedMovementProcessor.h"

#include "Core/MassbilitySubsystem.h"
#include "Entity/MassbilityFragments.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"
#include "Movement/MassbilityForcedMovementFragments.h"
#include "Movement/MassbilityForcedMovementSubsystem.h"

UMassbilityForcedMovementProcessor::UMassbilityForcedMovementProcessor()
	: MovementQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.ForcedMovement"));
	ExecutionOrder.ExecuteBefore.Add(FName(TEXT("Massbility.SpatialBuild")));
}

bool UMassbilityForcedMovementProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassbilityForcedMovementProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	MovementQuery.AddRequirement<FMassbilityStateFragment>(EMassFragmentAccess::ReadOnly);
	MovementQuery.AddRequirement<FMassbilityForcedMovementFragment>(EMassFragmentAccess::ReadWrite);
	MovementQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	MovementQuery.AddTagRequirement<FMassbilityHasForcedMovementTag>(EMassFragmentPresence::All);
}

void UMassbilityForcedMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassbilitySubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassbilitySubsystem>() : nullptr;
	UMassbilityForcedMovementSubsystem* MovementSubsystem =
		World ? World->GetSubsystem<UMassbilityForcedMovementSubsystem>() : nullptr;
	if (!CoreSubsystem || !MovementSubsystem)
	{
		return;
	}
	const FMassbilitySimulationClock& Clock = CoreSubsystem->GetSimulationClock();
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
		const TConstArrayView<FMassbilityStateFragment> States =
			ChunkContext.GetFragmentView<FMassbilityStateFragment>();
		TArrayView<FMassbilityForcedMovementFragment> Movements =
			ChunkContext.GetMutableFragmentView<FMassbilityForcedMovementFragment>();
		TArrayView<FTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			const FMassEntityHandle Entity = ChunkContext.GetEntity(It);
			if (!MovementSubsystem->TickEntity(Entity, CurrentTick, TickSeconds,
				States[It], Transforms[It], Movements[It]))
			{
				ChunkContext.Defer().RemoveTag<FMassbilityHasForcedMovementTag>(Entity);
			}
		}
	});
}
