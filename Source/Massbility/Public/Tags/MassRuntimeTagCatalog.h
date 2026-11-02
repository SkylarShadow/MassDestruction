// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "MassRuntimeTagCatalog.generated.h"

struct MASSBILITY_API FMassCompiledRuntimeTagCatalog
{
	bool Compile(const TArray<FGameplayTag>& InRuntimeTags, FString& OutError);
    //将设计期 Tag 转换为 FMassTagMask
	bool BuildMask(const FGameplayTagContainer& Tags, FMassTagMask& OutMask, FString& OutError) const;
	bool BuildMask(const TArray<FGameplayTag>& Tags, FMassTagMask& OutMask, FString& OutError) const;
	/** Projects only tags represented by this catalog; unknown tags are intentionally ignored. */
	void BuildMaskFiltered(const FGameplayTagContainer& Tags, FMassTagMask& OutMask) const;
	bool IsCompiled() const { return bCompiled; }

private:
	bool BuildMaskInternal(TConstArrayView<FGameplayTag> Tags, FMassTagMask& OutMask, FString& OutError) const;
	TArray<FGameplayTag> TagsByIndex;
	TMap<FGameplayTag, uint16> TagToIndex;
	bool bCompiled = false;
};

//运行时 Tag 注册表
UCLASS(BlueprintType)
class MASSBILITY_API UMassRuntimeTagCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	const TArray<FGameplayTag>& GetRuntimeTags() const { return RuntimeTags; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Tags", meta = (AllowPrivateAccess = "true"))
	TArray<FGameplayTag> RuntimeTags; // 用于反查
};
