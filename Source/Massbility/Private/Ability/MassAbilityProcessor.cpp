// Copyright MassDestruction. All Rights Reserved.

#include "Ability/MassAbilityProcessor.h"

#include "Ability/MassAbilitySubsystem.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Entity/MassFragments.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"

UMassAbilityProcessor::UMassAbilityProcessor()
	: AbilityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.Ability"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.Targeting")));
}

bool UMassAbilityProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassAbilityProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	AbilityQuery.AddRequirement<FMassAbilityFragment>(EMassFragmentAccess::ReadWrite);
	AbilityQuery.AddRequirement<FMassTargetFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddRequirement<FMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddTagRequirement<FMassHasAbilitiesTag>(EMassFragmentPresence::All);
}

void UMassAbilityProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassAbilitySubsystem* AbilitySubsystem = World ? World->GetSubsystem<UMassAbilitySubsystem>() : nullptr;
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
	if (!AbilitySubsystem || !CoreSubsystem)
	{
		return;
	}
	const uint32 CurrentTick = CoreSubsystem->GetSimulationClock().GetCurrentTick();
	AbilitySubsystem->FlushRequests(EntityManager, CurrentTick);
	if (LastAutomaticTick == CurrentTick)
	{
		return;
	}
	LastAutomaticTick = CurrentTick;

	AbilityQuery.ForEachEntityChunk(Context, [AbilitySubsystem, &EntityManager, CurrentTick](FMassExecutionContext& ChunkContext)
	{
		TArrayView<FMassAbilityFragment> AbilityStates = ChunkContext.GetMutableFragmentView<FMassAbilityFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			const FMassEntityHandle Entity = ChunkContext.GetEntity(It);
			const FMassCompiledAbilityLoadout* Loadout = AbilitySubsystem->FindLoadout(AbilityStates[It].AbilityLoadoutId);
			if (!Loadout)
			{
				continue;
			}
			for (uint8 Slot = 0; Slot < Loadout->AbilityCount; ++Slot)
			{
				const FMassCompiledAbility* Ability = AbilitySubsystem->FindAbility(Loadout->AbilityIds[Slot]);
				if (!Ability || Ability->TargetPolicy == EMassAbilityTargetPolicy::ExplicitTarget)
				{
					continue;
				}
				const EMassAbilityFailure Failure = AbilitySubsystem->TryActivate(
					EntityManager, Entity, Slot, {}, 1.0f, CurrentTick);
				AbilitySubsystem->RecordResult(Failure);
				if (Failure == EMassAbilityFailure::None)
				{
					break;
				}
			}
		}
	});
}
