// Copyright MassDestruction. All Rights Reserved.

#include "Output/MassbilityOutputSubsystem.h"

#include "Combat/MassbilityCombatSubsystem.h"
#include "Core/MassbilitySettings.h"
#include "Core/MassbilitySubsystem.h"
#include "Effect/MassbilityEffectSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassbilityFragments.h"
#include "External/MassbilityExternalTargetFragments.h"
#include "External/MassbilityExternalTargetSubsystem.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Massbility.h"

void UMassbilityOutputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassbilitySubsystem = Collection.InitializeDependency<UMassbilitySubsystem>();
	EffectSubsystem = Collection.InitializeDependency<UMassbilityEffectSubsystem>();
	CombatSubsystem = Collection.InitializeDependency<UMassbilityCombatSubsystem>();
	ExternalTargetSubsystem = Collection.InitializeDependency<UMassbilityExternalTargetSubsystem>();
	const UMassbilitySettings* Settings = GetDefault<UMassbilitySettings>();
	MaxPendingRequests = Settings ? FMath::Max(1, Settings->MaxPendingOutputRequests) : 4096;
	PendingRequests.Reserve(FMath::Min(MaxPendingRequests, 256));
	CompiledOutputs.Reserve(32);
}

void UMassbilityOutputSubsystem::Deinitialize()
{
	PendingRequests.Reset();
	CompiledOutputs.Reset();
	Stats = {};
	ExternalTargetSubsystem = nullptr;
	CombatSubsystem = nullptr;
	EffectSubsystem = nullptr;
	MassbilitySubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassbilityOutputSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassbilityOutputSubsystem* UMassbilityOutputSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassbilityOutputSubsystem>();
	}
	return nullptr;
}

FMassbilityOutputId UMassbilityOutputSubsystem::RegisterOutput(const FMassbilityOutputRegistration& Registration)
{
	const bool bPayloadValid = Registration.Type == EMassbilityAbilityOutputType::Effect
		? Registration.EffectId.IsValid()
		: FMath::IsFinite(Registration.Damage) && Registration.Damage > 0.0f;
	if (!bPayloadValid || !FMath::IsFinite(Registration.MagnitudeScale)
		|| Registration.MagnitudeScale <= 0.0f || CompiledOutputs.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Rejected invalid Massbility output registration."));
		return {};
	}

	FMassbilityCompiledOutput Compiled;
	static_cast<FMassbilityOutputRegistration&>(Compiled) = Registration;
	Compiled.OutputId.Value = static_cast<uint16>(CompiledOutputs.Num());
	CompiledOutputs.Add(Compiled);
	++Stats.DefinitionsRegistered;
	return Compiled.OutputId;
}

const FMassbilityCompiledOutput* UMassbilityOutputSubsystem::FindOutput(const FMassbilityOutputId OutputId) const
{
	return OutputId.IsValid() && CompiledOutputs.IsValidIndex(OutputId.Value)
		? &CompiledOutputs[OutputId.Value] : nullptr;
}

bool UMassbilityOutputSubsystem::CanRouteExternalOutput(const FMassbilityOutputId OutputId) const
{
	const FMassbilityCompiledOutput* Output = FindOutput(OutputId);
	return Output && ExternalTargetSubsystem
		&& ExternalTargetSubsystem->CanRouteExternalOutput(Output->SemanticTag);
}

bool UMassbilityOutputSubsystem::QueueOutput(const FMassbilityOutputRequest& Request)
{
	return QueueOutputs(MakeArrayView(&Request, 1));
}

bool UMassbilityOutputSubsystem::QueueOutputs(const TConstArrayView<FMassbilityOutputRequest> Requests)
{
	if (!IsInGameThread())
	{
		RecordFailure(EMassbilityOutputFailure::WrongThread);
		return false;
	}
	if (Requests.IsEmpty())
	{
		return false;
	}
	if (Requests.Num() > MaxPendingRequests - PendingRequests.Num())
	{
		RecordFailure(EMassbilityOutputFailure::Capacity);
		return false;
	}
	for (const FMassbilityOutputRequest& Request : Requests)
	{
		const FMassbilityCompiledOutput* Output = FindOutput(Request.OutputId);
		if (!Output)
		{
			RecordFailure(EMassbilityOutputFailure::InvalidOutput);
			return false;
		}
		if (!Request.Target.IsSet())
		{
			RecordFailure(EMassbilityOutputFailure::InvalidTarget);
			return false;
		}
		if (!Request.Source.IsSet() && !Output->bAllowInvalidSource)
		{
			RecordFailure(EMassbilityOutputFailure::InvalidSource);
			return false;
		}
		if (!FMath::IsFinite(Request.MagnitudeScale) || Request.MagnitudeScale <= 0.0f)
		{
			RecordFailure(EMassbilityOutputFailure::DownstreamRejected);
			return false;
		}
	}

	PendingRequests.Append(Requests.GetData(), Requests.Num());
	Stats.RequestsQueued += static_cast<uint64>(Requests.Num());
	Stats.QueueHighWaterMark = FMath::Max(Stats.QueueHighWaterMark, static_cast<uint64>(PendingRequests.Num()));
	return true;
}

void UMassbilityOutputSubsystem::FlushRequests(FMassEntityManager& EntityManager, const uint32 CurrentTick)
{
	for (const FMassbilityOutputRequest& Request : PendingRequests)
	{
		const EMassbilityOutputFailure Failure = Dispatch(EntityManager, Request, CurrentTick);
		if (Failure == EMassbilityOutputFailure::None)
		{
			++Stats.RequestsDispatched;
		}
		else
		{
			RecordFailure(Failure);
		}
	}
	PendingRequests.Reset();
}

