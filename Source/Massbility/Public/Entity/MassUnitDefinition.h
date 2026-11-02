// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "MassUnitDefinition.generated.h"

class UMassAttributeSchema;
class UMassAbilityDefinition;

/** 大规模实体的Data asset */
UCLASS(BlueprintType)
class MASSBILITY_API UMassUnitDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UMassAttributeSchema* GetAttributeSchema() const { return AttributeSchema; }
	float GetInitialHealth() const { return InitialHealth; }
	float GetInitialMaxHealth() const { return InitialMaxHealth; }
	float GetInitialMoveSpeed() const { return InitialMoveSpeed; }
	int32 GetTeamId() const { return TeamId; }
	const FGameplayTagContainer& GetInitialRuntimeTags() const { return InitialRuntimeTags; }
	const TArray<TObjectPtr<UMassAbilityDefinition>>& GetAbilities() const { return Abilities; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Unit", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMassAttributeSchema> AttributeSchema;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Unit", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float InitialHealth = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Unit", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float InitialMaxHealth = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Unit", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float InitialMoveSpeed = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Unit", meta = (AllowPrivateAccess = "true"))
	int32 TeamId = 0;   //TODO: 要不要移出，或者单独分一个fragment

	/** Author-time tags. The entity receives only the compiled mask at spawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Tags", meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer InitialRuntimeTags; //单位出生时具备的玩法 Tag

	/** Ordered ability slots. Phase 3 supports at most four entries per unit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Abilities", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UMassAbilityDefinition>> Abilities;
};
