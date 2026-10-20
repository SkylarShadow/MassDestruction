// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Movement/MassbilityForcedMovementTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassbilityForcedMovementSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassbilitySubsystem;
struct FMassEntityManager;
struct FMassExecutionContext;
struct FMassbilityForcedMovementFragment;
struct FMassbilityStateFragment;
struct FTransformFragment;

UCLASS()
class MASSBILITY_API UMassbilityForcedMovementSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassbilityForcedMovementSubsystem* Get(const UObject* WorldContextObject);

	/** Game-thread only. Accepted requests begin on the next Massbility simulation tick. */
	bool QueueForcedMovement(const FMassbilityForcedMovementRequest& Request);
	bool CancelForcedMovement(FMassEntityHandle Target, uint8 CancelPriority = MAX_uint8);
	bool RegisterMovementAdapter(IMassbilityMovementAdapter& Adapter);
	bool UnregisterMovementAdapter(const IMassbilityMovementAdapter& Adapter);

	const FMassbilityForcedMovementStats& GetStats() const { return Stats; }
	int32 GetPendingRequestCount() const { return PendingRequests.Num(); }

private:
	friend class UMassbilityForcedMovementProcessor;

	void FlushRequests(FMassEntityManager& EntityManager, FMassExecutionContext& Context, uint32 CurrentTick);
	EMassbilityForcedMovementFailure ApplyRequest(FMassEntityManager& EntityManager,
		FMassExecutionContext& Context, const FMassbilityForcedMovementRequest& Request, uint32 CurrentTick);
	bool TickEntity(FMassEntityHandle Entity, uint32 CurrentTick, float TickSeconds,
		const FMassbilityStateFragment& State, FTransformFragment& Transform,
		FMassbilityForcedMovementFragment& Movement);
	void RecordFailure(EMassbilityForcedMovementFailure Failure);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilitySubsystem> MassbilitySubsystem;

	TArray<FMassbilityForcedMovementRequest> PendingRequests;
	IMassbilityMovementAdapter* MovementAdapter = nullptr;
	uint32 NextRequestSerial = 1;
	int32 MaxPendingRequests = 2048;
	FMassbilityForcedMovementStats Stats;
};
