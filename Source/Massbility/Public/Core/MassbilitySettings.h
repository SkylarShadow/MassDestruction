// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "MassbilitySettings.generated.h"

class UMassbilityRuntimeTagCatalog;
class UMassbilityStateTagMapping;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Massbility"))
class MASSBILITY_API UMassbilitySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override;

	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Tags")
	TSoftObjectPtr<UMassbilityRuntimeTagCatalog> RuntimeTagCatalog;

	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Tags")
	TSoftObjectPtr<UMassbilityStateTagMapping> StateTagMapping;
};
