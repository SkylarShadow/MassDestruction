// Copyright MassDestruction. All Rights Reserved.


#include "External/MassExternalTargetSubsystem.h"

#include "Core/MassRuntimeSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassFragments.h"
#include "External/MassExternalTargetFragments.h"
#include "Mass/EntityFragments.h"
#include "Targeting/MassUnitHitShapeFragment.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"

// 初始化实体和运行时依赖，并预留目标及外部输出队列容量。
void UMassExternalTargetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassSubsystem = Collection.InitializeDependency<UMassRuntimeSubsystem>();
	TargetRecords.Reserve(16);
	FreeRecordIndices.Reserve(16);
	PendingOutputRequests.Reserve(64);
}

// 清理目标记录和输出队列；代理实体由 EntitySubsystem 生命周期统一回收。
void UMassExternalTargetSubsystem::Deinitialize()
{
	// UMassEntitySubsystem may already have released its FMassEntityManager when
	// world subsystems are torn down. Calling GetMutableEntityManager() here would
	// hit MassEntitySubsystem's check(EntityManager). Proxy entities are owned by
	// that manager and are cleaned up together with it, so this phase only clears
	// our bookkeeping. Normal endpoint destruction still goes through
	// UnregisterExternalTarget while the world is alive.
	PendingOutputRequests.Reset();
	OutputRouteRefCounts.Reset();
	FreeRecordIndices.Reset();
	TargetRecords.Reset();
	ProxyArchetype = {};
	Stats = {};
	MassSubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

// 判断当前 World 类型是否允许创建外部目标代理。
bool UMassExternalTargetSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

// 从 WorldContextObject 获取所属 World 的外部目标子系统。
UMassExternalTargetSubsystem* UMassExternalTargetSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassExternalTargetSubsystem>();
	}
	return nullptr;
}

bool UMassExternalTargetSubsystem::RegisterExternalTarget(const FMassExternalTargetSnapshot& Snapshot,
	FMassExternalTargetHandle& OutHandle, FMassEntityHandle& OutProxyEntity)
{
	// 创建只保存快照的 Proxy Entity；真实外部对象生命周期由调用方负责。
	// 代理只拥有快照和带代数校验的句柄；真实 Actor/ASC 的生命周期仍由外部运行时负责。
	OutHandle.Reset();
	OutProxyEntity = {};
	if (!EntitySubsystem || !MassSubsystem)
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
		FMassExternalTargetFragment& External = EntityView.GetFragmentData<FMassExternalTargetFragment>();
		External.Handle = OutHandle;
	}
	EntityManager.AddTagToEntity(ProxyEntity, FMassExternalTargetTag::StaticStruct());

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

bool UMassExternalTargetSubsystem::UpdateExternalTarget(const FMassExternalTargetHandle Handle,
	const FMassExternalTargetSnapshot& Snapshot)
{
	// 将外部对象最新的队伍、位置、可选中状态和 Tag 同步到代理实体。
	if (!EntitySubsystem || !MassSubsystem || !IsHandleRecordValid(Handle))
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle ProxyEntity = TargetRecords[Handle.Index].ProxyEntity;
	if (!EntityManager.IsEntityValid(ProxyEntity))
	{
		return false;
	}

	FMassUnitFragment* Unit = EntityManager.GetFragmentDataPtr<FMassUnitFragment>(ProxyEntity);
	FMassStateFragment* State = EntityManager.GetFragmentDataPtr<FMassStateFragment>(ProxyEntity);
	FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(ProxyEntity);
	FMassExternalTargetFragment* External = EntityManager.GetFragmentDataPtr<FMassExternalTargetFragment>(ProxyEntity);
	if (!Unit || !State || !Transform || !External || !(External->Handle == Handle))
	{
		return false;
	}

	Unit->UnitDefinitionId = Massbility::InvalidId;
	Unit->AttributeLayoutId = Massbility::InvalidId;
	Unit->TeamId = Snapshot.TeamId;
	State->StateFlags = Snapshot.bTargetable ? EMassStateFlags::Targetable : EMassStateFlags::None;
	State->BaseTags = Snapshot.RuntimeTags;
	State->EffectTags.Reset();
	Transform->SetTransform(Snapshot.Transform);
	if (FMassUnitHitShapeFragment* HitShape = EntityManager.GetFragmentDataPtr<FMassUnitHitShapeFragment>(ProxyEntity))
	{
		HitShape->Radius = FMath::Max(0.0f, Snapshot.HitRadius);
		HitShape->HalfHeight = FMath::Max(HitShape->Radius, Snapshot.HitHalfHeight);
		HitShape->bEnabled = Snapshot.HitRadius > 0.0f;
	}
	MassSubsystem->RebuildRuntimeTags(ProxyEntity);
	++Stats.SnapshotUpdates;
	return true;
}

