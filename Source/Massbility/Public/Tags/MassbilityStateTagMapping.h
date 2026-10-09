// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassbilityTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "MassbilityStateTagMapping.generated.h"

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassbilityStateTagBinding
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|State")
	EMassbilityStateFlags StateFlag = EMassbilityStateFlags::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|State")
	TArray<FGameplayTag> GrantedTags;
};

/** Project-configurable bridge from Mass state flags to gameplay tags. */
UCLASS(BlueprintType)
class MASSBILITY_API UMassbilityStateTagMapping : public UDataAsset
{
	GENERATED_BODY()

public:
	const TArray<FMassbilityStateTagBinding>& GetBindings() const { return Bindings; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|State", meta = (AllowPrivateAccess = "true"))
	TArray<FMassbilityStateTagBinding> Bindings;

};
