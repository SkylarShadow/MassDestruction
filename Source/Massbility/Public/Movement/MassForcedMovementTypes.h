// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Mass/EntityHandle.h"

#include "MassForcedMovementTypes.generated.h"

UENUM(BlueprintType)
enum class EMassForcedMovementMode : uint8
{
	Velocity,
	Displacement,
	ToLocation,
};

UENUM(BlueprintType)
enum class EMassForcedMovementBlend : uint8
{
	Replace,
	Additive,
	RejectIfActive,
};

UENUM(BlueprintType)
enum class EMassForcedMovementCollisionPolicy : uint8
{
	AdapterDefault,
	Stop,
	Slide,
	Ignore,
};

struct MASSBILITY_API FMassForcedMovementRequest
{
	FMassEntityHandle Source;
	FMassEntityHandle Target;
	FVector Vector = FVector::ZeroVector;
	uint32 DurationTicks = 1;
	uint32 RequestSerial = 0;
	uint8 Priority = 0;
	EMassForcedMovementMode Mode = EMassForcedMovementMode::Displacement;
	EMassForcedMovementBlend Blend = EMassForcedMovementBlend::Replace;
	EMassForcedMovementCollisionPolicy CollisionPolicy = EMassForcedMovementCollisionPolicy::Ignore;
	uint32 SimulationTick = 0;
};

struct MASSBILITY_API FMassMovementResolveParams
{
	FMassEntityHandle Entity;
	FVector CurrentLocation = FVector::ZeroVector;
	FVector DesiredDelta = FVector::ZeroVector;
	EMassForcedMovementCollisionPolicy CollisionPolicy = EMassForcedMovementCollisionPolicy::Ignore;
};

struct MASSBILITY_API FMassMovementResolveResult
{
	FVector AppliedDelta = FVector::ZeroVector;
	bool bBlocked = false;
	bool bFinished = false;
};

/**
 * Optional native collision/movement resolver. The registrar owns its lifetime and must unregister before destruction.
 * The adapter calculates an applied delta; Mass remains the sole writer of the entity Transform fragment.
 */
class MASSBILITY_API IMassMovementAdapter
{
public:
	virtual ~IMassMovementAdapter() = default;
	virtual bool ResolveForcedMovement(const FMassMovementResolveParams& Params,
		FMassMovementResolveResult& OutResult) = 0;
};

UENUM()
enum class EMassForcedMovementFailure : uint8
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

struct MASSBILITY_API FMassForcedMovementStats
{
	uint64 RequestsQueued = 0;
	uint64 RequestsAccepted = 0;
	uint64 Active = 0;
	uint64 Completed = 0;
	uint64 Blocked = 0;
	uint64 QueueHighWaterMark = 0;
	TStaticArray<uint64, static_cast<int32>(EMassForcedMovementFailure::Count)> Failures = {};
};
