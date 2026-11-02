// Copyright MassDestruction. All Rights Reserved.

#include "Core/MassGASSubsystem.h"

#include "AbilitySystemComponent.h"
#include "Combat/MassCombatSubsystem.h"
#include "Context/MassGASGameplayEffectContext.h"
#include "Core/MassGASBridgeActor.h"
#include "Core/MassGASSettings.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Effect/MassEffectDefinition.h"
#include "Effect/MassEffectSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassFragments.h"
#include "External/MassExternalTargetSubsystem.h"
#include "GameplayEffect.h"
#include "MassGAS.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Output/MassOutputSubsystem.h"

void UMassGASSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassSubsystem = Collection.InitializeDependency<UMassRuntimeSubsystem>();
	EffectSubsystem = Collection.InitializeDependency<UMassEffectSubsystem>();
	OutputSubsystem = Collection.InitializeDependency<UMassOutputSubsystem>();
	CombatSubsystem = Collection.InitializeDependency<UMassCombatSubsystem>();
	ExternalTargetSubsystem = Collection.InitializeDependency<UMassExternalTargetSubsystem>();
	Endpoints.Reserve(8);
	OutboundScratch.Reserve(64);
	CompileMappings();
}

void UMassGASSubsystem::Deinitialize()
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
	MassSubsystem = nullptr;
	EntitySubsystem = nullptr;
	Stats = {};
	Super::Deinitialize();
}

bool UMassGASSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassGASSubsystem* UMassGASSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassGASSubsystem>();
	}
	return nullptr;
}

FMassExternalTargetHandle UMassGASSubsystem::RegisterAbilitySystemTarget(
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
		FMassExternalTargetSnapshot Snapshot;
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
	FMassExternalTargetSnapshot Snapshot;
	if (!BuildSnapshot(Endpoint, Snapshot)
		|| !ExternalTargetSubsystem->RegisterExternalTarget(Snapshot, Endpoint.Handle, Endpoint.ProxyEntity))
	{
		return {};
	}
	Endpoints.Add(Endpoint);
	++Stats.EndpointsRegistered;
	return Endpoint.Handle;
}

