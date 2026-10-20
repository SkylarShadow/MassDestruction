// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "External/MassbilityExternalTargetTypes.h"
#include "MassArchetypeTypes.h"
#include "Output/MassbilityOutputTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassbilityExternalTargetSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassbilitySubsystem;

/**
 * 其他战斗运行时目标在 Mass 核心侧的边界。
 * 本子系统不会保存 Actor、ASC 或其他外部 UObject 引用。
 */
UCLASS()
class MASSBILITY_API UMassbilityExternalTargetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassbilityExternalTargetSubsystem* Get(const UObject* WorldContextObject);

	/** 创建代理实体，同时返回带代数校验的目标句柄和 Mass 实体句柄。 */
	bool RegisterExternalTarget(const FMassbilityExternalTargetSnapshot& Snapshot,
		FMassbilityExternalTargetHandle& OutHandle, FMassEntityHandle& OutProxyEntity);
	/** 将持有者的最新快照复制到代理实体，不保留外部 UObject 引用。 */
	bool UpdateExternalTarget(FMassbilityExternalTargetHandle Handle,
		const FMassbilityExternalTargetSnapshot& Snapshot);
	/** 销毁代理实体，并使当前句柄代数失效。 */
	bool UnregisterExternalTarget(FMassbilityExternalTargetHandle Handle);

	bool IsExternalTargetValid(FMassbilityExternalTargetHandle Handle) const;
	bool ResolveProxyEntity(FMassbilityExternalTargetHandle Handle, FMassEntityHandle& OutProxyEntity) const;
	bool ResolveExternalTarget(FMassEntityHandle ProxyEntity, FMassbilityExternalTargetHandle& OutHandle) const;

	bool RegisterExternalOutputRoute(FGameplayTag SemanticTag);
	bool UnregisterExternalOutputRoute(FGameplayTag SemanticTag);
	bool CanRouteExternalOutput(FGameplayTag SemanticTag) const;
	bool QueueExternalOutput(const FMassbilityExternalOutputRequest& Request, FGameplayTag SemanticTag);
	void ConsumeExternalOutputRequests(TArray<FMassbilityExternalOutputRequest>& OutRequests);

	int32 GetPendingOutputCount() const { return PendingOutputRequests.Num(); }
	const FMassbilityExternalTargetStats& GetStats() const { return Stats; }

private:
	struct FTargetRecord
	{
		FMassEntityHandle ProxyEntity;
		uint32 Serial = 1;
		bool bAllocated = false;
	};

	FMassArchetypeHandle GetOrCreateProxyArchetype();
	bool IsHandleRecordValid(FMassbilityExternalTargetHandle Handle) const;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilitySubsystem> MassbilitySubsystem;

	TArray<FTargetRecord> TargetRecords;
	TArray<uint32> FreeRecordIndices;
	TMap<FGameplayTag, uint16> OutputRouteRefCounts;
	TArray<FMassbilityExternalOutputRequest> PendingOutputRequests;
	FMassArchetypeHandle ProxyArchetype;
	FMassbilityExternalTargetStats Stats;
};
