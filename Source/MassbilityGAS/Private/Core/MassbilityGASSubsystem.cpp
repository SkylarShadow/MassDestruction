// Copyright MassDestruction. All Rights Reserved.

#include "Core/MassbilityGASSubsystem.h"

#include "AbilitySystemComponent.h"
#include "Combat/MassbilityCombatSubsystem.h"
#include "Context/MassbilityGameplayEffectContext.h"
#include "Core/MassbilityGASBridgeActor.h"
#include "Core/MassbilityGASSettings.h"
#include "Core/MassbilitySubsystem.h"
#include "Effect/MassbilityEffectDefinition.h"
#include "Effect/MassbilityEffectSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassbilityFragments.h"
#include "External/MassbilityExternalTargetSubsystem.h"
#include "GameplayEffect.h"
#include "MassbilityGAS.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Output/MassbilityOutputSubsystem.h"

void UMassbilityGASSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassbilitySubsystem = Collection.InitializeDependency<UMassbilitySubsystem>();
	EffectSubsystem = Collection.InitializeDependency<UMassbilityEffectSubsystem>();
	OutputSubsystem = Collection.InitializeDependency<UMassbilityOutputSubsystem>();
	CombatSubsystem = Collection.InitializeDependency<UMassbilityCombatSubsystem>();
	ExternalTargetSubsystem = Collection.InitializeDependency<UMassbilityExternalTargetSubsystem>();
	Endpoints.Reserve(8);
	OutboundScratch.Reserve(64);
	CompileMappings();
}

void UMassbilityGASSubsystem::Deinitialize()
{
	ReleaseRoutes();
	for (int32 Index = Endpoints.Num() - 1; Index >= 0; --Index)
	{
		RemoveEndpointAt(Index);
	}
	if (BridgeActor)
	{
		BridgeActor->Destroy();
	}
	BridgeActor = nullptr;
	OutboundScratch.Reset();
	RegisteredRouteTags.Reset();
	InboundEffectIds.Reset();
	InboundMappingIndices.Reset();
	OutboundMappingIndices.Reset();
	MappingAsset = nullptr;
	ExternalTargetSubsystem = nullptr;
	CombatSubsystem = nullptr;
	OutputSubsystem = nullptr;
	EffectSubsystem = nullptr;
	MassbilitySubsystem = nullptr;
	EntitySubsystem = nullptr;
	Stats = {};
	Super::Deinitialize();
}

bool UMassbilityGASSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassbilityGASSubsystem* UMassbilityGASSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassbilityGASSubsystem>();
	}
	return nullptr;
}

FMassbilityExternalTargetHandle UMassbilityGASSubsystem::RegisterAbilitySystemTarget(
	UAbilitySystemComponent* AbilitySystem, const int16 TeamId, const bool bTargetable)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !AbilitySystem
		|| AbilitySystem->GetWorld() != GetWorld() || !ExternalTargetSubsystem)
	{
		return {};
	}
	if (const int32 ExistingIndex = FindEndpointIndex(AbilitySystem); ExistingIndex != INDEX_NONE)
	{
		FEndpoint& Existing = Endpoints[ExistingIndex];
		Existing.TeamId = TeamId;
		Existing.bTargetable = bTargetable;
		FMassbilityExternalTargetSnapshot Snapshot;
		if (BuildSnapshot(Existing, Snapshot))
		{
			ExternalTargetSubsystem->UpdateExternalTarget(Existing.Handle, Snapshot);
		}
		return Existing.Handle;
	}

	FEndpoint Endpoint;
	Endpoint.AbilitySystem = AbilitySystem;
	Endpoint.AvatarActor = AbilitySystem->GetAvatarActor_Direct();
	Endpoint.TeamId = TeamId;
	Endpoint.bTargetable = bTargetable;
	FMassbilityExternalTargetSnapshot Snapshot;
	if (!BuildSnapshot(Endpoint, Snapshot)
		|| !ExternalTargetSubsystem->RegisterExternalTarget(Snapshot, Endpoint.Handle, Endpoint.ProxyEntity))
	{
		return {};
	}
	Endpoints.Add(Endpoint);
	++Stats.EndpointsRegistered;
	return Endpoint.Handle;
}

