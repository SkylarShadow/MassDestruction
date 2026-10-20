// Copyright MassDestruction. All Rights Reserved.

#include "Targeting/MassbilityTargetingProcessor.h"

#include "Ability/MassbilityAbilitySubsystem.h"
#include "Core/MassbilitySubsystem.h"
#include "Entity/MassbilityFragments.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"
#include "Targeting/MassbilitySpatialSubsystem.h"

UMassbilitySpatialBuildProcessor::UMassbilitySpatialBuildProcessor()
	: SourceQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.SpatialBuild"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.ForcedMovement")));
}

bool UMassbilitySpatialBuildProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassbilitySpatialBuildProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	SourceQuery.AddRequirement<FMassbilityUnitFragment>(EMassFragmentAccess::ReadOnly);
	SourceQuery.AddRequirement<FMassbilityStateFragment>(EMassFragmentAccess::ReadOnly);
	SourceQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassbilitySpatialBuildProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 先把本轮可选目标建立成只读空间快照，后续 Targeting Processor 才能稳定查询。
	UWorld* World = Context.GetWorld();
	UMassbilitySubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassbilitySubsystem>() : nullptr;
	UMassbilitySpatialSubsystem* SpatialSubsystem = World ? World->GetSubsystem<UMassbilitySpatialSubsystem>() : nullptr;
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
		const TConstArrayView<FMassbilityUnitFragment> Units = ChunkContext.GetFragmentView<FMassbilityUnitFragment>();
		const TConstArrayView<FMassbilityStateFragment> States = ChunkContext.GetFragmentView<FMassbilityStateFragment>();
		const TConstArrayView<FTransformFragment> Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			// 不可选中、正在死亡或已死亡的实体不进入索引，减少查询候选并避免命中无效目标。
			if (!EnumHasAnyFlags(States[It].StateFlags, EMassbilityStateFlags::Targetable)
				|| EnumHasAnyFlags(States[It].StateFlags, EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
			{
				continue;
			}
			SpatialSubsystem->Add(ChunkContext.GetEntity(It), Transforms[It].GetTransform().GetLocation(),
				Units[It].TeamId, States[It].RuntimeTags);
		}
	});
}

UMassbilityTargetingProcessor::UMassbilityTargetingProcessor()
	: TargetingQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.Targeting"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.SpatialBuild")));
}

bool UMassbilityTargetingProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

void UMassbilityTargetingProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	TargetingQuery.AddRequirement<FMassbilityUnitFragment>(EMassFragmentAccess::ReadOnly);
	TargetingQuery.AddRequirement<FMassbilityAbilityFragment>(EMassFragmentAccess::ReadOnly);
	TargetingQuery.AddRequirement<FMassbilityTargetFragment>(EMassFragmentAccess::ReadWrite);
	TargetingQuery.AddRequirement<FMassbilityStateFragment>(EMassFragmentAccess::ReadOnly);
	TargetingQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	TargetingQuery.AddTagRequirement<FMassbilityHasAbilitiesTag>(EMassFragmentPresence::All);
}

void UMassbilityTargetingProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Targeting 只负责周期性更新当前目标，不负责激活能力或造成伤害。
	UWorld* World = Context.GetWorld();
	UMassbilitySubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassbilitySubsystem>() : nullptr;
	UMassbilityAbilitySubsystem* AbilitySubsystem = World ? World->GetSubsystem<UMassbilityAbilitySubsystem>() : nullptr;
	UMassbilitySpatialSubsystem* SpatialSubsystem = World ? World->GetSubsystem<UMassbilitySpatialSubsystem>() : nullptr;
	if (!CoreSubsystem || !AbilitySubsystem || !SpatialSubsystem)
	{
		return;
	}
	const uint32 CurrentTick = CoreSubsystem->GetSimulationClock().GetCurrentTick();
	const uint32 RefreshTicks = CoreSubsystem->GetTargetRefreshTicks();
	TargetingQuery.ForEachEntityChunk(Context,
		[AbilitySubsystem, SpatialSubsystem, CurrentTick, RefreshTicks](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassbilityUnitFragment> Units = ChunkContext.GetFragmentView<FMassbilityUnitFragment>();
		const TConstArrayView<FMassbilityAbilityFragment> Abilities = ChunkContext.GetFragmentView<FMassbilityAbilityFragment>();
		const TConstArrayView<FMassbilityStateFragment> States = ChunkContext.GetFragmentView<FMassbilityStateFragment>();
		const TConstArrayView<FTransformFragment> Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		TArrayView<FMassbilityTargetFragment> Targets = ChunkContext.GetMutableFragmentView<FMassbilityTargetFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			// NextRefreshTick 采用模拟 Tick，而不是世界时间，保证目标刷新与固定步长一致。
			if (!FMassbilitySimulationClock::HasReached(CurrentTick, Targets[It].NextRefreshTick))
			{
				continue;
			}
			Targets[It].NextRefreshTick = CurrentTick + RefreshTicks;
			Targets[It].Target = {};
			Targets[It].LastKnownLocation = FVector::ZeroVector;
			// 阻断或死亡状态会清除旧目标，防止后续 Ability 继续使用过期 Handle。
			if (EnumHasAnyFlags(States[It].StateFlags,
				EMassbilityStateFlags::AbilityBlocked | EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
			{
				continue;
			}
			const FMassbilityCompiledAbilityLoadout* Loadout = AbilitySubsystem->FindLoadout(Abilities[It].AbilityLoadoutId);
			if (!Loadout)
			{
				continue;
			}
			for (uint8 Slot = 0; Slot < Loadout->AbilityCount; ++Slot)
			{
				// 当前目标由第一个符合 CurrentTarget 策略且成功命中的能力确定。
				const FMassbilityCompiledAbility* Ability = AbilitySubsystem->FindAbility(Loadout->AbilityIds[Slot]);
				if (!Ability || Ability->TargetPolicy != EMassbilityAbilityTargetPolicy::CurrentTarget
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
