// Copyright MassDestruction. All Rights Reserved.

#include "Effect/MassbilityEffectProcessor.h"

#include "Effect/MassbilityEffectSubsystem.h"
#include "Combat/MassbilityCombatFragments.h"
#include "Core/MassbilitySubsystem.h"
#include "Entity/MassbilityFragments.h"
#include "MassExecutionContext.h"

UMassbilityEffectApplyProcessor::UMassbilityEffectApplyProcessor()
	: WriteDependencyQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.EffectApply"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.OutputDispatch")));
}

bool UMassbilityEffectApplyProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassbilityEffectApplyProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	WriteDependencyQuery.AddRequirement<FMassbilityUnitFragment>(EMassFragmentAccess::ReadOnly);
	WriteDependencyQuery.AddRequirement<FMassbilityVitalFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassbilityAttributeFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassbilityStateFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassbilityCombatFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassbilityActiveEffectFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassbilityEffectApplyProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassbilityEffectSubsystem* EffectSubsystem = World ? World->GetSubsystem<UMassbilityEffectSubsystem>() : nullptr;
	UMassbilitySubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassbilitySubsystem>() : nullptr;
	if (!EffectSubsystem || !CoreSubsystem)
	{
		return;
	}
	EffectSubsystem->FlushRemovalRequests(EntityManager, Context);
	EffectSubsystem->FlushPendingRequests(EntityManager, Context, CoreSubsystem->GetSimulationClock().GetCurrentTick());
}

UMassbilityEffectProcessor::UMassbilityEffectProcessor()
	: ActiveEffectQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.EffectTick"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.EffectApply")));
}

bool UMassbilityEffectProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;   //不要因为实体查询暂时没有匹配实体，就把这个 Processor 剪枝
}

void UMassbilityEffectProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ActiveEffectQuery.AddRequirement<FMassbilityUnitFragment>(EMassFragmentAccess::ReadOnly);
	ActiveEffectQuery.AddRequirement<FMassbilityVitalFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddRequirement<FMassbilityAttributeFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddRequirement<FMassbilityStateFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddRequirement<FMassbilityCombatFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddRequirement<FMassbilityActiveEffectFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddTagRequirement<FMassbilityHasActiveEffectsTag>(EMassFragmentPresence::All);
}

void UMassbilityEffectProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassbilityEffectSubsystem* EffectSubsystem = World ? World->GetSubsystem<UMassbilityEffectSubsystem>() : nullptr;
	UMassbilitySubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassbilitySubsystem>() : nullptr;
	if (!EffectSubsystem || !CoreSubsystem)
	{
		return;
	}
	const uint32 CurrentTick = CoreSubsystem->GetSimulationClock().GetCurrentTick();
	if (CurrentTick == LastProcessedTick)
	{
		return;
	}
	LastProcessedTick = CurrentTick;

	ActiveEffectQuery.ForEachEntityChunk(Context, [EffectSubsystem, CurrentTick](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassbilityUnitFragment> Units = ChunkContext.GetFragmentView<FMassbilityUnitFragment>();
		TArrayView<FMassbilityVitalFragment> Vitals = ChunkContext.GetMutableFragmentView<FMassbilityVitalFragment>();
		TArrayView<FMassbilityAttributeFragment> Attributes = ChunkContext.GetMutableFragmentView<FMassbilityAttributeFragment>();
		TArrayView<FMassbilityStateFragment> States = ChunkContext.GetMutableFragmentView<FMassbilityStateFragment>();
		TArrayView<FMassbilityCombatFragment> Combat = ChunkContext.GetMutableFragmentView<FMassbilityCombatFragment>();
		TArrayView<FMassbilityActiveEffectFragment> ActiveEffects = ChunkContext.GetMutableFragmentView<FMassbilityActiveEffectFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			EffectSubsystem->TickEntity(ChunkContext.GetEntity(It), ChunkContext, CurrentTick,
				Units[It], Vitals[It], Attributes[It], States[It], Combat[It], ActiveEffects[It]);
		}
	});
}
