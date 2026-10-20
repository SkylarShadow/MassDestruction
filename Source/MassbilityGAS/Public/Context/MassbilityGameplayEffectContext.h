// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Mass/EntityHandle.h"

#include "MassbilityGameplayEffectContext.generated.h"

/** 逻辑来源为 Mass 实体的 GameplayEffect 使用的 GAS 上下文。 */
USTRUCT()
struct MASSBILITYGAS_API FMassbilityGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

	/** 仅权威端有效的来源身份；有意不参与网络序列化。 */
	FMassEntityHandle SourceMassEntity;
	/** GAS 映射资产使用的稳定语义输出键。 */
	FGameplayTag SemanticTag;
	/** 原生 GameplayEffect 应用 Modifier 前的最终原始强度。 */
	float RawMagnitude = 0.0f;
	/** 输出产生时记录的世界空间起点和方向。 */
	FVector_NetQuantize10 Origin = FVector::ZeroVector;
	FVector_NetQuantizeNormal Direction = FVector::ForwardVector;
	uint32 SimulationTick = 0;

	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FMassbilityGameplayEffectContext* Duplicate() const override;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FMassbilityGameplayEffectContext>
	: public TStructOpsTypeTraitsBase2<FMassbilityGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true,
	};
};

