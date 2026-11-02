// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Movement/MassForcedMovementTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassForcedMovementSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassRuntimeSubsystem;
struct FMassEntityManager;
struct FMassExecutionContext;
struct FMassForcedMovementFragment;
struct FMassStateFragment;
struct FTransformFragment;

UCLASS()
class MASSBILITY_API UMassForcedMovementSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassForcedMovementSubsystem* Get(const UObject* WorldContextObject);

	/** Game-thread only. Accepted requests begin on the next Mass simulation tick. */
	bool QueueForcedMovement(const FMassForcedMovementRequest& Request);
	bool CancelForcedMovement(FMassEntityHandle Target, uint8 CancelPriority = MAX_uint8);
	bool RegisterMovementAdapter(IMassMovementAdapter& Adapter);
	bool UnregisterMovementAdapter(const IMassMovementAdapter& Adapter);

	const FMassForcedMovementStats& GetStats() const { return Stats; }
	int32 GetPendingRequestCount() const { return PendingRequests.Num(); }

private:
	friend class UMassForcedMovementProcessor;

	void FlushRequests(FMassEntityManager& EntityManager, FMassExecutionContext& Context, uint32 CurrentTick);
	EMassForcedMovementFailure ApplyRequest(FMassEntityManager& EntityManager,
		FMassExecutionContext& Context, const FMassForcedMovementRequest& Request, uint32 CurrentTick);
	bool TickEntity(FMassEntityHandle Entity, uint32 CurrentTick, float TickSeconds,
		const FMassStateFragment& State, FTransformFragment& Transform,
		FMassForcedMovementFragment& Movement);
	void RecordFailure(EMassForcedMovementFailure Failure);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassRuntimeSubsystem> MassSubsystem;

	TArray<FMassForcedMovementRequest> PendingRequests;
	IMassMovementAdapter* MovementAdapter = nullptr;
	uint32 NextRequestSerial = 1;
	int32 MaxPendingRequests = 2048;
	FMassForcedMovementStats Stats;
};
