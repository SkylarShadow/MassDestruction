// Copyright MassDestruction. All Rights Reserved.

#include "External/MassbilityExternalTargetSubsystem.h"

#include "Core/MassbilitySubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassbilityFragments.h"
#include "External/MassbilityExternalTargetFragments.h"
#include "Mass/EntityFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"

void UMassbilityExternalTargetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassbilitySubsystem = Collection.InitializeDependency<UMassbilitySubsystem>();
	TargetRecords.Reserve(16);
	FreeRecordIndices.Reserve(16);
	PendingOutputRequests.Reserve(64);
}

void UMassbilityExternalTargetSubsystem::Deinitialize()
{
	if (EntitySubsystem)
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		for (const FTargetRecord& Record : TargetRecords)
		{
			if (Record.bAllocated && EntityManager.IsEntityValid(Record.ProxyEntity))
			{
				EntityManager.DestroyEntity(Record.ProxyEntity);
			}
		}
	}
	PendingOutputRequests.Reset();
	OutputRouteRefCounts.Reset();
	FreeRecordIndices.Reset();
	TargetRecords.Reset();
	ProxyArchetype = {};
	Stats = {};
	MassbilitySubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassbilityExternalTargetSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassbilityExternalTargetSubsystem* UMassbilityExternalTargetSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassbilityExternalTargetSubsystem>();
	}
	return nullptr;
}

