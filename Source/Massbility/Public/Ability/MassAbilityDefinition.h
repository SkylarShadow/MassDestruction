// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Ability/MassAbilityTypes.h"
#include "Engine/DataAsset.h"

#include "MassAbilityDefinition.generated.h"

UCLASS(BlueprintType)
class MASSBILITY_API UMassAbilityDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	FGameplayTag GetAbilityTag() const { return AbilityTag; }
	const FGameplayTagContainer& GetAbilityTags() const { return AbilityTags; }
	float GetCooldownSeconds() const { return CooldownSeconds; }
	const FGameplayTagContainer& GetRequiredOwnerTags() const { return RequiredOwnerTags; }
	const FGameplayTagContainer& GetBlockedOwnerTags() const { return BlockedOwnerTags; }
	const FGameplayTagContainer& GetRequiredTargetTags() const { return RequiredTargetTags; }
	const FGameplayTagContainer& GetBlockedTargetTags() const { return BlockedTargetTags; }
	EMassAbilityTargetPolicy GetTargetPolicy() const { return TargetPolicy; }
	float GetRange() const { return Range; }
	bool Uses2DDistance() const { return bUse2DDistance; }
	const TArray<FMassAbilityOutputDefinition>& GetOutputs() const { return Outputs; }
	bool Validate(FString& OutError) const;

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability", meta = (AllowPrivateAccess = "true"))
	FGameplayTag AbilityTag;

	/** 能力类别。Effect 可通过 BlockedAbilityTags 阻止匹配的能力。AbilityTag 本身也会自动加入该集合。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability|Tags", meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer AbilityTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability", meta = (ClampMin = "0.0", Units = "s", AllowPrivateAccess = "true"))
	float CooldownSeconds = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability|Tags", meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer RequiredOwnerTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability|Tags", meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer BlockedOwnerTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability|Tags", meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer RequiredTargetTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability|Tags", meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer BlockedTargetTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability|Targeting", meta = (AllowPrivateAccess = "true"))
	EMassAbilityTargetPolicy TargetPolicy = EMassAbilityTargetPolicy::CurrentTarget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability|Targeting", meta = (ClampMin = "0.0", Units = "cm", AllowPrivateAccess = "true"))
	float Range = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability|Targeting", meta = (AllowPrivateAccess = "true"))
	bool bUse2DDistance = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability", meta = (AllowPrivateAccess = "true"))
	TArray<FMassAbilityOutputDefinition> Outputs;
};

