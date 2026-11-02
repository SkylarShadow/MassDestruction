// Copyright MassDestruction. All Rights Reserved.

#include "Movement/MassForcedMovementSubsystem.h"

#include "Core/MassRuntimeSettings.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassFragments.h"
#include "External/MassExternalTargetFragments.h"
#include "Mass/EntityFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Movement/MassForcedMovementFragments.h"

void UMassForcedMovementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassSubsystem = Collection.InitializeDependency<UMassRuntimeSubsystem>();
	const UMassRuntimeSettings* Settings = GetDefault<UMassRuntimeSettings>();
	MaxPendingRequests = Settings ? FMath::Max(1, Settings->MaxPendingForcedMovementRequests) : 2048;
	PendingRequests.Reserve(FMath::Min(MaxPendingRequests, 128));
}

void UMassForcedMovementSubsystem::Deinitialize()
{
	MovementAdapter = nullptr;
	PendingRequests.Reset();
	NextRequestSerial = 1;
	Stats = {};
	MassSubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassForcedMovementSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassForcedMovementSubsystem* UMassForcedMovementSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassForcedMovementSubsystem>();
	}
	return nullptr;
}

bool UMassForcedMovementSubsystem::QueueForcedMovement(const FMassForcedMovementRequest& Request)
{
	if (!IsInGameThread() || !Request.Target.IsSet() || Request.Vector.ContainsNaN() || Request.DurationTicks == 0)
	{
		RecordFailure(EMassForcedMovementFailure::InvalidRequest);
		return false;
	}
	if (PendingRequests.Num() >= MaxPendingRequests)
	{
		RecordFailure(EMassForcedMovementFailure::Capacity);
		return false;
	}
	FMassForcedMovementRequest& Queued = PendingRequests.Add_GetRef(Request);
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

bool UMassForcedMovementSubsystem::CancelForcedMovement(const FMassEntityHandle Target,
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
	FMassForcedMovementFragment* Movement =
		EntityManager.GetFragmentDataPtr<FMassForcedMovementFragment>(Target);
	if (!Movement || !Movement->bActive || CancelPriority < Movement->Priority)
	{
		return false;
	}
	Movement->Reset();
	return true;
}

bool UMassForcedMovementSubsystem::RegisterMovementAdapter(IMassMovementAdapter& Adapter)
{
	if (MovementAdapter && MovementAdapter != &Adapter)
	{
		return false;
	}
	MovementAdapter = &Adapter;
	return true;
}

bool UMassForcedMovementSubsystem::UnregisterMovementAdapter(const IMassMovementAdapter& Adapter)
{
	if (MovementAdapter != &Adapter)
	{
		return false;
	}
	MovementAdapter = nullptr;
	return true;
}

void UMassForcedMovementSubsystem::FlushRequests(FMassEntityManager& EntityManager,
	FMassExecutionContext& Context, const uint32 CurrentTick)
{
	for (const FMassForcedMovementRequest& Request : PendingRequests)
	{
		const EMassForcedMovementFailure Failure = ApplyRequest(EntityManager, Context, Request, CurrentTick);
		if (Failure == EMassForcedMovementFailure::None)
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

EMassForcedMovementFailure UMassForcedMovementSubsystem::ApplyRequest(
	FMassEntityManager& EntityManager, FMassExecutionContext& Context,
	const FMassForcedMovementRequest& Request, const uint32 CurrentTick)
{
	if (!EntityManager.IsEntityValid(Request.Target))
	{
		return EMassForcedMovementFailure::InvalidTarget;
	}
	if (EntityManager.GetFragmentDataPtr<FMassExternalTargetFragment>(Request.Target))
	{
		return EMassForcedMovementFailure::ExternalTargetUnsupported;
	}
	FMassForcedMovementFragment* Movement =
		EntityManager.GetFragmentDataPtr<FMassForcedMovementFragment>(Request.Target);
	const FMassStateFragment* State = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Request.Target);
	const FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Request.Target);
	if (!Movement || !State || !Transform)
	{
		return EMassForcedMovementFailure::MissingFragment;
	}
	if (EnumHasAnyFlags(State->StateFlags, EMassStateFlags::Dying | EMassStateFlags::Dead))
	{
		Movement->Reset();
		return EMassForcedMovementFailure::DeadTarget;
	}
	if (!MovementAdapter && Request.CollisionPolicy != EMassForcedMovementCollisionPolicy::Ignore)
	{
		return EMassForcedMovementFailure::UnsupportedCollision;
	}
	if (Movement->bActive)
	{
		if (Request.Blend == EMassForcedMovementBlend::RejectIfActive)
		{
			return EMassForcedMovementFailure::ActiveRejected;
		}
		if (Request.Priority < Movement->Priority)
		{
			return EMassForcedMovementFailure::LowerPriority;
		}
		if (Request.Blend == EMassForcedMovementBlend::Additive)
		{
			if (Request.Mode != EMassForcedMovementMode::Velocity
				|| Movement->Mode != EMassForcedMovementMode::Velocity
				|| Request.CollisionPolicy != Movement->CollisionPolicy)
			{
				return EMassForcedMovementFailure::UnsupportedBlend;
			}
			Movement->Vector += Request.Vector;
			Movement->EndTick = FMath::Max(Movement->EndTick, CurrentTick + Request.DurationTicks);
			Movement->Priority = Request.Priority;
			Movement->RequestSerial = Request.RequestSerial;
			return EMassForcedMovementFailure::None;
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
		Context.Defer().AddTag<FMassHasForcedMovementTag>(Request.Target);
	}
	return EMassForcedMovementFailure::None;
}

bool UMassForcedMovementSubsystem::TickEntity(const FMassEntityHandle Entity, const uint32 CurrentTick,
	const float TickSeconds, const FMassStateFragment& State, FTransformFragment& Transform,
	FMassForcedMovementFragment& Movement)
{
	if (!Movement.bActive)
	{
		return false;
	}
	++Stats.Active;
	if (EnumHasAnyFlags(State.StateFlags, EMassStateFlags::Dying | EMassStateFlags::Dead))
	{
		Movement.Reset();
		return false;
	}
	const uint32 DurationTicks = FMath::Max(1u, Movement.EndTick - Movement.StartTick);
	const uint32 ExpectedAppliedTicks = FMassSimulationClock::HasReached(CurrentTick, Movement.StartTick)
		? FMath::Min(DurationTicks, static_cast<uint32>(CurrentTick - Movement.StartTick) + 1u) : 0u;
	if (ExpectedAppliedTicks <= Movement.AppliedTicks)
	{
		return true;
	}
	const uint32 DeltaTicks = ExpectedAppliedTicks - Movement.AppliedTicks;
	const FVector CurrentLocation = Transform.GetTransform().GetLocation();
	FVector DesiredDelta = FVector::ZeroVector;
	if (Movement.Mode == EMassForcedMovementMode::Velocity)
	{
		DesiredDelta = Movement.Vector * TickSeconds * static_cast<float>(DeltaTicks);
	}
	else
	{
		const FVector TotalDisplacement = Movement.Mode == EMassForcedMovementMode::ToLocation
			? Movement.Vector - Movement.StartLocation : Movement.Vector;
		DesiredDelta = TotalDisplacement * (static_cast<float>(DeltaTicks) / static_cast<float>(DurationTicks));
		if (ExpectedAppliedTicks == DurationTicks && Movement.Mode == EMassForcedMovementMode::ToLocation)
		{
			DesiredDelta = Movement.Vector - CurrentLocation;
		}
	}

	FMassMovementResolveResult Result;
	if (MovementAdapter)
	{
		FMassMovementResolveParams Params;
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

void UMassForcedMovementSubsystem::RecordFailure(const EMassForcedMovementFailure Failure)
{
	const uint8 Index = static_cast<uint8>(Failure);
	if (Failure != EMassForcedMovementFailure::None && Index < Stats.Failures.Num())
	{
		++Stats.Failures[Index];
	}
}
