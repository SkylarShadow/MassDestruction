// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Effect/MassEffectTypes.h"
#include "Engine/DataAsset.h"

#include "MassEffectDefinition.generated.h"

UCLASS(BlueprintType)
class MASSBILITY_API UMassEffectDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	FGameplayTag GetEffectTag() const { return EffectTag; }
	EMassEffectDurationPolicy GetDurationPolicy() const { return DurationPolicy; }
	float GetDurationSeconds() const { return DurationSeconds; }
	float GetPeriodSeconds() const { return PeriodSeconds; }
	EMassEffectStackPolicy GetStackPolicy() const { return StackPolicy; }
	uint8 GetStackLimit() const { return StackLimit; }
	const FGameplayTagContainer& GetGrantedTags() const { return GrantedTags; }
	const TArray<FMassEffectModifierDefinition>& GetModifiers() const { return Modifiers; }

	bool Validate(FString& OutError) const;

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect", meta = (AllowPrivateAccess = "true"))
	FGameplayTag EffectTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect", meta = (AllowPrivateAccess = "true"))
	EMassEffectDurationPolicy DurationPolicy = EMassEffectDurationPolicy::Instant;

	/** Used only when DurationPolicy is Duration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect", meta = (ClampMin = "0.0", Units = "s", AllowPrivateAccess = "true"))
	float DurationSeconds = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect", meta = (ClampMin = "0.0", Units = "s", AllowPrivateAccess = "true"))
	float PeriodSeconds = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect", meta = (AllowPrivateAccess = "true"))
	EMassEffectStackPolicy StackPolicy = EMassEffectStackPolicy::RefreshDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect", meta = (ClampMin = "1", ClampMax = "255", AllowPrivateAccess = "true"))
	uint8 StackLimit = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect", meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer GrantedTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect", meta = (AllowPrivateAccess = "true"))
	TArray<FMassEffectModifierDefinition> Modifiers;
};
