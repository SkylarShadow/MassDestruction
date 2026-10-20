// Copyright MassDestruction. All Rights Reserved.

#include "Combat/MassbilityDeathProcessor.h"

#include "Combat/MassbilityCombatFragments.h"
#include "Combat/MassbilityCombatSubsystem.h"
#include "Core/MassbilitySubsystem.h"
#include "Entity/MassbilityFragments.h"
#include "MassCommandBuffer.h"
#include "MassExecutionContext.h"

UMassbilityDeathProcessor::UMassbilityDeathProcessor()
	: WriteDependencyQuery(*this)
	, DyingQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.Death"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.EffectTick")));
}

bool UMassbilityDeathProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassbilityDeathProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	WriteDependencyQuery.AddRequirement<FMassbilityVitalFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassbilityStateFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassbilityCombatFragment>(EMassFragmentAccess::ReadWrite);

	DyingQuery.AddRequirement<FMassbilityStateFragment>(EMassFragmentAccess::ReadWrite);
	DyingQuery.AddRequirement<FMassbilityCombatFragment>(EMassFragmentAccess::ReadWrite);
	DyingQuery.AddTagRequirement<FMassbilityDyingTag>(EMassFragmentPresence::All);
}

void UMassbilityDeathProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassbilityCombatSubsystem* CombatSubsystem = World ? World->GetSubsystem<UMassbilityCombatSubsystem>() : nullptr;
	UMassbilitySubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassbilitySubsystem>() : nullptr;
	if (!CombatSubsystem || !CoreSubsystem)
	{
		return;
	}
	const uint32 CurrentTick = CoreSubsystem->GetSimulationClock().GetCurrentTick();
	CombatSubsystem->BeginDeathEventTick(CurrentTick);
	CombatSubsystem->FlushDamageRequests(EntityManager, CurrentTick);
	CombatSubsystem->ResolveDeathCandidates(EntityManager, Context, CurrentTick);

	DyingQuery.ForEachEntityChunk(Context, [CoreSubsystem, CurrentTick](FMassExecutionContext& ChunkContext)
	{
		TArrayView<FMassbilityStateFragment> States = ChunkContext.GetMutableFragmentView<FMassbilityStateFragment>();
		TArrayView<FMassbilityCombatFragment> Combat = ChunkContext.GetMutableFragmentView<FMassbilityCombatFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			if (CurrentTick == Combat[It].DeathStartedTick)
			{
				continue;
			}
			States[It].StateFlags &= ~EMassbilityStateFlags::Dying;
			States[It].StateFlags |= EMassbilityStateFlags::Dead;
			FMassbilityTagMask StateMask;
			CoreSubsystem->BuildStateMask(States[It].StateFlags, StateMask);
			States[It].RuntimeTags = States[It].BaseTags;
			States[It].RuntimeTags.Append(StateMask);
			States[It].RuntimeTags.Append(States[It].EffectTags);
			ChunkContext.Defer().RemoveTag<FMassbilityDyingTag>(ChunkContext.GetEntity(It));
		}
	});
}