bool UMassbilityGASSubsystem::UpdateAbilitySystemTargetTeam(
	const FMassbilityExternalTargetHandle Handle, const int16 TeamId)
{
	const int32 Index = FindEndpointIndex(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Endpoints[Index].TeamId = TeamId;
	FMassbilityExternalTargetSnapshot Snapshot;
	return BuildSnapshot(Endpoints[Index], Snapshot)
		&& ExternalTargetSubsystem->UpdateExternalTarget(Handle, Snapshot);
}

bool UMassbilityGASSubsystem::SetAbilitySystemTargetable(
	const FMassbilityExternalTargetHandle Handle, const bool bTargetable)
{
	const int32 Index = FindEndpointIndex(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Endpoints[Index].bTargetable = bTargetable;
	FMassbilityExternalTargetSnapshot Snapshot;
	return BuildSnapshot(Endpoints[Index], Snapshot)
		&& ExternalTargetSubsystem->UpdateExternalTarget(Handle, Snapshot);
}

bool UMassbilityGASSubsystem::UnregisterAbilitySystemTarget(const FMassbilityExternalTargetHandle Handle)
{
	const int32 Index = FindEndpointIndex(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	RemoveEndpointAt(Index);
	return true;
}

UAbilitySystemComponent* UMassbilityGASSubsystem::ResolveAbilitySystem(
	const FMassbilityExternalTargetHandle Handle) const
{
	const int32 Index = FindEndpointIndex(Handle);
	return Index != INDEX_NONE ? Endpoints[Index].AbilitySystem.Get() : nullptr;
}

bool UMassbilityGASSubsystem::CompileMappings()
{
	// 在世界初始化时编译一次。运行时 Processor 只使用整数和映射查找，
	// 热路径不会读取作者资产。
	ReleaseRoutes();
	InboundEffectIds.Reset();
	InboundMappingIndices.Reset();
	OutboundMappingIndices.Reset();
	MappingAsset = nullptr;

	const UMassbilityGASSettings* Settings = GetDefault<UMassbilityGASSettings>();
	MappingAsset = Settings ? Settings->EffectMapping.LoadSynchronous() : nullptr;
	if (!MappingAsset)
	{
		UE_LOG(LogMassbilityGAS, Warning, TEXT("No Massbility GAS effect mapping is configured."));
		return false;
	}

	const TArray<FMassbilityGASOutboundEffectMapping>& Outbound = MappingAsset->GetOutboundMappings();
	for (int32 Index = 0; Index < Outbound.Num(); ++Index)
	{
		const FMassbilityGASOutboundEffectMapping& Entry = Outbound[Index];
		if (!Entry.SemanticTag.IsValid() || !Entry.GameplayEffectClass || !FMath::IsFinite(Entry.GameplayEffectLevel)
			|| Entry.GameplayEffectLevel < 0.0f || OutboundMappingIndices.Contains(Entry.SemanticTag))
		{
			UE_LOG(LogMassbilityGAS, Error, TEXT("Invalid or duplicate outbound GAS mapping at index %d."), Index);
			OutboundMappingIndices.Reset();
			return false;
		}
		OutboundMappingIndices.Add(Entry.SemanticTag, Index);
	}

	const TArray<FMassbilityGASInboundEffectMapping>& Inbound = MappingAsset->GetInboundMappings();
	InboundEffectIds.SetNum(Inbound.Num());
	for (int32 Index = 0; Index < Inbound.Num(); ++Index)
	{
		const FMassbilityGASInboundEffectMapping& Entry = Inbound[Index];
		UClass* EffectClass = Entry.GameplayEffectClass.Get();
		if (!EffectClass || !FMath::IsFinite(Entry.DefaultMagnitude) || InboundMappingIndices.Contains(EffectClass)
			|| (Entry.Policy == EMassbilityGASInboundPolicy::Effect && !Entry.MassEffect))
		{
			UE_LOG(LogMassbilityGAS, Error, TEXT("Invalid or duplicate inbound GAS mapping at index %d."), Index);
			OutboundMappingIndices.Reset();
			InboundMappingIndices.Reset();
			InboundEffectIds.Reset();
			return false;
		}
		InboundMappingIndices.Add(EffectClass, Index);
		if (Entry.Policy == EMassbilityGASInboundPolicy::Effect)
		{
			InboundEffectIds[Index] = EffectSubsystem->RegisterEffect(*Entry.MassEffect);
			if (!InboundEffectIds[Index].IsValid())
			{
				UE_LOG(LogMassbilityGAS, Error, TEXT("Could not register Mass effect for inbound mapping %d."), Index);
				OutboundMappingIndices.Reset();
				InboundMappingIndices.Reset();
				InboundEffectIds.Reset();
				return false;
			}
		}
	}

	for (const FMassbilityGASOutboundEffectMapping& Entry : Outbound)
	{
		if (!ExternalTargetSubsystem->RegisterExternalOutputRoute(Entry.SemanticTag))
		{
			ReleaseRoutes();
			OutboundMappingIndices.Reset();
			InboundMappingIndices.Reset();
			InboundEffectIds.Reset();
			return false;
		}
		RegisteredRouteTags.Add(Entry.SemanticTag);
	}
	return true;
}

void UMassbilityGASSubsystem::ReleaseRoutes()
{
	if (ExternalTargetSubsystem)
	{
		for (const FGameplayTag Tag : RegisteredRouteTags)
		{
			ExternalTargetSubsystem->UnregisterExternalOutputRoute(Tag);
		}
	}
	RegisteredRouteTags.Reset();
}

bool UMassbilityGASSubsystem::BuildSnapshot(const FEndpoint& Endpoint,
	FMassbilityExternalTargetSnapshot& OutSnapshot) const
{
	UAbilitySystemComponent* AbilitySystem = Endpoint.AbilitySystem.Get();
	AActor* Avatar = AbilitySystem ? AbilitySystem->GetAvatarActor_Direct() : nullptr;
	if (!AbilitySystem || !Avatar || !MassbilitySubsystem)
	{
		return false;
	}
	OutSnapshot = {};
	OutSnapshot.Transform = Avatar->GetActorTransform();
	OutSnapshot.TeamId = Endpoint.TeamId;
	OutSnapshot.bTargetable = Endpoint.bTargetable;
	FGameplayTagContainer OwnedTags;
	AbilitySystem->GetOwnedGameplayTags(OwnedTags);
	MassbilitySubsystem->GetRuntimeTagCatalog().BuildMaskFiltered(OwnedTags, OutSnapshot.RuntimeTags);
	return true;
}

void UMassbilityGASSubsystem::SyncEndpoints()
{
	if (!ExternalTargetSubsystem)
	{
		return;
	}
	for (int32 Index = Endpoints.Num() - 1; Index >= 0; --Index)
	{
		// 当 ASC 或 Avatar 已失效时在这里移除 Endpoint，避免过期代理进入 SpatialBuild。
		FEndpoint& Endpoint = Endpoints[Index];
		UAbilitySystemComponent* AbilitySystem = Endpoint.AbilitySystem.Get();
		AActor* Avatar = AbilitySystem ? AbilitySystem->GetAvatarActor_Direct() : nullptr;
		if (!AbilitySystem || !Avatar)
		{
			RemoveEndpointAt(Index);
			continue;
		}
		Endpoint.AvatarActor = Avatar;
		FMassbilityExternalTargetSnapshot Snapshot;
		if (BuildSnapshot(Endpoint, Snapshot)
			&& ExternalTargetSubsystem->UpdateExternalTarget(Endpoint.Handle, Snapshot))
		{
			++Stats.ProxyUpdates;
		}
	}
}

void UMassbilityGASSubsystem::FlushOutboundRequests()
{
	if (!ExternalTargetSubsystem)
	{
		return;
	}
	OutboundScratch.Reset();
	ExternalTargetSubsystem->ConsumeExternalOutputRequests(OutboundScratch);
	for (const FMassbilityExternalOutputRequest& Request : OutboundScratch)
	{
		++Stats.OutboundRequestsReceived;
		ApplyOutboundRequest(Request);
	}
	OutboundScratch.Reset();
}

bool UMassbilityGASSubsystem::ApplyOutboundRequest(const FMassbilityExternalOutputRequest& Request)
{
	// 这是 Mass 输出转换为原生 GameplayEffectSpec 的唯一 MassbilityGAS 路径。
	// Mass 核心模块不会包含 GameplayAbilities 依赖。
	UAbilitySystemComponent* TargetAbilitySystem = ResolveAbilitySystem(Request.Target);
	if (!TargetAbilitySystem)
	{
		++Stats.OutboundRejectedInvalidTarget;
		return false;
	}
	const FMassbilityCompiledOutput* Output = OutputSubsystem ? OutputSubsystem->FindOutput(Request.OutputId) : nullptr;
	if (!Output)
	{
		++Stats.OutboundSpecFailures;
		return false;
	}
	const int32* MappingIndex = OutboundMappingIndices.Find(Output->SemanticTag);
	if (!MappingIndex || !MappingAsset || !MappingAsset->GetOutboundMappings().IsValidIndex(*MappingIndex))
	{
		++Stats.OutboundRejectedNoMapping;
		return false;
	}
	const FMassbilityGASOutboundEffectMapping& Mapping = MappingAsset->GetOutboundMappings()[*MappingIndex];
	AMassbilityGASBridgeActor* SourceActor = GetOrCreateBridgeActor();
	UAbilitySystemComponent* SourceAbilitySystem = SourceActor ? SourceActor->GetAbilitySystemComponent() : nullptr;
	if (!SourceAbilitySystem)
	{
		++Stats.OutboundSpecFailures;
		return false;
	}

	FMassbilityGameplayEffectContext* MassContext = new FMassbilityGameplayEffectContext();
	MassContext->SourceMassEntity = Request.Source;
	MassContext->SemanticTag = Output->SemanticTag;
	const float FinalScale = Output->MagnitudeScale * Request.MagnitudeScale;
	MassContext->RawMagnitude = Output->Type == EMassbilityAbilityOutputType::Damage
		? Output->Damage * FinalScale : FinalScale;
	MassContext->Origin = Request.Origin;
	MassContext->Direction = Request.Direction;
	MassContext->SimulationTick = Request.SimulationTick;
	MassContext->AddInstigator(SourceActor, SourceActor);
	FGameplayEffectContextHandle ContextHandle(MassContext);
	FGameplayEffectSpecHandle SpecHandle = SourceAbilitySystem->MakeOutgoingSpec(
		Mapping.GameplayEffectClass, Mapping.GameplayEffectLevel, ContextHandle);
	if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
	{
		++Stats.OutboundSpecFailures;
		return false;
	}
	SpecHandle.Data->AddDynamicAssetTag(Output->SemanticTag);
	if (Mapping.MagnitudeSetByCallerTag.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(Mapping.MagnitudeSetByCallerTag, MassContext->RawMagnitude);
	}
	SourceAbilitySystem->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetAbilitySystem);
	++Stats.OutboundSpecsApplied;
	return true;
}

AMassbilityGASBridgeActor* UMassbilityGASSubsystem::GetOrCreateBridgeActor()
{
	if (BridgeActor)
	{
		return BridgeActor;
	}
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Name = MakeUniqueObjectName(World, AMassbilityGASBridgeActor::StaticClass(), TEXT("MassbilityGASBridge"));
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	BridgeActor = World->SpawnActor<AMassbilityGASBridgeActor>(Params);
	if (BridgeActor && BridgeActor->GetAbilitySystemComponent())
	{
		BridgeActor->GetAbilitySystemComponent()->InitAbilityActorInfo(BridgeActor, BridgeActor);
	}
	return BridgeActor;
}

int32 UMassbilityGASSubsystem::FindEndpointIndex(const FMassbilityExternalTargetHandle Handle) const
{
	return Endpoints.IndexOfByPredicate([Handle](const FEndpoint& Endpoint) { return Endpoint.Handle == Handle; });
}

int32 UMassbilityGASSubsystem::FindEndpointIndex(const UAbilitySystemComponent* AbilitySystem) const
{
	return Endpoints.IndexOfByPredicate([AbilitySystem](const FEndpoint& Endpoint)
	{
		return Endpoint.AbilitySystem.Get() == AbilitySystem;
	});
}

void UMassbilityGASSubsystem::RemoveEndpointAt(const int32 Index)
{
	if (!Endpoints.IsValidIndex(Index))
	{
		return;
	}
	if (ExternalTargetSubsystem)
	{
		ExternalTargetSubsystem->UnregisterExternalTarget(Endpoints[Index].Handle);
	}
	Endpoints.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	++Stats.EndpointsUnregistered;
}

bool UMassbilityGASSubsystem::ApplyGameplayEffectSpecToMassTarget(const FGameplayEffectSpec& Spec,
	const FMassEntityHandle Target, const FHitResult* OptionalHitResult)
{
	// GAS TargetData 从这里进入权威端；将其转换为已有的 Mass 战斗或效果队列，
	// 不在此处直接修改 Mass Fragment。
	++Stats.InboundSpecsReceived;
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !Spec.Def || !EntitySubsystem
		|| !EntitySubsystem->GetEntityManager().IsEntityValid(Target)
		|| !MappingAsset || !EffectSubsystem || !CombatSubsystem)
	{
		return false;
	}
	const FMassbilityStateFragment* State =
		EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassbilityStateFragment>(Target);
	if (!State || !EnumHasAnyFlags(State->StateFlags, EMassbilityStateFlags::Targetable)
		|| EnumHasAnyFlags(State->StateFlags, EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
	{
		return false;
	}
	const int32* MappingIndex = InboundMappingIndices.Find(Spec.Def->GetClass());
	if (!MappingIndex || !MappingAsset->GetInboundMappings().IsValidIndex(*MappingIndex))
	{
		++Stats.InboundRejectedNoMapping;
		return false;
	}
	const FMassbilityGASInboundEffectMapping& Mapping = MappingAsset->GetInboundMappings()[*MappingIndex];
	const float Magnitude = Mapping.MagnitudeSetByCallerTag.IsValid()
		? Spec.GetSetByCallerMagnitude(Mapping.MagnitudeSetByCallerTag, false, Mapping.DefaultMagnitude)
		: Mapping.DefaultMagnitude;
	if (!FMath::IsFinite(Magnitude) || Magnitude <= 0.0f)
	{
		return false;
	}

	FMassEntityHandle Source;
	if (UAbilitySystemComponent* SourceASC = Spec.GetContext().GetInstigatorAbilitySystemComponent())
	{
		const int32 SourceIndex = FindEndpointIndex(SourceASC);
		if (SourceIndex != INDEX_NONE)
		{
			Source = Endpoints[SourceIndex].ProxyEntity;
		}
	}
	const FVector Origin = OptionalHitResult ? OptionalHitResult->TraceStart : Spec.GetContext().GetOrigin();
	const FVector HitLocation = OptionalHitResult ? OptionalHitResult->ImpactPoint : Origin;
	const FVector Direction = (HitLocation - Origin).GetSafeNormal();
	if (Mapping.Policy == EMassbilityGASInboundPolicy::Damage)
	{
		if (CombatSubsystem->QueueDamage(Target, Magnitude, Source, {}, Origin, Direction))
		{
			++Stats.InboundAppliedAsDamage;
			return true;
		}
		return false;
	}
	if (InboundEffectIds.IsValidIndex(*MappingIndex)
		&& EffectSubsystem->QueueRegisteredEffect(Target, InboundEffectIds[*MappingIndex], Source, Magnitude))
	{
		++Stats.InboundAppliedAsEffect;
		return true;
	}
	return false;
}
