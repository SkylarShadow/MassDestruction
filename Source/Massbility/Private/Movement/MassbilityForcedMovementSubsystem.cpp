// Copyright MassDestruction. All Rights Reserved.

#include "Movement/MassbilityForcedMovementSubsystem.h"

#include "Core/MassbilitySettings.h"
#include "Core/MassbilitySubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassbilityFragments.h"
#include "External/MassbilityExternalTargetFragments.h"
#include "Mass/EntityFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Movement/MassbilityForcedMovementFragments.h"

void UMassbilityForcedMovementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassbilitySubsystem = Collection.InitializeDependency<UMassbilitySubsystem>();
	const UMassbilitySettings* Settings = GetDefault<UMassbilitySettings>();
	MaxPendingRequests = Settings ? FMath::Max(1, Settings->MaxPendingForcedMovementRequests) : 2048;
	PendingRequests.Reserve(FMath::Min(MaxPendingRequests, 128));
}

void UMassbilityForcedMovementSubsystem::Deinitialize()
{
	MovementAdapter = nullptr;
	PendingRequests.Reset();
	NextRequestSerial = 1;
	Stats = {};
	MassbilitySubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassbilityForcedMovementSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassbilityForcedMovementSubsystem* UMassbilityForcedMovementSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassbilityForcedMovementSubsystem>();
	}
	return nullptr;
}

bool UMassbilityForcedMovementSubsystem::QueueForcedMovement(const FMassbilityForcedMovementRequest& Request)
{
	if (!IsInGameThread() || !Request.Target.IsSet() || Request.Vector.ContainsNaN() || Request.DurationTicks == 0)
	{
		RecordFailure(EMassbilityForcedMovementFailure::InvalidRequest);
		return false;
	}
	if (PendingRequests.Num() >= MaxPendingRequests)
	{
		RecordFailure(EMassbilityForcedMovementFailure::Capacity);
		return false;
	}
	FMassbilityForcedMovementRequest& Queued = PendingRequests.Add_GetRef(Request);
	if (Queued.RequestSerial == 0)
	{
		Queued.RequestSerial = NextRequestSerial++;
		if (NextRequestSerial == 0)
		{
			NextRequestSerial = 1;
		}
	}
	++Stats.RequestsQueued;
	Stats.QueueHighWaterMark = FMath::Max(Stats.QueueHighWaterMark, static_cast<uint64>(PendingRequests.Num()));
	return true;
}

bool UMassbilityForcedMovementSubsystem::CancelForcedMovement(const FMassEntityHandle Target,
	const uint8 CancelPriority)
{
	if (!EntitySubsystem)
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(Target))
	{
		return false;
	}
	FMassbilityForcedMovementFragment* Movement =
		EntityManager.GetFragmentDataPtr<FMassbilityForcedMovementFragment>(Target);
	if (!Movement || !Movement->bActive || CancelPriority < Movement->Priority)
	{
		return false;
	}
	Movement->Reset();
	return true;
}

bool UMassbilityForcedMovementSubsystem::RegisterMovementAdapter(IMassbilityMovementAdapter& Adapter)
{
	if (MovementAdapter && MovementAdapter != &Adapter)
	{
		return false;
	}
	MovementAdapter = &Adapter;
	return true;
}

bool UMassbilityForcedMovementSubsystem::UnregisterMovementAdapter(const IMassbilityMovementAdapter& Adapter)
{
	if (MovementAdapter != &Adapter)
	{
		return false;
	}
	MovementAdapter = nullptr;
	return true;
}

void UMassbilityForcedMovementSubsystem::FlushRequests(FMassEntityManager& EntityManager,
	FMassExecutionContext& Context, const uint32 CurrentTick)
{
	for (const FMassbilityForcedMovementRequest& Request : PendingRequests)
	{
		const EMassbilityForcedMovementFailure Failure = ApplyRequest(EntityManager, Context, Request, CurrentTick);
		if (Failure == EMassbilityForcedMovementFailure::None)
		{
			++Stats.RequestsAccepted;
		}
		else
		{
			RecordFailure(Failure);
		}
	}
	PendingRequests.Reset();
}

