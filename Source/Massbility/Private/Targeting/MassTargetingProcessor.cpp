// Copyright MassDestruction. All Rights Reserved.

#include "Targeting/MassTargetingProcessor.h"

#include "Ability/MassAbilitySubsystem.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Entity/MassFragments.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"
#include "Targeting/MassSpatialSubsystem.h"

UMassSpatialBuildProcessor::UMassSpatialBuildProcessor()
	: SourceQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.SpatialBuild"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.ForcedMovement")));
}

bool UMassSpatialBuildProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassSpatialBuildProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	SourceQuery.AddRequirement<FMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	SourceQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadOnly);
	SourceQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassSpatialBuildProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 先把本轮可选目标建立成只读空间快照，后续 Targeting Processor 才能稳定查询。
	UWorld* World = Context.GetWorld();
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
	UMassSpatialSubsystem* SpatialSubsystem = World ? World->GetSubsystem<UMassSpatialSubsystem>() : nullptr;
	if (!CoreSubsystem || !SpatialSubsystem)
	{
		return;
	}
	const uint32 CurrentTick = CoreSubsystem->GetSimulationClock().GetCurrentTick();
	if (LastBuildTick != MAX_uint32
		&& static_cast<uint32>(CurrentTick - LastBuildTick) < CoreSubsystem->GetTargetRefreshTicks())
	{
		return;
	}
	LastBuildTick = CurrentTick;
	SpatialSubsystem->BeginBuild(CoreSubsystem->GetSpatialCellSize());
	SourceQuery.ForEachEntityChunk(Context, [SpatialSubsystem](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassUnitFragment> Units = ChunkContext.GetFragmentView<FMassUnitFragment>();
		const TConstArrayView<FMassStateFragment> States = ChunkContext.GetFragmentView<FMassStateFragment>();
		const TConstArrayView<FTransformFragment> Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			// 不可选中、正在死亡或已死亡的实体不进入索引，减少查询候选并避免命中无效目标。
			if (!EnumHasAnyFlags(States[It].StateFlags, EMassStateFlags::Targetable)
				|| EnumHasAnyFlags(States[It].StateFlags, EMassStateFlags::Dying | EMassStateFlags::Dead))
			{
				continue;
			}
			SpatialSubsystem->Add(ChunkContext.GetEntity(It), Transforms[It].GetTransform().GetLocation(),
				Units[It].TeamId, States[It].RuntimeTags);
		}
	});
}

UMassTargetingProcessor::UMassTargetingProcessor()
	: TargetingQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.Targeting"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.SpatialBuild")));
}

bool UMassTargetingProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassTargetingProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	TargetingQuery.AddRequirement<FMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	TargetingQuery.AddRequirement<FMassAbilityFragment>(EMassFragmentAccess::ReadOnly);
	TargetingQuery.AddRequirement<FMassTargetFragment>(EMassFragmentAccess::ReadWrite);
	TargetingQuery.AddRequirement<FMassStateFragment>(EMassFragmentAccess::ReadOnly);
	TargetingQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	TargetingQuery.AddTagRequirement<FMassHasAbilitiesTag>(EMassFragmentPresence::All);
}

void UMassTargetingProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Targeting 只负责周期性更新当前目标，不负责激活能力或造成伤害。
	UWorld* World = Context.GetWorld();
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
	UMassAbilitySubsystem* AbilitySubsystem = World ? World->GetSubsystem<UMassAbilitySubsystem>() : nullptr;
	UMassSpatialSubsystem* SpatialSubsystem = World ? World->GetSubsystem<UMassSpatialSubsystem>() : nullptr;
	if (!CoreSubsystem || !AbilitySubsystem || !SpatialSubsystem)
	{
		return;
	}
	const uint32 CurrentTick = CoreSubsystem->GetSimulationClock().GetCurrentTick();
	const uint32 RefreshTicks = CoreSubsystem->GetTargetRefreshTicks();
	TargetingQuery.ForEachEntityChunk(Context,
		[AbilitySubsystem, SpatialSubsystem, CurrentTick, RefreshTicks](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassUnitFragment> Units = ChunkContext.GetFragmentView<FMassUnitFragment>();
		const TConstArrayView<FMassAbilityFragment> Abilities = ChunkContext.GetFragmentView<FMassAbilityFragment>();
		const TConstArrayView<FMassStateFragment> States = ChunkContext.GetFragmentView<FMassStateFragment>();
		const TConstArrayView<FTransformFragment> Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		TArrayView<FMassTargetFragment> Targets = ChunkContext.GetMutableFragmentView<FMassTargetFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			// NextRefreshTick 采用模拟 Tick，而不是世界时间，保证目标刷新与固定步长一致。
			if (!FMassSimulationClock::HasReached(CurrentTick, Targets[It].NextRefreshTick))
			{
				continue;
			}
			Targets[It].NextRefreshTick = CurrentTick + RefreshTicks;
			Targets[It].Target = {};
			Targets[It].LastKnownLocation = FVector::ZeroVector;
			// 阻断或死亡状态会清除旧目标，防止后续 Ability 继续使用过期 Handle。
			if (EnumHasAnyFlags(States[It].StateFlags,
				EMassStateFlags::AbilityBlocked | EMassStateFlags::Dying | EMassStateFlags::Dead))
			{
				continue;
			}
			const FMassCompiledAbilityLoadout* Loadout = AbilitySubsystem->FindLoadout(Abilities[It].AbilityLoadoutId);
			if (!Loadout)
			{
				continue;
			}
			for (uint8 Slot = 0; Slot < Loadout->AbilityCount; ++Slot)
			{
				// 当前目标由第一个符合 CurrentTarget 策略且成功命中的能力确定。
				const FMassCompiledAbility* Ability = AbilitySubsystem->FindAbility(Loadout->AbilityIds[Slot]);
				if (!Ability || Ability->TargetPolicy != EMassAbilityTargetPolicy::CurrentTarget
					|| Ability->RangeSquared <= 0.0f)
				{
					continue;
				}
				if (SpatialSubsystem->FindNearestHostile(ChunkContext.GetEntity(It),
					Transforms[It].GetTransform().GetLocation(), Units[It].TeamId, FMath::Sqrt(Ability->RangeSquared),
					Ability->RequiredTargetTags, Ability->BlockedTargetTags,
					Targets[It].Target, Targets[It].LastKnownLocation))
				{
					break;
				}
			}
		}
	});
}