EMassbilityOutputFailure UMassbilityOutputSubsystem::Dispatch(FMassEntityManager& EntityManager,
	const FMassbilityOutputRequest& Request, const uint32 CurrentTick)
{
	const FMassbilityCompiledOutput* Output = FindOutput(Request.OutputId);
	if (!Output)
	{
		return EMassbilityOutputFailure::InvalidOutput;
	}
	if (!EntityManager.IsEntityValid(Request.Target))
	{
		return EMassbilityOutputFailure::InvalidTarget;
	}

	const bool bSourceValid = EntityManager.IsEntityValid(Request.Source);
	if (!bSourceValid && !Output->bAllowInvalidSource)
	{
		return EMassbilityOutputFailure::InvalidSource;
	}
	const FMassbilityStateFragment* TargetState =
		EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Request.Target);
	const FMassbilityUnitFragment* TargetUnit =
		EntityManager.GetFragmentDataPtr<FMassbilityUnitFragment>(Request.Target);
	if (!TargetState || !TargetUnit)
	{
		return EMassbilityOutputFailure::InvalidTarget;
	}
	if ((!Output->bAllowDeadTarget && EnumHasAnyFlags(TargetState->StateFlags,
		EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
		|| (Output->bRequireTargetable && Request.Source != Request.Target
			&& !EnumHasAnyFlags(TargetState->StateFlags, EMassbilityStateFlags::Targetable)))
	{
		return EMassbilityOutputFailure::TargetState;
	}
	if (!TargetState->RuntimeTags.ContainsAll(Output->RequiredTargetTags)
		|| TargetState->RuntimeTags.ContainsAny(Output->BlockedTargetTags))
	{
		return EMassbilityOutputFailure::TargetTags;
	}

	if (Output->RelationPolicy == EMassbilityOutputRelationPolicy::Self)
	{
		if (!bSourceValid || Request.Source != Request.Target)
		{
			return EMassbilityOutputFailure::Relation;
		}
	}
	else if (Output->RelationPolicy != EMassbilityOutputRelationPolicy::Any)
	{
		const FMassbilityUnitFragment* SourceUnit = bSourceValid
			? EntityManager.GetFragmentDataPtr<FMassbilityUnitFragment>(Request.Source) : nullptr;
		if (!SourceUnit)
		{
			return EMassbilityOutputFailure::InvalidSource;
		}
		const bool bFriendly = SourceUnit->TeamId == TargetUnit->TeamId;
		if ((Output->RelationPolicy == EMassbilityOutputRelationPolicy::SelfOrHostile
				&& Request.Source != Request.Target && bFriendly)
			|| (Output->RelationPolicy == EMassbilityOutputRelationPolicy::Friendly && !bFriendly)
			|| (Output->RelationPolicy == EMassbilityOutputRelationPolicy::Hostile
				&& (bFriendly || Request.Source == Request.Target)))
		{
			return EMassbilityOutputFailure::Relation;
		}
	}

	if (const FMassbilityExternalTargetFragment* External =
		EntityManager.GetFragmentDataPtr<FMassbilityExternalTargetFragment>(Request.Target))
	{
		if (!ExternalTargetSubsystem || !ExternalTargetSubsystem->IsExternalTargetValid(External->Handle))
		{
			return EMassbilityOutputFailure::InvalidTarget;
		}
		if (!ExternalTargetSubsystem->CanRouteExternalOutput(Output->SemanticTag))
		{
			return EMassbilityOutputFailure::NoExternalRoute;
		}
		FMassbilityExternalOutputRequest ExternalRequest;
		ExternalRequest.Source = bSourceValid ? Request.Source : FMassEntityHandle();
		ExternalRequest.Target = External->Handle;
		ExternalRequest.OutputId = Request.OutputId;
		ExternalRequest.MagnitudeScale = Request.MagnitudeScale;
		ExternalRequest.Origin = Request.Origin;
		ExternalRequest.Direction = Request.Direction.GetSafeNormal();
		ExternalRequest.SimulationTick = Request.SimulationTick != 0 ? Request.SimulationTick : CurrentTick;
		if (!ExternalTargetSubsystem->QueueExternalOutput(ExternalRequest, Output->SemanticTag))
		{
			return EMassbilityOutputFailure::DownstreamRejected;
		}
		++Stats.RoutedToExternal;
		return EMassbilityOutputFailure::None;
	}

	const float FinalScale = Output->MagnitudeScale * Request.MagnitudeScale;
	const FMassEntityHandle ResolvedSource = bSourceValid ? Request.Source : FMassEntityHandle();
	const bool bAccepted = Output->Type == EMassbilityAbilityOutputType::Effect
		? EffectSubsystem && EffectSubsystem->QueueRegisteredEffect(
			Request.Target, Output->EffectId, ResolvedSource, FinalScale)
		: CombatSubsystem && CombatSubsystem->QueueDamage(Request.Target, Output->Damage * FinalScale,
			ResolvedSource, Output->EffectId, Request.Origin, Request.Direction);
	if (!bAccepted)
	{
		return EMassbilityOutputFailure::DownstreamRejected;
	}
	++Stats.RoutedToMass;
	return EMassbilityOutputFailure::None;
}

void UMassbilityOutputSubsystem::RecordFailure(const EMassbilityOutputFailure Failure)
{
	const uint8 Index = static_cast<uint8>(Failure);
	if (Failure != EMassbilityOutputFailure::None && Index < Stats.Failures.Num())
	{
		++Stats.Failures[Index];
	}
}