bool UMassbilityExternalTargetSubsystem::RegisterExternalTarget(const FMassbilityExternalTargetSnapshot& Snapshot,
	FMassbilityExternalTargetHandle& OutHandle, FMassEntityHandle& OutProxyEntity)
{
	// 代理只拥有快照和带代数校验的句柄；真实 Actor/ASC 的生命周期仍由外部运行时负责。
	OutHandle.Reset();
	OutProxyEntity = {};
	if (!EntitySubsystem || !MassbilitySubsystem)
	{
		return false;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassArchetypeHandle Archetype = GetOrCreateProxyArchetype();
	if (!Archetype.IsValid())
	{
		return false;
	}

	uint32 RecordIndex = MAX_uint32;
	if (!FreeRecordIndices.IsEmpty())
	{
		RecordIndex = FreeRecordIndices.Pop(EAllowShrinking::No);
	}
	else
	{
		RecordIndex = static_cast<uint32>(TargetRecords.AddDefaulted());
	}
	FTargetRecord& Record = TargetRecords[RecordIndex];
	if (Record.Serial == 0)
	{
		Record.Serial = 1;
	}
	OutHandle = {RecordIndex, Record.Serial};

	const FMassEntityHandle ProxyEntity = EntityManager.CreateEntity(Archetype);
	if (!EntityManager.IsEntityValid(ProxyEntity))
	{
		FreeRecordIndices.Add(RecordIndex);
		OutHandle.Reset();
		return false;
	}

	Record.ProxyEntity = ProxyEntity;
	Record.bAllocated = true;
	OutProxyEntity = ProxyEntity;
	{
		FMassEntityView EntityView(EntityManager, ProxyEntity);
		FMassbilityExternalTargetFragment& External = EntityView.GetFragmentData<FMassbilityExternalTargetFragment>();
		External.Handle = OutHandle;
	}
	EntityManager.AddTagToEntity(ProxyEntity, FMassbilityExternalTargetTag::StaticStruct());

	if (!UpdateExternalTarget(OutHandle, Snapshot))
	{
		UnregisterExternalTarget(OutHandle);
		OutHandle.Reset();
		OutProxyEntity = {};
		return false;
	}
	++Stats.TargetsRegistered;
	return true;
}

bool UMassbilityExternalTargetSubsystem::UpdateExternalTarget(const FMassbilityExternalTargetHandle Handle,
	const FMassbilityExternalTargetSnapshot& Snapshot)
{
	if (!EntitySubsystem || !MassbilitySubsystem || !IsHandleRecordValid(Handle))
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle ProxyEntity = TargetRecords[Handle.Index].ProxyEntity;
	if (!EntityManager.IsEntityValid(ProxyEntity))
	{
		return false;
	}

	FMassbilityUnitFragment* Unit = EntityManager.GetFragmentDataPtr<FMassbilityUnitFragment>(ProxyEntity);
	FMassbilityStateFragment* State = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(ProxyEntity);
	FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(ProxyEntity);
	FMassbilityExternalTargetFragment* External = EntityManager.GetFragmentDataPtr<FMassbilityExternalTargetFragment>(ProxyEntity);
	if (!Unit || !State || !Transform || !External || !(External->Handle == Handle))
	{
		return false;
	}

	Unit->UnitDefinitionId = Massbility::InvalidId;
	Unit->AttributeSchemaId = Massbility::InvalidId;
	Unit->TeamId = Snapshot.TeamId;
	State->StateFlags = Snapshot.bTargetable ? EMassbilityStateFlags::Targetable : EMassbilityStateFlags::None;
	State->BaseTags = Snapshot.RuntimeTags;
	State->EffectTags.Reset();
	Transform->SetTransform(Snapshot.Transform);
	MassbilitySubsystem->RebuildRuntimeTags(ProxyEntity);
	++Stats.SnapshotUpdates;
	return true;
}

bool UMassbilityExternalTargetSubsystem::UnregisterExternalTarget(const FMassbilityExternalTargetHandle Handle)
{
	if (!EntitySubsystem || !IsHandleRecordValid(Handle))
	{
		return false;
	}
	FTargetRecord& Record = TargetRecords[Handle.Index];
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (EntityManager.IsEntityValid(Record.ProxyEntity))
	{
		if (MassbilitySubsystem)
		{
			MassbilitySubsystem->SetStateFlags(Record.ProxyEntity, EMassbilityStateFlags::None);
		}
		EntityManager.DestroyEntity(Record.ProxyEntity);
	}
	Record.ProxyEntity = {};
	Record.bAllocated = false;
	++Record.Serial;
	if (Record.Serial == 0)
	{
		Record.Serial = 1;
	}
	FreeRecordIndices.Add(Handle.Index);
	++Stats.TargetsUnregistered;
	return true;
}

bool UMassbilityExternalTargetSubsystem::IsExternalTargetValid(const FMassbilityExternalTargetHandle Handle) const
{
	return IsHandleRecordValid(Handle) && EntitySubsystem
		&& EntitySubsystem->GetEntityManager().IsEntityValid(TargetRecords[Handle.Index].ProxyEntity);
}

bool UMassbilityExternalTargetSubsystem::ResolveProxyEntity(const FMassbilityExternalTargetHandle Handle,
	FMassEntityHandle& OutProxyEntity) const
{
	OutProxyEntity = {};
	if (!IsExternalTargetValid(Handle))
	{
		return false;
	}
	OutProxyEntity = TargetRecords[Handle.Index].ProxyEntity;
	return true;
}

bool UMassbilityExternalTargetSubsystem::ResolveExternalTarget(const FMassEntityHandle ProxyEntity,
	FMassbilityExternalTargetHandle& OutHandle) const
{
	OutHandle.Reset();
	if (!EntitySubsystem || !EntitySubsystem->GetEntityManager().IsEntityValid(ProxyEntity))
	{
		return false;
	}
	const FMassbilityExternalTargetFragment* External =
		EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassbilityExternalTargetFragment>(ProxyEntity);
	if (!External || !IsExternalTargetValid(External->Handle))
	{
		return false;
	}
	OutHandle = External->Handle;
	return true;
}

bool UMassbilityExternalTargetSubsystem::RegisterExternalOutputRoute(const FGameplayTag SemanticTag)
{
	if (!SemanticTag.IsValid())
	{
		return false;
	}
	uint16& RefCount = OutputRouteRefCounts.FindOrAdd(SemanticTag);
	if (RefCount == MAX_uint16)
	{
		return false;
	}
	++RefCount;
	return true;
}

bool UMassbilityExternalTargetSubsystem::UnregisterExternalOutputRoute(const FGameplayTag SemanticTag)
{
	uint16* RefCount = OutputRouteRefCounts.Find(SemanticTag);
	if (!RefCount)
	{
		return false;
	}
	if (*RefCount <= 1)
	{
		OutputRouteRefCounts.Remove(SemanticTag);
	}
	else
	{
		--*RefCount;
	}
	return true;
}

bool UMassbilityExternalTargetSubsystem::CanRouteExternalOutput(const FGameplayTag SemanticTag) const
{
	const uint16* RefCount = OutputRouteRefCounts.Find(SemanticTag);
	return SemanticTag.IsValid() && RefCount && *RefCount > 0;
}

bool UMassbilityExternalTargetSubsystem::QueueExternalOutput(const FMassbilityExternalOutputRequest& Request,
	const FGameplayTag SemanticTag)
{
	// 入队时完成路由校验，避免无效目标或未映射的语义输出进入跨运行时队列。
	if (!IsExternalTargetValid(Request.Target))
	{
		++Stats.OutputRequestsRejectedInvalidTarget;
		return false;
	}
	if (!CanRouteExternalOutput(SemanticTag))
	{
		++Stats.OutputRequestsRejectedNoRoute;
		return false;
	}
	if (!Request.OutputId.IsValid()
		|| !FMath::IsFinite(Request.MagnitudeScale) || Request.MagnitudeScale <= 0.0f)
	{
		return false;
	}
	PendingOutputRequests.Add(Request);
	++Stats.OutputRequestsQueued;
	return true;
}

void UMassbilityExternalTargetSubsystem::ConsumeExternalOutputRequests(
	TArray<FMassbilityExternalOutputRequest>& OutRequests)
{
	OutRequests.Append(PendingOutputRequests);
	PendingOutputRequests.Reset();
}

FMassArchetypeHandle UMassbilityExternalTargetSubsystem::GetOrCreateProxyArchetype()
{
	if (!EntitySubsystem)
	{
		return {};
	}
	if (!ProxyArchetype.IsValid())
	{
		const TArray<const UScriptStruct*> Fragments = {
			FMassbilityUnitFragment::StaticStruct(),
			FMassbilityStateFragment::StaticStruct(),
			FMassbilityExternalTargetFragment::StaticStruct(),
			FTransformFragment::StaticStruct(),
		};
		ProxyArchetype = EntitySubsystem->GetMutableEntityManager().CreateArchetype(Fragments);
	}
	return ProxyArchetype;
}

bool UMassbilityExternalTargetSubsystem::IsHandleRecordValid(const FMassbilityExternalTargetHandle Handle) const
{
	return Handle.IsSet() && TargetRecords.IsValidIndex(static_cast<int32>(Handle.Index))
		&& TargetRecords[Handle.Index].bAllocated && TargetRecords[Handle.Index].Serial == Handle.Serial;
}
