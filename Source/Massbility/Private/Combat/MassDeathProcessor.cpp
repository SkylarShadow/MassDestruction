// Copyright MassDestruction. All Rights Reserved.

#include "Combat/MassDeathProcessor.h"

#include "Combat/MassCombatFragments.h"
#include "Combat/MassCombatSubsystem.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Entity/MassFragments.h"
#include "MassCommandBuffer.h"
#include "MassExecutionContext.h"

UMassDeathProcessor::UMassDeathProcessor()
	: WriteDependencyQuery(*this)
	, DyingQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.Death"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.EffectTick")));
}

bool UMassDeathProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassDeathProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	WriteDependencyQuery.AddRequirement<FMassVitalFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassCombatFragment>(EMassFragmentAccess::ReadWrite);

	DyingQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadWrite);
	DyingQuery.AddRequirement<FMassCombatFragment>(EMassFragmentAccess::ReadWrite);
	DyingQuery.AddTagRequirement<FMassDyingTag>(EMassFragmentPresence::All);
}

void UMassDeathProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassCombatSubsystem* CombatSubsystem = World ? World->GetSubsystem<UMassCombatSubsystem>() : nullptr;
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
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
		TArrayView<FMassStateFragment> States = ChunkContext.GetMutableFragmentView<FMassStateFragment>();
		TArrayView<FMassCombatFragment> Combat = ChunkContext.GetMutableFragmentView<FMassCombatFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			if (CurrentTick == Combat[It].DeathStartedTick)
			{
				continue;
			}
			States[It].StateFlags &= ~EMassStateFlags::Dying;
			States[It].StateFlags |= EMassStateFlags::Dead;
			FMassTagMask StateMask;
			CoreSubsystem->BuildStateMask(States[It].StateFlags, StateMask);
			States[It].RuntimeTags = States[It].BaseTags;
			States[It].RuntimeTags.Append(StateMask);
			States[It].RuntimeTags.Append(States[It].EffectTags);
			ChunkContext.Defer().RemoveTag<FMassDyingTag>(ChunkContext.GetEntity(It));
		}
	});
}
