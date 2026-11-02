// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "MassStateTagMapping.generated.h"

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassStateTagBinding
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|State")
	EMassStateFlags StateFlag = EMassStateFlags::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|State")
	TArray<FGameplayTag> GrantedTags;
};

/** Project-configurable bridge from Mass state flags to gameplay tags. */
UCLASS(BlueprintType)
class MASSBILITY_API UMassStateTagMapping : public UDataAsset
{
	GENERATED_BODY()

public:
	const TArray<FMassStateTagBinding>& GetBindings() const { return Bindings; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|State", meta = (AllowPrivateAccess = "true"))
	TArray<FMassStateTagBinding> Bindings;

};