bool UMassGASSubsystem::UpdateAbilitySystemTargetTeam(
	const FMassExternalTargetHandle Handle, const int16 TeamId)
{
	const int32 Index = FindEndpointIndex(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Endpoints[Index].TeamId = TeamId;
	FMassExternalTargetSnapshot Snapshot;
	return BuildSnapshot(Endpoints[Index], Snapshot)
		&& ExternalTargetSubsystem->UpdateExternalTarget(Handle, Snapshot);
}

bool UMassGASSubsystem::SetAbilitySystemTargetable(
	const FMassExternalTargetHandle Handle, const bool bTargetable)
{
	const int32 Index = FindEndpointIndex(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Endpoints[Index].bTargetable = bTargetable;
	FMassExternalTargetSnapshot Snapshot;
	return BuildSnapshot(Endpoints[Index], Snapshot)
		&& ExternalTargetSubsystem->UpdateExternalTarget(Handle, Snapshot);
}

bool UMassGASSubsystem::UnregisterAbilitySystemTarget(const FMassExternalTargetHandle Handle)
{
	const int32 Index = FindEndpointIndex(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	RemoveEndpointAt(Index);
	return true;
}

UAbilitySystemComponent* UMassGASSubsystem::ResolveAbilitySystem(
	const FMassExternalTargetHandle Handle) const
{
	const int32 Index = FindEndpointIndex(Handle);
	return Index != INDEX_NONE ? Endpoints[Index].AbilitySystem.Get() : nullptr;
}

bool UMassGASSubsystem::CompileMappings()
{
	// 在世界初始化时编译一次。运行时 Processor 只使用整数和映射查找，
	// 热路径不会读取作者资产。
	ReleaseRoutes();
	InboundEffectIds.Reset();
	InboundMappingIndices.Reset();
	OutboundMappingIndices.Reset();
	MappingAsset = nullptr;

	const UMassGASSettings* Settings = GetDefault<UMassGASSettings>();
	MappingAsset = Settings ? Settings->EffectMapping.LoadSynchronous() : nullptr;
	if (!MappingAsset)
	{
		UE_LOG(LogMassbilityGAS, Warning, TEXT("No Mass GAS effect mapping is configured."));
		return false;
	}

	const TArray<FMassGASOutboundEffectMapping>& Outbound = MappingAsset->GetOutboundMappings();
	for (int32 Index = 0; Index < Outbound.Num(); ++Index)
	{
		const FMassGASOutboundEffectMapping& Entry = Outbound[Index];
		if (!Entry.SemanticTag.IsValid() || !Entry.GameplayEffectClass || !FMath::IsFinite(Entry.GameplayEffectLevel)
			|| Entry.GameplayEffectLevel < 0.0f || OutboundMappingIndices.Contains(Entry.SemanticTag))
		{
			UE_LOG(LogMassbilityGAS, Error, TEXT("Invalid or duplicate outbound GAS mapping at index %d."), Index);
			OutboundMappingIndices.Reset();
			return false;
		}
		OutboundMappingIndices.Add(Entry.SemanticTag, Index);
	}

	const TArray<FMassGASInboundEffectMapping>& Inbound = MappingAsset->GetInboundMappings();
	InboundEffectIds.SetNum(Inbound.Num());
	for (int32 Index = 0; Index < Inbound.Num(); ++Index)
	{
		const FMassGASInboundEffectMapping& Entry = Inbound[Index];
		UClass* EffectClass = Entry.GameplayEffectClass.Get();
		if (!EffectClass || !FMath::IsFinite(Entry.DefaultMagnitude) || InboundMappingIndices.Contains(EffectClass)
			|| (Entry.Policy == EMassGASInboundPolicy::Effect && !Entry.MassEffect))
		{
			UE_LOG(LogMassbilityGAS, Error, TEXT("Invalid or duplicate inbound GAS mapping at index %d."), Index);
			OutboundMappingIndices.Reset();
			InboundMappingIndices.Reset();
			InboundEffectIds.Reset();
			return false;
		}
		InboundMappingIndices.Add(EffectClass, Index);
		if (Entry.Policy == EMassGASInboundPolicy::Effect)
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

	for (const FMassGASOutboundEffectMapping& Entry : Outbound)
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

void UMassGASSubsystem::ReleaseRoutes()
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

bool UMassGASSubsystem::BuildSnapshot(const FEndpoint& Endpoint,
	FMassExternalTargetSnapshot& OutSnapshot) const
{
	UAbilitySystemComponent* AbilitySystem = Endpoint.AbilitySystem.Get();
	AActor* Avatar = AbilitySystem ? AbilitySystem->GetAvatarActor_Direct() : nullptr;
	if (!AbilitySystem || !Avatar || !MassSubsystem)
	{
		return false;
	}
	OutSnapshot = {};
	OutSnapshot.Transform = Avatar->GetActorTransform();
	OutSnapshot.TeamId = Endpoint.TeamId;
	OutSnapshot.bTargetable = Endpoint.bTargetable;
	FGameplayTagContainer OwnedTags;
	AbilitySystem->GetOwnedGameplayTags(OwnedTags);
	MassSubsystem->GetRuntimeTagCatalog().BuildMaskFiltered(OwnedTags, OutSnapshot.RuntimeTags);
	return true;
}

void UMassGASSubsystem::SyncEndpoints()
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
		FMassExternalTargetSnapshot Snapshot;
		if (BuildSnapshot(Endpoint, Snapshot)
			&& ExternalTargetSubsystem->UpdateExternalTarget(Endpoint.Handle, Snapshot))
		{
			++Stats.ProxyUpdates;
		}
	}
}

void UMassGASSubsystem::FlushOutboundRequests()
{
	if (!ExternalTargetSubsystem)
	{
		return;
	}
	OutboundScratch.Reset();
	ExternalTargetSubsystem->ConsumeExternalOutputRequests(OutboundScratch);
	for (const FMassExternalOutputRequest& Request : OutboundScratch)
	{
		++Stats.OutboundRequestsReceived;
		ApplyOutboundRequest(Request);
	}
	OutboundScratch.Reset();
}

bool UMassGASSubsystem::ApplyOutboundRequest(const FMassExternalOutputRequest& Request)
{
	// 这是 Mass 输出转换为原生 GameplayEffectSpec 的唯一 MassGAS 路径。
	// Mass 核心模块不会包含 GameplayAbilities 依赖。
	UAbilitySystemComponent* TargetAbilitySystem = ResolveAbilitySystem(Request.Target);
	if (!TargetAbilitySystem)
	{
		++Stats.OutboundRejectedInvalidTarget;
		return false;
	}
	const FMassCompiledOutput* Output = OutputSubsystem ? OutputSubsystem->FindOutput(Request.OutputId) : nullptr;
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
	const FMassGASOutboundEffectMapping& Mapping = MappingAsset->GetOutboundMappings()[*MappingIndex];
	AMassGASBridgeActor* SourceActor = GetOrCreateBridgeActor();
	UAbilitySystemComponent* SourceAbilitySystem = SourceActor ? SourceActor->GetAbilitySystemComponent() : nullptr;
	if (!SourceAbilitySystem)
	{
		++Stats.OutboundSpecFailures;
		return false;
	}

	FMassGASGameplayEffectContext* MassContext = new FMassGASGameplayEffectContext();
	MassContext->SourceMassEntity = Request.Source;
	MassContext->SemanticTag = Output->SemanticTag;
	const float FinalScale = Output->MagnitudeScale * Request.MagnitudeScale;
	MassContext->RawMagnitude = Output->Type == EMassAbilityOutputType::Damage
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

AMassGASBridgeActor* UMassGASSubsystem::GetOrCreateBridgeActor()
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
	Params.Name = MakeUniqueObjectName(World, AMassGASBridgeActor::StaticClass(), TEXT("MassGASBridge"));
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	BridgeActor = World->SpawnActor<AMassGASBridgeActor>(Params);
	if (BridgeActor && BridgeActor->GetAbilitySystemComponent())
	{
		BridgeActor->GetAbilitySystemComponent()->InitAbilityActorInfo(BridgeActor, BridgeActor);
	}
	return BridgeActor;
}

int32 UMassGASSubsystem::FindEndpointIndex(const FMassExternalTargetHandle Handle) const
{
	return Endpoints.IndexOfByPredicate([Handle](const FEndpoint& Endpoint) { return Endpoint.Handle == Handle; });
}

int32 UMassGASSubsystem::FindEndpointIndex(const UAbilitySystemComponent* AbilitySystem) const
{
	return Endpoints.IndexOfByPredicate([AbilitySystem](const FEndpoint& Endpoint)
	{
		return Endpoint.AbilitySystem.Get() == AbilitySystem;
	});
}

void UMassGASSubsystem::RemoveEndpointAt(const int32 Index)
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

bool UMassGASSubsystem::ApplyGameplayEffectSpecToMassTarget(const FGameplayEffectSpec& Spec,
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
	const FMassStateFragment* State =
		EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassStateFragment>(Target);
	if (!State || !EnumHasAnyFlags(State->StateFlags, EMassStateFlags::Targetable)
		|| EnumHasAnyFlags(State->StateFlags, EMassStateFlags::Dying | EMassStateFlags::Dead))
	{
		return false;
	}
	const int32* MappingIndex = InboundMappingIndices.Find(Spec.Def->GetClass());
	if (!MappingIndex || !MappingAsset->GetInboundMappings().IsValidIndex(*MappingIndex))
	{
		++Stats.InboundRejectedNoMapping;
		return false;
	}
	const FMassGASInboundEffectMapping& Mapping = MappingAsset->GetInboundMappings()[*MappingIndex];
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
	if (Mapping.Policy == EMassGASInboundPolicy::Damage)
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
