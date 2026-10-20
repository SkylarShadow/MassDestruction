// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Mass/EntityHandle.h"

#include "MassbilityGameplayAbilityTargetData.generated.h"

/** 仅权威端使用的目标数据，用于将现有 GameplayEffect Spec 应用到 Mass 实体。 */
USTRUCT()
struct MASSBILITYGAS_API FGameplayAbilityTargetData_MassEntity : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	/** 仅权威端有效的 Mass 句柄；Phase 4 有意拒绝其网络序列化。 */
	FMassEntityHandle TargetEntity;
	FHitResult HitResult;
	bool bHasHitResult = false;

	virtual UScriptStruct* GetScriptStruct() const override;
	virtual bool HasHitResult() const override { return bHasHitResult; }
	virtual const FHitResult* GetHitResult() const override { return bHasHitResult ? &HitResult : nullptr; }
	virtual bool HasOrigin() const override { return bHasHitResult; }
	virtual FTransform GetOrigin() const override;
	virtual bool HasEndPoint() const override { return bHasHitResult; }
	virtual FVector GetEndPoint() const override;
	virtual void AddTargetDataToContext(FGameplayEffectContextHandle& Context, bool bIncludeActorArray) const override;
	/** 通过桥接层应用 Spec；Mass 目标预期返回空的 ActiveEffectHandle 数组。 */
	virtual TArray<FActiveGameplayEffectHandle> ApplyGameplayEffectSpec(
		FGameplayEffectSpec& Spec, FPredictionKey PredictionKey = FPredictionKey()) override;

	/** Phase 4 不允许原始 Mass 句柄跨网络传输。 */
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGameplayAbilityTargetData_MassEntity>
	: public TStructOpsTypeTraitsBase2<FGameplayAbilityTargetData_MassEntity>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true,
	};
};

namespace MassbilityGAS
{
	MASSBILITYGAS_API FGameplayAbilityTargetDataHandle MakeMassEntityTargetData(
		FMassEntityHandle TargetEntity, const FHitResult* OptionalHitResult = nullptr);
}

