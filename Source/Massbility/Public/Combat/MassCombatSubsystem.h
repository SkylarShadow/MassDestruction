// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Combat/MassCombatTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassCombatSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassEffectSubsystem;
class UMassRuntimeSubsystem;
struct FMassEntityManager;
struct FMassExecutionContext;
struct FMassCombatFragment;

UCLASS()
class MASSBILITY_API UMassCombatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassCombatSubsystem* Get(const UObject* WorldContextObject);

	bool QueueDamage(FMassEntityHandle Target, float Damage, FMassEntityHandle Source = {},
		FMassEffectId EffectId = {}, FVector Origin = FVector::ZeroVector,
		FVector Direction = FVector::ZeroVector);
	/** Events remain valid until the Death processor begins a later simulation tick. */
	TConstArrayView<FMassDeathEvent> GetDeathEventsForCurrentTick() const { return DeathEvents; }
	void CopyDeathEventsForCurrentTick(TArray<FMassDeathEvent>& OutEvents) const;
	/** Compatibility copy API. It no longer removes events, so multiple consumers cannot steal from each other. */
	void ConsumeDeathEvents(TArray<FMassDeathEvent>& OutEvents);
	int32 GetPendingDamageCount() const { return PendingDamageRequests.Num(); }

	/** Called by compiled effect execution after Health was reduced. */
	void RecordAppliedDamage(FMassEntityHandle Target, FMassCombatFragment& Combat,
		FMassEntityHandle Source, FMassEffectId EffectId, float AppliedDamage,
		bool bTargetHasNoHealth, uint32 CurrentTick);

private:
	friend class UMassDeathProcessor;

	void FlushDamageRequests(FMassEntityManager& EntityManager, uint32 CurrentTick);
	void BeginDeathEventTick(uint32 CurrentTick);
	void ResolveDeathCandidates(FMassEntityManager& EntityManager, FMassExecutionContext& Context, uint32 CurrentTick);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassRuntimeSubsystem> MassSubsystem;

	TArray<FMassDamageRequest> PendingDamageRequests;
	TArray<FMassEntityHandle> PendingDeathCandidates;
	TArray<FMassDeathEvent> DeathEvents;
	uint32 DeathEventsTick = MAX_uint32;
};
