// Copyright MassDestruction. All Rights Reserved.

#include "Combat/MassCombatSubsystem.h"

#include "Combat/MassCombatFragments.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Effect/MassEffectSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassFragments.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Movement/MassForcedMovementFragments.h"

void UMassCombatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassSubsystem = Collection.InitializeDependency<UMassRuntimeSubsystem>();
	PendingDamageRequests.Reserve(256);
	PendingDeathCandidates.Reserve(64);
	DeathEvents.Reserve(64);
}

void UMassCombatSubsystem::Deinitialize()
{
	DeathEvents.Reset();
	DeathEventsTick = MAX_uint32;
	PendingDeathCandidates.Reset();
	PendingDamageRequests.Reset();
	MassSubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassCombatSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassCombatSubsystem* UMassCombatSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassCombatSubsystem>();
	}
	return nullptr;
}

bool UMassCombatSubsystem::QueueDamage(const FMassEntityHandle Target, const float Damage,
	const FMassEntityHandle Source, const FMassEffectId EffectId, const FVector Origin, const FVector Direction)
{
	if (!Target.IsSet() || !FMath::IsFinite(Damage) || Damage <= 0.0f)
	{
		return false;
	}
	FMassDamageRequest& Request = PendingDamageRequests.AddDefaulted_GetRef();
	Request.Target = Target;
	Request.Context.Source = Source;
	Request.Context.EffectId = EffectId;
	Request.Context.RawMagnitude = Damage;
	Request.Context.Origin = Origin;
	Request.Context.Direction = Direction.GetSafeNormal();
	return true;
}

void UMassCombatSubsystem::ConsumeDeathEvents(TArray<FMassDeathEvent>& OutEvents)
{
	OutEvents.Append(DeathEvents);
}

void UMassCombatSubsystem::CopyDeathEventsForCurrentTick(TArray<FMassDeathEvent>& OutEvents) const
{
	OutEvents.Append(DeathEvents);
}

void UMassCombatSubsystem::BeginDeathEventTick(const uint32 CurrentTick)
{
	if (DeathEventsTick != CurrentTick)
	{
		DeathEvents.Reset();
		DeathEventsTick = CurrentTick;
	}
}

void UMassCombatSubsystem::RecordAppliedDamage(const FMassEntityHandle Target, FMassCombatFragment& Combat,
	const FMassEntityHandle Source, const FMassEffectId EffectId, const float AppliedDamage,
	const bool bTargetHasNoHealth, const uint32 CurrentTick)
{
	if (AppliedDamage <= 0.0f)
	{
		return;
	}
	Combat.LastDamageSource = Source;
	Combat.LastDamageEffect = EffectId;
	Combat.LastDamageTick = CurrentTick;
	if (bTargetHasNoHealth)
	{
		PendingDeathCandidates.Add(Target);
	}
}

void UMassCombatSubsystem::FlushDamageRequests(FMassEntityManager& EntityManager, const uint32 CurrentTick)
{
	for (const FMassDamageRequest& Request : PendingDamageRequests)
	{
		if (!EntityManager.IsEntityValid(Request.Target))
		{
			continue;
		}
		FMassVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassVitalFragment>(Request.Target);
		FMassCombatFragment* Combat = EntityManager.GetFragmentDataPtr<FMassCombatFragment>(Request.Target);
		FMassStateFragment* State = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Request.Target);
		if (!Vitals || !Combat || !State || EnumHasAnyFlags(State->StateFlags, EMassStateFlags::Dying | EMassStateFlags::Dead))
		{
			continue;
		}
		const float PreviousHealth = Vitals->Health;
		Vitals->Health = FMath::Clamp(Vitals->Health - Request.Context.RawMagnitude, 0.0f, Vitals->MaxHealth);
		RecordAppliedDamage(Request.Target, *Combat, Request.Context.Source, Request.Context.EffectId,
			PreviousHealth - Vitals->Health, Vitals->Health <= 0.0f, CurrentTick);
	}
	PendingDamageRequests.Reset();
}

void UMassCombatSubsystem::ResolveDeathCandidates(FMassEntityManager& EntityManager, FMassExecutionContext& Context, const uint32 CurrentTick)
{
	UMassEffectSubsystem* EffectSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMassEffectSubsystem>() : nullptr;
	for (const FMassEntityHandle Entity : PendingDeathCandidates)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}
		FMassVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassVitalFragment>(Entity);
		FMassStateFragment* State = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Entity);
		FMassCombatFragment* Combat = EntityManager.GetFragmentDataPtr<FMassCombatFragment>(Entity);
		if (!Vitals || !State || !Combat || Vitals->Health > 0.0f
			|| EnumHasAnyFlags(State->StateFlags, EMassStateFlags::Dying | EMassStateFlags::Dead))
		{
			continue;
		}
		State->StateFlags &= ~EMassStateFlags::Targetable;
		State->StateFlags |= EMassStateFlags::Dying | EMassStateFlags::AbilityBlocked | EMassStateFlags::MovementBlocked;
		Combat->DeathStartedTick = CurrentTick;
		if (FMassForcedMovementFragment* Movement =
			EntityManager.GetFragmentDataPtr<FMassForcedMovementFragment>(Entity))
		{
			Movement->Reset();
		}
		MassSubsystem->RebuildRuntimeTags(Entity);
		DeathEvents.Add({Entity, Combat->LastDamageSource, Combat->LastDamageEffect, CurrentTick});
		Context.Defer().AddTag<FMassDyingTag>(Entity);
		if (EffectSubsystem)
		{
			EffectSubsystem->QueueRemoveAllEffects(Entity);
		}
	}
	PendingDeathCandidates.Reset();
}
