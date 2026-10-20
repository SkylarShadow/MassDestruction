// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "MassbilityGASEffectMapping.generated.h"

class UGameplayEffect;
class UMassbilityEffectDefinition;

UENUM(BlueprintType)
enum class EMassbilityGASInboundPolicy : uint8
{
	/** 将传入的 GameplayEffect 转换为 Mass 伤害请求。 */
	Damage,
	/** 将传入的 GameplayEffect 转换为已注册的 Mass 效果。 */
	Effect,
};

USTRUCT(BlueprintType)
struct MASSBILITYGAS_API FMassbilityGASOutboundEffectMapping
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	FGameplayTag SemanticTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	TSubclassOf<UGameplayEffect> GameplayEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	FGameplayTag MagnitudeSetByCallerTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS", meta = (ClampMin = "0.0"))
	float GameplayEffectLevel = 1.0f;
};

USTRUCT(BlueprintType)
struct MASSBILITYGAS_API FMassbilityGASInboundEffectMapping
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	TSubclassOf<UGameplayEffect> GameplayEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	EMassbilityGASInboundPolicy Policy = EMassbilityGASInboundPolicy::Damage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	TObjectPtr<UMassbilityEffectDefinition> MassEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	FGameplayTag MagnitudeSetByCallerTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	float DefaultMagnitude = 1.0f;
};

UCLASS(BlueprintType)
class MASSBILITYGAS_API UMassbilityGASEffectMapping : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	const TArray<FMassbilityGASOutboundEffectMapping>& GetOutboundMappings() const { return OutboundMappings; }
	const TArray<FMassbilityGASInboundEffectMapping>& GetInboundMappings() const { return InboundMappings; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS", meta = (AllowPrivateAccess = "true"))
	TArray<FMassbilityGASOutboundEffectMapping> OutboundMappings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS", meta = (AllowPrivateAccess = "true"))
	TArray<FMassbilityGASInboundEffectMapping> InboundMappings;
};

