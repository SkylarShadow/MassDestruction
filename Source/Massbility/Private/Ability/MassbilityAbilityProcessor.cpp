// Copyright MassDestruction. All Rights Reserved.

#include "Ability/MassbilityAbilityProcessor.h"

#include "Ability/MassbilityAbilitySubsystem.h"
#include "Core/MassbilitySubsystem.h"
#include "Entity/MassbilityFragments.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"

UMassbilityAbilityProcessor::UMassbilityAbilityProcessor()
	: AbilityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.Ability"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.Targeting")));
}

bool UMassbilityAbilityProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassbilityAbilityProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	AbilityQuery.AddRequirement<FMassbilityAbilityFragment>(EMassFragmentAccess::ReadWrite);
	AbilityQuery.AddRequirement<FMassbilityTargetFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddRequirement<FMassbilityStateFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddRequirement<FMassbilityUnitFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddTagRequirement<FMassbilityHasAbilitiesTag>(EMassFragmentPresence::All);
}

void UMassbilityAbilityProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassbilityAbilitySubsystem* AbilitySubsystem = World ? World->GetSubsystem<UMassbilityAbilitySubsystem>() : nullptr;
	UMassbilitySubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassbilitySubsystem>() : nullptr;
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
		TArrayView<FMassbilityAbilityFragment> AbilityStates = ChunkContext.GetMutableFragmentView<FMassbilityAbilityFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			const FMassEntityHandle Entity = ChunkContext.GetEntity(It);
			const FMassbilityCompiledAbilityLoadout* Loadout = AbilitySubsystem->FindLoadout(AbilityStates[It].AbilityLoadoutId);
			if (!Loadout)
			{
				continue;
			}
			for (uint8 Slot = 0; Slot < Loadout->AbilityCount; ++Slot)
			{
				const FMassbilityCompiledAbility* Ability = AbilitySubsystem->FindAbility(Loadout->AbilityIds[Slot]);
				if (!Ability || Ability->TargetPolicy == EMassbilityAbilityTargetPolicy::ExplicitTarget)
				{
					continue;
				}
				const EMassbilityAbilityFailure Failure = AbilitySubsystem->TryActivate(
					EntityManager, Entity, Slot, {}, 1.0f, CurrentTick);
				AbilitySubsystem->RecordResult(Failure);
				if (Failure == EMassbilityAbilityFailure::None)
				{
					break;
				}
			}
		}
	});
}