bool UMassExternalTargetSubsystem::UnregisterExternalTarget(const FMassExternalTargetHandle Handle)
{
	// 销毁代理实体并递增 Serial，使所有旧句柄立即失效。
	if (!EntitySubsystem || !IsHandleRecordValid(Handle))
	{
		return false;
	}
	FTargetRecord& Record = TargetRecords[Handle.Index];
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (EntityManager.IsEntityValid(Record.ProxyEntity))
	{
		if (MassSubsystem)
		{
			MassSubsystem->SetStateFlags(Record.ProxyEntity, EMassStateFlags::None);
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

// 校验句柄代数和代理实体，确认外部目标仍可参与战斗。
bool UMassExternalTargetSubsystem::IsExternalTargetValid(const FMassExternalTargetHandle Handle) const
{
	return IsHandleRecordValid(Handle) && EntitySubsystem
		&& EntitySubsystem->GetEntityManager().IsEntityValid(TargetRecords[Handle.Index].ProxyEntity);
}

// 将外部目标句柄解析为对应的 Mass 代理实体。
bool UMassExternalTargetSubsystem::ResolveProxyEntity(const FMassExternalTargetHandle Handle,
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

// 从 Mass 代理实体反向解析外部目标句柄。
bool UMassExternalTargetSubsystem::ResolveExternalTarget(const FMassEntityHandle ProxyEntity,
	FMassExternalTargetHandle& OutHandle) const
{
	OutHandle.Reset();
	if (!EntitySubsystem || !EntitySubsystem->GetEntityManager().IsEntityValid(ProxyEntity))
	{
		return false;
	}
	const FMassExternalTargetFragment* External =
		EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassExternalTargetFragment>(ProxyEntity);
	if (!External || !IsExternalTargetValid(External->Handle))
	{
		return false;
	}
	OutHandle = External->Handle;
	return true;
}

// 为一个语义 Tag 注册外部输出路由，并增加引用计数。
bool UMassExternalTargetSubsystem::RegisterExternalOutputRoute(const FGameplayTag SemanticTag)
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

// 取消一个外部输出路由；引用归零后移除路由。
bool UMassExternalTargetSubsystem::UnregisterExternalOutputRoute(const FGameplayTag SemanticTag)
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

// 判断当前是否存在可处理指定语义输出的外部路由。
bool UMassExternalTargetSubsystem::CanRouteExternalOutput(const FGameplayTag SemanticTag) const
{
	const uint16* RefCount = OutputRouteRefCounts.Find(SemanticTag);
	return SemanticTag.IsValid() && RefCount && *RefCount > 0;
}

bool UMassExternalTargetSubsystem::QueueExternalOutput(const FMassExternalOutputRequest& Request,
	const FGameplayTag SemanticTag)
{
	// 只有目标有效且存在语义路由时，跨运行时输出才允许入队。
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

// 将等待中的外部输出转移给桥接层，并清空内部队列。
void UMassExternalTargetSubsystem::ConsumeExternalOutputRequests(
	TArray<FMassExternalOutputRequest>& OutRequests)
{
	OutRequests.Append(PendingOutputRequests);
	PendingOutputRequests.Reset();
}

FMassArchetypeHandle UMassExternalTargetSubsystem::GetOrCreateProxyArchetype()
{
	// 延迟创建并复用外部目标代理所需的固定 Fragment 组合。
	if (!EntitySubsystem)
	{
		return {};
	}
	if (!ProxyArchetype.IsValid())
	{
		const TArray<const UScriptStruct*> Fragments = {
			FMassUnitFragment::StaticStruct(),
			FMassStateFragment::StaticStruct(),
			FMassExternalTargetFragment::StaticStruct(),
			FMassUnitHitShapeFragment::StaticStruct(),
			FTransformFragment::StaticStruct(),
		};
		ProxyArchetype = EntitySubsystem->GetMutableEntityManager().CreateArchetype(Fragments);
	}
	return ProxyArchetype;
}

// 校验句柄索引、分配状态和 Serial，防止旧句柄访问复用记录。
bool UMassExternalTargetSubsystem::IsHandleRecordValid(const FMassExternalTargetHandle Handle) const
{
	return Handle.IsSet() && TargetRecords.IsValidIndex(static_cast<int32>(Handle.Index))
		&& TargetRecords[Handle.Index].bAllocated && TargetRecords[Handle.Index].Serial == Handle.Serial;
}
