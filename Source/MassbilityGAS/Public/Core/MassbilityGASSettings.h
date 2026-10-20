// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "MassbilityGASSettings.generated.h"

class UMassbilityGASEffectMapping;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Massbility GAS"))
class MASSBILITYGAS_API UMassbilityGASSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Massbility"); }

	UPROPERTY(Config, EditAnywhere, Category = "Bridge")
	TSoftObjectPtr<UMassbilityGASEffectMapping> EffectMapping;
};

