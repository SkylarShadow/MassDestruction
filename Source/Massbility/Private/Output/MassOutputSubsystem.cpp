// Copyright MassDestruction. All Rights Reserved.

#include "Output/MassOutputSubsystem.h"

#include "Combat/MassCombatSubsystem.h"
#include "Core/MassRuntimeSettings.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Effect/MassEffectSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassFragments.h"
#include "External/MassExternalTargetFragments.h"
#include "External/MassExternalTargetSubsystem.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Mass.h"

void UMassOutputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassSubsystem = Collection.InitializeDependency<UMassRuntimeSubsystem>();
	EffectSubsystem = Collection.InitializeDependency<UMassEffectSubsystem>();
	CombatSubsystem = Collection.InitializeDependency<UMassCombatSubsystem>();
	ExternalTargetSubsystem = Collection.InitializeDependency<UMassExternalTargetSubsystem>();
	const UMassRuntimeSettings* Settings = GetDefault<UMassRuntimeSettings>();
	MaxPendingRequests = Settings ? FMath::Max(1, Settings->MaxPendingOutputRequests) : 4096;
	PendingRequests.Reserve(FMath::Min(MaxPendingRequests, 256));
	CompiledOutputs.Reserve(32);
}

void UMassOutputSubsystem::Deinitialize()
{
	PendingRequests.Reset();
	CompiledOutputs.Reset();
	Stats = {};
	ExternalTargetSubsystem = nullptr;
	CombatSubsystem = nullptr;
	EffectSubsystem = nullptr;
	MassSubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassOutputSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassOutputSubsystem* UMassOutputSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassOutputSubsystem>();
	}
	return nullptr;
}

FMassOutputId UMassOutputSubsystem::RegisterOutput(const FMassOutputRegistration& Registration)
{
	const bool bPayloadValid = Registration.Type == EMassAbilityOutputType::Effect
		? Registration.EffectId.IsValid()
		: FMath::IsFinite(Registration.Damage) && Registration.Damage > 0.0f;
	if (!bPayloadValid || !FMath::IsFinite(Registration.MagnitudeScale)
		|| Registration.MagnitudeScale <= 0.0f || CompiledOutputs.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Rejected invalid Mass output registration."));
		return {};
	}

	FMassCompiledOutput Compiled;
	static_cast<FMassOutputRegistration&>(Compiled) = Registration;
	Compiled.OutputId.Value = static_cast<uint16>(CompiledOutputs.Num());
	CompiledOutputs.Add(Compiled);
	++Stats.DefinitionsRegistered;
	return Compiled.OutputId;
}

const FMassCompiledOutput* UMassOutputSubsystem::FindOutput(const FMassOutputId OutputId) const
{
	return OutputId.IsValid() && CompiledOutputs.IsValidIndex(OutputId.Value)
		? &CompiledOutputs[OutputId.Value] : nullptr;
}

bool UMassOutputSubsystem::CanRouteExternalOutput(const FMassOutputId OutputId) const
{
	const FMassCompiledOutput* Output = FindOutput(OutputId);
	return Output && ExternalTargetSubsystem
		&& ExternalTargetSubsystem->CanRouteExternalOutput(Output->SemanticTag);
}

bool UMassOutputSubsystem::QueueOutput(const FMassOutputRequest& Request)
{
	return QueueOutputs(MakeArrayView(&Request, 1));
}

bool UMassOutputSubsystem::QueueOutputs(const TConstArrayView<FMassOutputRequest> Requests)
{
	if (!IsInGameThread())
	{
		RecordFailure(EMassOutputFailure::WrongThread);
		return false;
	}
	if (Requests.IsEmpty())
	{
		return false;
	}
	if (Requests.Num() > MaxPendingRequests - PendingRequests.Num())
	{
		RecordFailure(EMassOutputFailure::Capacity);
		return false;
	}
	for (const FMassOutputRequest& Request : Requests)
	{
		const FMassCompiledOutput* Output = FindOutput(Request.OutputId);
		if (!Output)
		{
			RecordFailure(EMassOutputFailure::InvalidOutput);
			return false;
		}
		if (!Request.Target.IsSet())
		{
			RecordFailure(EMassOutputFailure::InvalidTarget);
			return false;
		}
		if (!Request.Source.IsSet() && !Output->bAllowInvalidSource)
		{
			RecordFailure(EMassOutputFailure::InvalidSource);
			return false;
		}
		if (!FMath::IsFinite(Request.MagnitudeScale) || Request.MagnitudeScale <= 0.0f)
		{
			RecordFailure(EMassOutputFailure::DownstreamRejected);
			return false;
		}
	}

	PendingRequests.Append(Requests.GetData(), Requests.Num());
	Stats.RequestsQueued += static_cast<uint64>(Requests.Num());
	Stats.QueueHighWaterMark = FMath::Max(Stats.QueueHighWaterMark, static_cast<uint64>(PendingRequests.Num()));
	return true;
}

