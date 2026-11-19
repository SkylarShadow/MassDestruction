// Copyright MassDestruction. All Rights Reserved.

// Effect Processor 在 Mass Tick 中推进持续效果并处理效果请求。

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

// 配置应用 Effect 时需要读取和写入的 Fragment。
void UMassEffectApplyProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	WriteDependencyQuery.AddRequirement<FMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	WriteDependencyQuery.AddRequirement<FMassVitalFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassAttributeFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassCombatFragment>(EMassFragmentAccess::ReadWrite);
	WriteDependencyQuery.AddRequirement<FMassActiveEffectFragment>(EMassFragmentAccess::ReadWrite);
}

// 在 Effect 更新前刷新新增、移除和持续效果请求。
void UMassEffectApplyProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 处理新增/移除请求，并推进所有实体上的持续效果。
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

// 构造持续 Effect 处理器并设置执行顺序。
UMassEffectProcessor::UMassEffectProcessor()
	: ActiveEffectQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.EffectTick"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.EffectApply")));
}

// 持续效果需要按实体更新，可能实体查询暂时没有匹配实体，别裁剪
bool UMassEffectProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;   
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
