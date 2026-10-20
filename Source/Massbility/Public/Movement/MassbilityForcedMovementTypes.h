// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Mass/EntityHandle.h"

#include "MassbilityForcedMovementTypes.generated.h"

UENUM(BlueprintType)
enum class EMassbilityForcedMovementMode : uint8
{
	Velocity,
	Displacement,
	ToLocation,
};

UENUM(BlueprintType)
enum class EMassbilityForcedMovementBlend : uint8
{
	Replace,
	Additive,
	RejectIfActive,
};

UENUM(BlueprintType)
enum class EMassbilityForcedMovementCollisionPolicy : uint8
{
	AdapterDefault,
	Stop,
	Slide,
	Ignore,
};

struct MASSBILITY_API FMassbilityForcedMovementRequest
{
	FMassEntityHandle Source;
	FMassEntityHandle Target;
	FVector Vector = FVector::ZeroVector;
	uint32 DurationTicks = 1;
	uint32 RequestSerial = 0;
	uint8 Priority = 0;
	EMassbilityForcedMovementMode Mode = EMassbilityForcedMovementMode::Displacement;
	EMassbilityForcedMovementBlend Blend = EMassbilityForcedMovementBlend::Replace;
	EMassbilityForcedMovementCollisionPolicy CollisionPolicy = EMassbilityForcedMovementCollisionPolicy::Ignore;
	uint32 SimulationTick = 0;
};

struct MASSBILITY_API FMassbilityMovementResolveParams
{
	FMassEntityHandle Entity;
	FVector CurrentLocation = FVector::ZeroVector;
	FVector DesiredDelta = FVector::ZeroVector;
	EMassbilityForcedMovementCollisionPolicy CollisionPolicy = EMassbilityForcedMovementCollisionPolicy::Ignore;
};

struct MASSBILITY_API FMassbilityMovementResolveResult
{
	FVector AppliedDelta = FVector::ZeroVector;
	bool bBlocked = false;
	bool bFinished = false;
};

/**
 * Optional native collision/movement resolver. The registrar owns its lifetime and must unregister before destruction.
 * The adapter calculates an applied delta; Massbility remains the sole writer of the entity Transform fragment.
 */
class MASSBILITY_API IMassbilityMovementAdapter
{
public:
	virtual ~IMassbilityMovementAdapter() = default;
	virtual bool ResolveForcedMovement(const FMassbilityMovementResolveParams& Params,
		FMassbilityMovementResolveResult& OutResult) = 0;
};

UENUM()
enum class EMassbilityForcedMovementFailure : uint8
{
	None,
	InvalidRequest,
	InvalidTarget,
	ExternalTargetUnsupported,
	MissingFragment,
	DeadTarget,
	LowerPriority,
	ActiveRejected,
	UnsupportedBlend,
	UnsupportedCollision,
	Capacity,
	Count UMETA(Hidden),
};

struct MASSBILITY_API FMassbilityForcedMovementStats
{
	uint64 RequestsQueued = 0;
	uint64 RequestsAccepted = 0;
	uint64 Active = 0;
	uint64 Completed = 0;
	uint64 Blocked = 0;
	uint64 QueueHighWaterMark = 0;
	TStaticArray<uint64, static_cast<int32>(EMassbilityForcedMovementFailure::Count)> Failures = {};
};
