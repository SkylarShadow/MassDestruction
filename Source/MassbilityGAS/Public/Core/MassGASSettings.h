// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "MassGASSettings.generated.h"

class UMassGASEffectMapping;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Massbility GAS"))
class MASSBILITYGAS_API UMassGASSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Mass"); }

	UPROPERTY(Config, EditAnywhere, Category = "Bridge")
	TSoftObjectPtr<UMassGASEffectMapping> EffectMapping;
};