void UMassOutputSubsystem::FlushRequests(FMassEntityManager& EntityManager, const uint32 CurrentTick)
{
	for (const FMassOutputRequest& Request : PendingRequests)
	{
		const EMassOutputFailure Failure = Dispatch(EntityManager, Request, CurrentTick);
		if (Failure == EMassOutputFailure::None)
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

EMassOutputFailure UMassOutputSubsystem::Dispatch(FMassEntityManager& EntityManager,
	const FMassOutputRequest& Request, const uint32 CurrentTick)
{
	const FMassCompiledOutput* Output = FindOutput(Request.OutputId);
	if (!Output)
	{
		return EMassOutputFailure::InvalidOutput;
	}
	if (!EntityManager.IsEntityValid(Request.Target))
	{
		return EMassOutputFailure::InvalidTarget;
	}

	const bool bSourceValid = EntityManager.IsEntityValid(Request.Source);
	if (!bSourceValid && !Output->bAllowInvalidSource)
	{
		return EMassOutputFailure::InvalidSource;
	}
	const FMassStateFragment* TargetState =
		EntityManager.GetFragmentDataPtr<FMassStateFragment>(Request.Target);
	const FMassUnitFragment* TargetUnit =
		EntityManager.GetFragmentDataPtr<FMassUnitFragment>(Request.Target);
	if (!TargetState || !TargetUnit)
	{
		return EMassOutputFailure::InvalidTarget;
	}
	if ((!Output->bAllowDeadTarget && EnumHasAnyFlags(TargetState->StateFlags,
		EMassStateFlags::Dying | EMassStateFlags::Dead))
		|| (Output->bRequireTargetable && Request.Source != Request.Target
			&& !EnumHasAnyFlags(TargetState->StateFlags, EMassStateFlags::Targetable)))
	{
		return EMassOutputFailure::TargetState;
	}
	if (!TargetState->RuntimeTags.ContainsAll(Output->RequiredTargetTags)
		|| TargetState->RuntimeTags.ContainsAny(Output->BlockedTargetTags))
	{
		return EMassOutputFailure::TargetTags;
	}

	if (Output->RelationPolicy == EMassOutputRelationPolicy::Self)
	{
		if (!bSourceValid || Request.Source != Request.Target)
		{
			return EMassOutputFailure::Relation;
		}
	}
	else if (Output->RelationPolicy != EMassOutputRelationPolicy::Any)
	{
		const FMassUnitFragment* SourceUnit = bSourceValid
			? EntityManager.GetFragmentDataPtr<FMassUnitFragment>(Request.Source) : nullptr;
		if (!SourceUnit)
		{
			return EMassOutputFailure::InvalidSource;
		}
		const bool bFriendly = SourceUnit->TeamId == TargetUnit->TeamId;
		if ((Output->RelationPolicy == EMassOutputRelationPolicy::SelfOrHostile
				&& Request.Source != Request.Target && bFriendly)
			|| (Output->RelationPolicy == EMassOutputRelationPolicy::Friendly && !bFriendly)
			|| (Output->RelationPolicy == EMassOutputRelationPolicy::Hostile
				&& (bFriendly || Request.Source == Request.Target)))
		{
			return EMassOutputFailure::Relation;
		}
	}

	if (const FMassExternalTargetFragment* External =
		EntityManager.GetFragmentDataPtr<FMassExternalTargetFragment>(Request.Target))
	{
		if (!ExternalTargetSubsystem || !ExternalTargetSubsystem->IsExternalTargetValid(External->Handle))
		{
			return EMassOutputFailure::InvalidTarget;
		}
		if (!ExternalTargetSubsystem->CanRouteExternalOutput(Output->SemanticTag))
		{
			return EMassOutputFailure::NoExternalRoute;
		}
		FMassExternalOutputRequest ExternalRequest;
		ExternalRequest.Source = bSourceValid ? Request.Source : FMassEntityHandle();
		ExternalRequest.Target = External->Handle;
		ExternalRequest.OutputId = Request.OutputId;
		ExternalRequest.MagnitudeScale = Request.MagnitudeScale;
		ExternalRequest.Origin = Request.Origin;
		ExternalRequest.Direction = Request.Direction.GetSafeNormal();
		ExternalRequest.SimulationTick = Request.SimulationTick != 0 ? Request.SimulationTick : CurrentTick;
		if (!ExternalTargetSubsystem->QueueExternalOutput(ExternalRequest, Output->SemanticTag))
		{
			return EMassOutputFailure::DownstreamRejected;
		}
		++Stats.RoutedToExternal;
		return EMassOutputFailure::None;
	}

	const float FinalScale = Output->MagnitudeScale * Request.MagnitudeScale;
	const FMassEntityHandle ResolvedSource = bSourceValid ? Request.Source : FMassEntityHandle();
	const bool bAccepted = Output->Type == EMassAbilityOutputType::Effect
		? EffectSubsystem && EffectSubsystem->QueueRegisteredEffect(
			Request.Target, Output->EffectId, ResolvedSource, FinalScale)
		: CombatSubsystem && CombatSubsystem->QueueDamage(Request.Target, Output->Damage * FinalScale,
			ResolvedSource, Output->EffectId, Request.Origin, Request.Direction);
	if (!bAccepted)
	{
		return EMassOutputFailure::DownstreamRejected;
	}
	++Stats.RoutedToMass;
	return EMassOutputFailure::None;
}

void UMassOutputSubsystem::RecordFailure(const EMassOutputFailure Failure)
{
	const uint8 Index = static_cast<uint8>(Failure);
	if (Failure != EMassOutputFailure::None && Index < Stats.Failures.Num())
	{
		++Stats.Failures[Index];
	}
}
