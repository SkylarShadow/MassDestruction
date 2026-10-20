// Copyright MassDestruction. All Rights Reserved.

#include "Combat/MassbilityCombatSubsystem.h"

#include "Combat/MassbilityCombatFragments.h"
#include "Core/MassbilitySubsystem.h"
#include "Effect/MassbilityEffectSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassbilityFragments.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Movement/MassbilityForcedMovementFragments.h"

void UMassbilityCombatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassbilitySubsystem = Collection.InitializeDependency<UMassbilitySubsystem>();
	PendingDamageRequests.Reserve(256);
	PendingDeathCandidates.Reserve(64);
	DeathEvents.Reserve(64);
}

void UMassbilityCombatSubsystem::Deinitialize()
{
	DeathEvents.Reset();
	DeathEventsTick = MAX_uint32;
	PendingDeathCandidates.Reset();
	PendingDamageRequests.Reset();
	MassbilitySubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassbilityCombatSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassbilityCombatSubsystem* UMassbilityCombatSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassbilityCombatSubsystem>();
	}
	return nullptr;
}

bool UMassbilityCombatSubsystem::QueueDamage(const FMassEntityHandle Target, const float Damage,
	const FMassEntityHandle Source, const FMassbilityEffectId EffectId, const FVector Origin, const FVector Direction)
{
	if (!Target.IsSet() || !FMath::IsFinite(Damage) || Damage <= 0.0f)
	{
		return false;
	}
	FMassbilityDamageRequest& Request = PendingDamageRequests.AddDefaulted_GetRef();
	Request.Target = Target;
	Request.Context.Source = Source;
	Request.Context.EffectId = EffectId;
	Request.Context.RawMagnitude = Damage;
	Request.Context.Origin = Origin;
	Request.Context.Direction = Direction.GetSafeNormal();
	return true;
}

void UMassbilityCombatSubsystem::ConsumeDeathEvents(TArray<FMassbilityDeathEvent>& OutEvents)
{
	OutEvents.Append(DeathEvents);
}

void UMassbilityCombatSubsystem::CopyDeathEventsForCurrentTick(TArray<FMassbilityDeathEvent>& OutEvents) const
{
	OutEvents.Append(DeathEvents);
}

void UMassbilityCombatSubsystem::BeginDeathEventTick(const uint32 CurrentTick)
{
	if (DeathEventsTick != CurrentTick)
	{
		DeathEvents.Reset();
		DeathEventsTick = CurrentTick;
	}
}

void UMassbilityCombatSubsystem::RecordAppliedDamage(const FMassEntityHandle Target, FMassbilityCombatFragment& Combat,
	const FMassEntityHandle Source, const FMassbilityEffectId EffectId, const float AppliedDamage,
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

void UMassbilityCombatSubsystem::FlushDamageRequests(FMassEntityManager& EntityManager, const uint32 CurrentTick)
{
	for (const FMassbilityDamageRequest& Request : PendingDamageRequests)
	{
		if (!EntityManager.IsEntityValid(Request.Target))
		{
			continue;
		}
		FMassbilityVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassbilityVitalFragment>(Request.Target);
		FMassbilityCombatFragment* Combat = EntityManager.GetFragmentDataPtr<FMassbilityCombatFragment>(Request.Target);
		FMassbilityStateFragment* State = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Request.Target);
		if (!Vitals || !Combat || !State || EnumHasAnyFlags(State->StateFlags, EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
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

void UMassbilityCombatSubsystem::ResolveDeathCandidates(FMassEntityManager& EntityManager, FMassExecutionContext& Context, const uint32 CurrentTick)
{
	UMassbilityEffectSubsystem* EffectSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMassbilityEffectSubsystem>() : nullptr;
	for (const FMassEntityHandle Entity : PendingDeathCandidates)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}
		FMassbilityVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassbilityVitalFragment>(Entity);
		FMassbilityStateFragment* State = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Entity);
		FMassbilityCombatFragment* Combat = EntityManager.GetFragmentDataPtr<FMassbilityCombatFragment>(Entity);
		if (!Vitals || !State || !Combat || Vitals->Health > 0.0f
			|| EnumHasAnyFlags(State->StateFlags, EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
		{
			continue;
		}
		State->StateFlags &= ~EMassbilityStateFlags::Targetable;
		State->StateFlags |= EMassbilityStateFlags::Dying | EMassbilityStateFlags::AbilityBlocked | EMassbilityStateFlags::MovementBlocked;
		Combat->DeathStartedTick = CurrentTick;
		if (FMassbilityForcedMovementFragment* Movement =
			EntityManager.GetFragmentDataPtr<FMassbilityForcedMovementFragment>(Entity))
		{
			Movement->Reset();
		}
		MassbilitySubsystem->RebuildRuntimeTags(Entity);
		DeathEvents.Add({Entity, Combat->LastDamageSource, Combat->LastDamageEffect, CurrentTick});
		Context.Defer().AddTag<FMassbilityDyingTag>(Entity);
		if (EffectSubsystem)
		{
			EffectSubsystem->QueueRemoveAllEffects(Entity);
		}
	}
	PendingDeathCandidates.Reset();
}
