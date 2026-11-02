// Copyright MassDestruction. All Rights Reserved.

#include "Effect/MassEffectProcessor.h"

#include "Effect/MassEffectSubsystem.h"
#include "Combat/MassCombatFragments.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Entity/MassFragments.h"
#include "MassExecutionContext.h"

UMassEffectApplyProcessor::UMassEffectApplyProcessor()
	: WriteDependencyQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.EffectApply"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.OutputDispatch")));
}

bool UMassEffectApplyProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassEffectApplyProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	WriteDependencyQuery.AddRequirement<FMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	WriteDependencyQuery.AddRequirement<FMassVitalFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassAttributeFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassCombatFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassActiveEffectFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassEffectApplyProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassEffectSubsystem* EffectSubsystem = World ? World->GetSubsystem<UMassEffectSubsystem>() : nullptr;
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
	if (!EffectSubsystem || !CoreSubsystem)
	{
		return;
	}
	EffectSubsystem->FlushRemovalRequests(EntityManager, Context);
	EffectSubsystem->FlushPendingRequests(EntityManager, Context, CoreSubsystem->GetSimulationClock().GetCurrentTick());
}

UMassEffectProcessor::UMassEffectProcessor()
	: ActiveEffectQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.EffectTick"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.EffectApply")));
}

bool UMassEffectProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;   //不要因为实体查询暂时没有匹配实体，就把这个 Processor 剪枝
}

void UMassEffectProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ActiveEffectQuery.AddRequirement<FMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	ActiveEffectQuery.AddRequirement<FMassVitalFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddRequirement<FMassAttributeFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddRequirement<FMassCombatFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddRequirement<FMassActiveEffectFragment>(EMassFragmentAccess::ReadWrite);
	ActiveEffectQuery.AddTagRequirement<FMassHasActiveEffectsTag>(EMassFragmentPresence::All);
}

void UMassEffectProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UMassEffectSubsystem* EffectSubsystem = World ? World->GetSubsystem<UMassEffectSubsystem>() : nullptr;
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
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
		const TConstArrayView<FMassUnitFragment> Units = ChunkContext.GetFragmentView<FMassUnitFragment>();
		TArrayView<FMassVitalFragment> Vitals = ChunkContext.GetMutableFragmentView<FMassVitalFragment>();
		TArrayView<FMassAttributeFragment> Attributes = ChunkContext.GetMutableFragmentView<FMassAttributeFragment>();
		TArrayView<FMassStateFragment> States = ChunkContext.GetMutableFragmentView<FMassStateFragment>();
		TArrayView<FMassCombatFragment> Combat = ChunkContext.GetMutableFragmentView<FMassCombatFragment>();
		TArrayView<FMassActiveEffectFragment> ActiveEffects = ChunkContext.GetMutableFragmentView<FMassActiveEffectFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			EffectSubsystem->TickEntity(ChunkContext.GetEntity(It), ChunkContext, CurrentTick,
				Units[It], Vitals[It], Attributes[It], States[It], Combat[It], ActiveEffects[It]);
		}
	});
}
