// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Effect/MassGASEffectMapping.h"
#include "Effect/MassEffectTypes.h"
#include "External/MassExternalTargetTypes.h"
#include "Output/MassOutputTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassGASSubsystem.generated.h"

class AMassGASBridgeActor;
class UAbilitySystemComponent;
class UMassEntitySubsystem;
class UMassCombatSubsystem;
class UMassEffectSubsystem;
class UMassExternalTargetSubsystem;
class UMassGASEffectMapping;
class UMassOutputSubsystem;
class UMassRuntimeSubsystem;
struct FGameplayEffectSpec;

struct MASSBILITYGAS_API FMassGASBridgeStats
{
	uint64 EndpointsRegistered = 0;
	uint64 EndpointsUnregistered = 0;
	uint64 ProxyUpdates = 0;
	uint64 OutboundRequestsReceived = 0;
	uint64 OutboundSpecsApplied = 0;
	uint64 OutboundRejectedInvalidTarget = 0;
	uint64 OutboundRejectedNoMapping = 0;
	uint64 OutboundSpecFailures = 0;
	uint64 InboundSpecsReceived = 0;
	uint64 InboundAppliedAsDamage = 0;
	uint64 InboundAppliedAsEffect = 0;
	uint64 InboundRejectedNoMapping = 0;
};

UCLASS()
class MASSBILITYGAS_API UMassGASSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassGASSubsystem* Get(const UObject* WorldContextObject);

	/** 将 ASC 注册为外部目标并创建对应的 Mass 代理；仅权威端可用。 */
	FMassExternalTargetHandle RegisterAbilitySystemTarget(UAbilitySystemComponent* AbilitySystem,
		int16 TeamId, bool bTargetable = true);
	/** 只更新队伍值；变换和拥有的 GameplayTag 由代理同步 Processor 更新。 */
	bool UpdateAbilitySystemTargetTeam(FMassExternalTargetHandle Handle, int16 TeamId);
	/** 控制 Mass 目标系统是否可以选择该 ASC 代理。 */
	bool SetAbilitySystemTargetable(FMassExternalTargetHandle Handle, bool bTargetable);
	bool UnregisterAbilitySystemTarget(FMassExternalTargetHandle Handle);
	UAbilitySystemComponent* ResolveAbilitySystem(FMassExternalTargetHandle Handle) const;

	/** 将原生 GAS Spec 转换为排队的 Mass 伤害或效果请求。 */
	bool ApplyGameplayEffectSpecToMassTarget(const FGameplayEffectSpec& Spec,
		FMassEntityHandle Target, const FHitResult* OptionalHitResult = nullptr);

	const FMassGASBridgeStats& GetStats() const { return Stats; }

private:
	friend class UMassGASProxySyncProcessor;
	friend class UMassGASBridgeProcessor;

	struct FEndpoint
	{
		FMassExternalTargetHandle Handle;
		FMassEntityHandle ProxyEntity;
		TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
		TWeakObjectPtr<AActor> AvatarActor;
		int16 TeamId = 0;
		bool bTargetable = true;
	};

	/** 在世界启动时加载并校验不可变的资产映射表。 */
	bool CompileMappings();
	void ReleaseRoutes();
	bool BuildSnapshot(const FEndpoint& Endpoint, FMassExternalTargetSnapshot& OutSnapshot) const;
	/** 在空间查询前，将 ASC Avatar 的当前状态复制到代理快照。 */
	void SyncEndpoints();
	/** 消费 Mass 外部输出请求，并应用映射后的 GameplayEffect Spec。 */
	void FlushOutboundRequests();
	bool ApplyOutboundRequest(const struct FMassExternalOutputRequest& Request);
	AMassGASBridgeActor* GetOrCreateBridgeActor();
	int32 FindEndpointIndex(FMassExternalTargetHandle Handle) const;
	int32 FindEndpointIndex(const UAbilitySystemComponent* AbilitySystem) const;
	void RemoveEndpointAt(int32 Index);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassRuntimeSubsystem> MassSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassEffectSubsystem> EffectSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassOutputSubsystem> OutputSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassCombatSubsystem> CombatSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassExternalTargetSubsystem> ExternalTargetSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassGASEffectMapping> MappingAsset;

	UPROPERTY(Transient)
	TObjectPtr<AMassGASBridgeActor> BridgeActor;

	TArray<FEndpoint> Endpoints;
	TMap<FGameplayTag, int32> OutboundMappingIndices;
	TMap<UClass*, int32> InboundMappingIndices;
	TArray<struct FMassEffectId> InboundEffectIds;
	TArray<FGameplayTag> RegisteredRouteTags;
	TArray<struct FMassExternalOutputRequest> OutboundScratch;
	FMassGASBridgeStats Stats;
};
