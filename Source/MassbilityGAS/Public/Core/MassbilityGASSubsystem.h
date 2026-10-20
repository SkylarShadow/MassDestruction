// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Effect/MassbilityGASEffectMapping.h"
#include "Effect/MassbilityEffectTypes.h"
#include "External/MassbilityExternalTargetTypes.h"
#include "Output/MassbilityOutputTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassbilityGASSubsystem.generated.h"

class AMassbilityGASBridgeActor;
class UAbilitySystemComponent;
class UMassEntitySubsystem;
class UMassbilityCombatSubsystem;
class UMassbilityEffectSubsystem;
class UMassbilityExternalTargetSubsystem;
class UMassbilityGASEffectMapping;
class UMassbilityOutputSubsystem;
class UMassbilitySubsystem;
struct FGameplayEffectSpec;

struct MASSBILITYGAS_API FMassbilityGASBridgeStats
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
class MASSBILITYGAS_API UMassbilityGASSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassbilityGASSubsystem* Get(const UObject* WorldContextObject);

	/** 将 ASC 注册为外部目标并创建对应的 Mass 代理；仅权威端可用。 */
	FMassbilityExternalTargetHandle RegisterAbilitySystemTarget(UAbilitySystemComponent* AbilitySystem,
		int16 TeamId, bool bTargetable = true);
	/** 只更新队伍值；变换和拥有的 GameplayTag 由代理同步 Processor 更新。 */
	bool UpdateAbilitySystemTargetTeam(FMassbilityExternalTargetHandle Handle, int16 TeamId);
	/** 控制 Mass 目标系统是否可以选择该 ASC 代理。 */
	bool SetAbilitySystemTargetable(FMassbilityExternalTargetHandle Handle, bool bTargetable);
	bool UnregisterAbilitySystemTarget(FMassbilityExternalTargetHandle Handle);
	UAbilitySystemComponent* ResolveAbilitySystem(FMassbilityExternalTargetHandle Handle) const;

	/** 将原生 GAS Spec 转换为排队的 Mass 伤害或效果请求。 */
	bool ApplyGameplayEffectSpecToMassTarget(const FGameplayEffectSpec& Spec,
		FMassEntityHandle Target, const FHitResult* OptionalHitResult = nullptr);

	const FMassbilityGASBridgeStats& GetStats() const { return Stats; }

private:
	friend class UMassbilityGASProxySyncProcessor;
	friend class UMassbilityGASBridgeProcessor;

	struct FEndpoint
	{
		FMassbilityExternalTargetHandle Handle;
		FMassEntityHandle ProxyEntity;
		TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
		TWeakObjectPtr<AActor> AvatarActor;
		int16 TeamId = 0;
		bool bTargetable = true;
	};

	/** 在世界启动时加载并校验不可变的资产映射表。 */
	bool CompileMappings();
	void ReleaseRoutes();
	bool BuildSnapshot(const FEndpoint& Endpoint, FMassbilityExternalTargetSnapshot& OutSnapshot) const;
	/** 在空间查询前，将 ASC Avatar 的当前状态复制到代理快照。 */
	void SyncEndpoints();
	/** 消费 Mass 外部输出请求，并应用映射后的 GameplayEffect Spec。 */
	void FlushOutboundRequests();
	bool ApplyOutboundRequest(const struct FMassbilityExternalOutputRequest& Request);
	AMassbilityGASBridgeActor* GetOrCreateBridgeActor();
	int32 FindEndpointIndex(FMassbilityExternalTargetHandle Handle) const;
	int32 FindEndpointIndex(const UAbilitySystemComponent* AbilitySystem) const;
	void RemoveEndpointAt(int32 Index);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilitySubsystem> MassbilitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityEffectSubsystem> EffectSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityOutputSubsystem> OutputSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityCombatSubsystem> CombatSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityExternalTargetSubsystem> ExternalTargetSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityGASEffectMapping> MappingAsset;

	UPROPERTY(Transient)
	TObjectPtr<AMassbilityGASBridgeActor> BridgeActor;

	TArray<FEndpoint> Endpoints;
	TMap<FGameplayTag, int32> OutboundMappingIndices;
	TMap<UClass*, int32> InboundMappingIndices;
	TArray<struct FMassbilityEffectId> InboundEffectIds;
	TArray<FGameplayTag> RegisteredRouteTags;
	TArray<struct FMassbilityExternalOutputRequest> OutboundScratch;
	FMassbilityGASBridgeStats Stats;
};
