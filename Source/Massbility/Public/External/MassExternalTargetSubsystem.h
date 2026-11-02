// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "External/MassExternalTargetTypes.h"
#include "MassArchetypeTypes.h"
#include "Output/MassOutputTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassExternalTargetSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassRuntimeSubsystem;

/**
 * 其他战斗运行时目标在 Mass 核心侧的边界。
 * 本子系统不会保存 Actor、ASC 或其他外部 UObject 引用。
 */
UCLASS()
class MASSBILITY_API UMassExternalTargetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassExternalTargetSubsystem* Get(const UObject* WorldContextObject);

	/** 创建代理实体，同时返回带代数校验的目标句柄和 Mass 实体句柄。 */
	bool RegisterExternalTarget(const FMassExternalTargetSnapshot& Snapshot,
		FMassExternalTargetHandle& OutHandle, FMassEntityHandle& OutProxyEntity);
	/** 将持有者的最新快照复制到代理实体，不保留外部 UObject 引用。 */
	bool UpdateExternalTarget(FMassExternalTargetHandle Handle,
		const FMassExternalTargetSnapshot& Snapshot);
	/** 销毁代理实体，并使当前句柄代数失效。 */
	bool UnregisterExternalTarget(FMassExternalTargetHandle Handle);

	bool IsExternalTargetValid(FMassExternalTargetHandle Handle) const;
	bool ResolveProxyEntity(FMassExternalTargetHandle Handle, FMassEntityHandle& OutProxyEntity) const;
	bool ResolveExternalTarget(FMassEntityHandle ProxyEntity, FMassExternalTargetHandle& OutHandle) const;

	bool RegisterExternalOutputRoute(FGameplayTag SemanticTag);
	bool UnregisterExternalOutputRoute(FGameplayTag SemanticTag);
	bool CanRouteExternalOutput(FGameplayTag SemanticTag) const;
	bool QueueExternalOutput(const FMassExternalOutputRequest& Request, FGameplayTag SemanticTag);
	void ConsumeExternalOutputRequests(TArray<FMassExternalOutputRequest>& OutRequests);

	int32 GetPendingOutputCount() const { return PendingOutputRequests.Num(); }
	const FMassExternalTargetStats& GetStats() const { return Stats; }

private:
	struct FTargetRecord
	{
		FMassEntityHandle ProxyEntity;
		uint32 Serial = 1;
		bool bAllocated = false;
	};

	FMassArchetypeHandle GetOrCreateProxyArchetype();
	bool IsHandleRecordValid(FMassExternalTargetHandle Handle) const;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassRuntimeSubsystem> MassSubsystem;

	TArray<FTargetRecord> TargetRecords;
	TArray<uint32> FreeRecordIndices;
	TMap<FGameplayTag, uint16> OutputRouteRefCounts;
	TArray<FMassExternalOutputRequest> PendingOutputRequests;
	FMassArchetypeHandle ProxyArchetype;
	FMassExternalTargetStats Stats;
};
