// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassbilityTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "MassbilityRuntimeTagCatalog.generated.h"

struct MASSBILITY_API FMassbilityCompiledRuntimeTagCatalog
{
	bool Compile(const TArray<FGameplayTag>& InRuntimeTags, FString& OutError);
    //将设计期 Tag 转换为 FMassbilityTagMask
	bool BuildMask(const FGameplayTagContainer& Tags, FMassbilityTagMask& OutMask, FString& OutError) const;
	bool BuildMask(const TArray<FGameplayTag>& Tags, FMassbilityTagMask& OutMask, FString& OutError) const;
	bool IsCompiled() const { return bCompiled; }

private:
	bool BuildMaskInternal(TConstArrayView<FGameplayTag> Tags, FMassbilityTagMask& OutMask, FString& OutError) const;
	TArray<FGameplayTag> TagsByIndex;
	TMap<FGameplayTag, uint16> TagToIndex;
	bool bCompiled = false;
};

//运行时 Tag 注册表
UCLASS(BlueprintType)
class MASSBILITY_API UMassbilityRuntimeTagCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	const TArray<FGameplayTag>& GetRuntimeTags() const { return RuntimeTags; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Tags", meta = (AllowPrivateAccess = "true"))
	TArray<FGameplayTag> RuntimeTags; // 用于反查
};
