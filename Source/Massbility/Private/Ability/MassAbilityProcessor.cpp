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
	// 配置处理阶段和执行顺序：目标更新完成后才能使用 CurrentTarget。
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.Ability"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.Targeting")));
}

// 返回是否允许 Mass 根据查询结果裁剪本处理器的执行范围。
bool UMassAbilityProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

// 配置自动能力执行所需的 Fragment 和 Entity Tag 查询条件。
void UMassAbilityProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	AbilityQuery.AddRequirement<FMassAbilityFragment>(EMassFragmentAccess::ReadWrite);
	AbilityQuery.AddRequirement<FMassTargetFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddRequirement<FMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	AbilityQuery.AddTagRequirement<FMassHasAbilitiesTag>(EMassFragmentPresence::All);
}

// 消费主动能力请求，并为每个实体尝试一次自动能力。
void UMassAbilityProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 同一 Tick 先处理显式请求，再执行每个实体的自动能力。
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
	// 自动能力按 Loadout 槽位顺序尝试；同一实体本 Tick 成功一次即停止。

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
