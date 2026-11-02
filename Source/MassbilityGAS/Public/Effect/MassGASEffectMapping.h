// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "MassGASEffectMapping.generated.h"

class UGameplayEffect;
class UMassEffectDefinition;

UENUM(BlueprintType)
enum class EMassGASInboundPolicy : uint8
{
	//将传入GE转换为Mass伤害请求。 
	Damage,
	// 将传入GE转换为已注册的Mass效果。 
	Effect,
};

USTRUCT(BlueprintType)
struct MASSBILITYGAS_API FMassGASOutboundEffectMapping
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
struct MASSBILITYGAS_API FMassGASInboundEffectMapping
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	TSubclassOf<UGameplayEffect> GameplayEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	EMassGASInboundPolicy Policy = EMassGASInboundPolicy::Damage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	TObjectPtr<UMassEffectDefinition> MassEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	FGameplayTag MagnitudeSetByCallerTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS")
	float DefaultMagnitude = 1.0f;
};

UCLASS(BlueprintType)
class MASSBILITYGAS_API UMassGASEffectMapping : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	const TArray<FMassGASOutboundEffectMapping>& GetOutboundMappings() const { return OutboundMappings; }
	const TArray<FMassGASInboundEffectMapping>& GetInboundMappings() const { return InboundMappings; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS", meta = (AllowPrivateAccess = "true"))
	TArray<FMassGASOutboundEffectMapping> OutboundMappings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|GAS", meta = (AllowPrivateAccess = "true"))
	TArray<FMassGASInboundEffectMapping> InboundMappings;
};