EMassbilityForcedMovementFailure UMassbilityForcedMovementSubsystem::ApplyRequest(
	FMassEntityManager& EntityManager, FMassExecutionContext& Context,
	const FMassbilityForcedMovementRequest& Request, const uint32 CurrentTick)
{
	if (!EntityManager.IsEntityValid(Request.Target))
	{
		return EMassbilityForcedMovementFailure::InvalidTarget;
	}
	if (EntityManager.GetFragmentDataPtr<FMassbilityExternalTargetFragment>(Request.Target))
	{
		return EMassbilityForcedMovementFailure::ExternalTargetUnsupported;
	}
	FMassbilityForcedMovementFragment* Movement =
		EntityManager.GetFragmentDataPtr<FMassbilityForcedMovementFragment>(Request.Target);
	const FMassbilityStateFragment* State = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Request.Target);
	const FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Request.Target);
	if (!Movement || !State || !Transform)
	{
		return EMassbilityForcedMovementFailure::MissingFragment;
	}
	if (EnumHasAnyFlags(State->StateFlags, EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
	{
		Movement->Reset();
		return EMassbilityForcedMovementFailure::DeadTarget;
	}
	if (!MovementAdapter && Request.CollisionPolicy != EMassbilityForcedMovementCollisionPolicy::Ignore)
	{
		return EMassbilityForcedMovementFailure::UnsupportedCollision;
	}
	if (Movement->bActive)
	{
		if (Request.Blend == EMassbilityForcedMovementBlend::RejectIfActive)
		{
			return EMassbilityForcedMovementFailure::ActiveRejected;
		}
		if (Request.Priority < Movement->Priority)
		{
			return EMassbilityForcedMovementFailure::LowerPriority;
		}
		if (Request.Blend == EMassbilityForcedMovementBlend::Additive)
		{
			if (Request.Mode != EMassbilityForcedMovementMode::Velocity
				|| Movement->Mode != EMassbilityForcedMovementMode::Velocity
				|| Request.CollisionPolicy != Movement->CollisionPolicy)
			{
				return EMassbilityForcedMovementFailure::UnsupportedBlend;
			}
			Movement->Vector += Request.Vector;
			Movement->EndTick = FMath::Max(Movement->EndTick, CurrentTick + Request.DurationTicks);
			Movement->Priority = Request.Priority;
			Movement->RequestSerial = Request.RequestSerial;
			return EMassbilityForcedMovementFailure::None;
		}
	}

	const bool bWasActive = Movement->bActive;
	Movement->StartLocation = Transform->GetTransform().GetLocation();
	Movement->Vector = Request.Vector;
	Movement->StartTick = CurrentTick + (bWasActive ? 0u : 1u);
	Movement->EndTick = Movement->StartTick + Request.DurationTicks;
	Movement->AppliedTicks = 0;
	Movement->RequestSerial = Request.RequestSerial;
	Movement->Priority = Request.Priority;
	Movement->Mode = Request.Mode;
	Movement->CollisionPolicy = Request.CollisionPolicy;
	Movement->bActive = true;
	if (!bWasActive)
	{
		Context.Defer().AddTag<FMassbilityHasForcedMovementTag>(Request.Target);
	}
	return EMassbilityForcedMovementFailure::None;
}

bool UMassbilityForcedMovementSubsystem::TickEntity(const FMassEntityHandle Entity, const uint32 CurrentTick,
	const float TickSeconds, const FMassbilityStateFragment& State, FTransformFragment& Transform,
	FMassbilityForcedMovementFragment& Movement)
{
	if (!Movement.bActive)
	{
		return false;
	}
	++Stats.Active;
	if (EnumHasAnyFlags(State.StateFlags, EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
	{
		Movement.Reset();
		return false;
	}
	const uint32 DurationTicks = FMath::Max(1u, Movement.EndTick - Movement.StartTick);
	const uint32 ExpectedAppliedTicks = FMassbilitySimulationClock::HasReached(CurrentTick, Movement.StartTick)
		? FMath::Min(DurationTicks, static_cast<uint32>(CurrentTick - Movement.StartTick) + 1u) : 0u;
	if (ExpectedAppliedTicks <= Movement.AppliedTicks)
	{
		return true;
	}
	const uint32 DeltaTicks = ExpectedAppliedTicks - Movement.AppliedTicks;
	const FVector CurrentLocation = Transform.GetTransform().GetLocation();
	FVector DesiredDelta = FVector::ZeroVector;
	if (Movement.Mode == EMassbilityForcedMovementMode::Velocity)
	{
		DesiredDelta = Movement.Vector * TickSeconds * static_cast<float>(DeltaTicks);
	}
	else
	{
		const FVector TotalDisplacement = Movement.Mode == EMassbilityForcedMovementMode::ToLocation
			? Movement.Vector - Movement.StartLocation : Movement.Vector;
		DesiredDelta = TotalDisplacement * (static_cast<float>(DeltaTicks) / static_cast<float>(DurationTicks));
		if (ExpectedAppliedTicks == DurationTicks && Movement.Mode == EMassbilityForcedMovementMode::ToLocation)
		{
			DesiredDelta = Movement.Vector - CurrentLocation;
		}
	}

	FMassbilityMovementResolveResult Result;
	if (MovementAdapter)
	{
		FMassbilityMovementResolveParams Params;
		Params.Entity = Entity;
		Params.CurrentLocation = CurrentLocation;
		Params.DesiredDelta = DesiredDelta;
		Params.CollisionPolicy = Movement.CollisionPolicy;
		if (!MovementAdapter->ResolveForcedMovement(Params, Result))
		{
			Result.bBlocked = true;
			Result.bFinished = true;
		}
	}
	else
	{
		Result.AppliedDelta = DesiredDelta;
	}

	if (!Result.AppliedDelta.ContainsNaN())
	{
		FTransform UpdatedTransform = Transform.GetTransform();
		UpdatedTransform.AddToTranslation(Result.AppliedDelta);
		Transform.SetTransform(UpdatedTransform);
	}
	Movement.AppliedTicks = ExpectedAppliedTicks;
	if (Result.bBlocked)
	{
		++Stats.Blocked;
	}
	if (Result.bFinished || Result.bBlocked || ExpectedAppliedTicks >= DurationTicks)
	{
		Movement.Reset();
		++Stats.Completed;
		return false;
	}
	return true;
}

void UMassbilityForcedMovementSubsystem::RecordFailure(const EMassbilityForcedMovementFailure Failure)
{
	const uint8 Index = static_cast<uint8>(Failure);
	if (Failure != EMassbilityForcedMovementFailure::None && Index < Stats.Failures.Num())
	{
		++Stats.Failures[Index];
	}
}
