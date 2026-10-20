// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Combat/MassbilityCombatTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassbilityCombatSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassbilityEffectSubsystem;
class UMassbilitySubsystem;
struct FMassEntityManager;
struct FMassExecutionContext;
struct FMassbilityCombatFragment;

UCLASS()
class MASSBILITY_API UMassbilityCombatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassbilityCombatSubsystem* Get(const UObject* WorldContextObject);

	bool QueueDamage(FMassEntityHandle Target, float Damage, FMassEntityHandle Source = {},
		FMassbilityEffectId EffectId = {}, FVector Origin = FVector::ZeroVector,
		FVector Direction = FVector::ZeroVector);
	/** Events remain valid until the Death processor begins a later simulation tick. */
	TConstArrayView<FMassbilityDeathEvent> GetDeathEventsForCurrentTick() const { return DeathEvents; }
	void CopyDeathEventsForCurrentTick(TArray<FMassbilityDeathEvent>& OutEvents) const;
	/** Compatibility copy API. It no longer removes events, so multiple consumers cannot steal from each other. */
	void ConsumeDeathEvents(TArray<FMassbilityDeathEvent>& OutEvents);
	int32 GetPendingDamageCount() const { return PendingDamageRequests.Num(); }

	/** Called by compiled effect execution after Health was reduced. */
	void RecordAppliedDamage(FMassEntityHandle Target, FMassbilityCombatFragment& Combat,
		FMassEntityHandle Source, FMassbilityEffectId EffectId, float AppliedDamage,
		bool bTargetHasNoHealth, uint32 CurrentTick);

private:
	friend class UMassbilityDeathProcessor;

	void FlushDamageRequests(FMassEntityManager& EntityManager, uint32 CurrentTick);
	void BeginDeathEventTick(uint32 CurrentTick);
	void ResolveDeathCandidates(FMassEntityManager& EntityManager, FMassExecutionContext& Context, uint32 CurrentTick);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilitySubsystem> MassbilitySubsystem;

	TArray<FMassbilityDamageRequest> PendingDamageRequests;
	TArray<FMassEntityHandle> PendingDeathCandidates;
	TArray<FMassbilityDeathEvent> DeathEvents;
	uint32 DeathEventsTick = MAX_uint32;
};
