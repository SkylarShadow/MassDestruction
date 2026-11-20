// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "MassAttributeSet.generated.h"

//单个属性定义
USTRUCT(BlueprintType)
struct MASSBILITY_API FMassAttributeDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Attribute")
	FGameplayTag AttributeTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Attribute")
	float DefaultValue = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Attribute")
	float MinValue = -FLT_MAX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Attribute")
	float MaxValue = FLT_MAX;
};

//编译后的单个属性
struct MASSBILITY_API FMassCompiledAttribute
{
	FGameplayTag AttributeTag;
	float DefaultValue = 0.0f;
	float MinValue = -FLT_MAX;
	float MaxValue = FLT_MAX;
};

struct MASSBILITY_API FMassCompiledAttributeLayout
{
	uint16 LayoutId = Massbility::InvalidId; // World Subsystem 内注册后的布局 ID
	uint8 AttributeCount = 0; //实际使用了多少扩展属性槽
	TStaticArray<FMassCompiledAttribute, Massbility::MaxExtendedAttributes> Attributes; //编译后的固定长度属性定义
};

/** Reusable composition unit for related extended attributes. */
UCLASS(BlueprintType)
class MASSBILITY_API UMassAttributeSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	const TArray<FMassAttributeDefinition>& GetAttributes() const { return Attributes; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Attribute", meta = (AllowPrivateAccess = "true"))
	TArray<FMassAttributeDefinition> Attributes;
};

/** Flatten an ordered set list into a reusable runtime layout. */
struct MASSBILITY_API FMassAttributeLayoutCompiler
{
	static bool Compile(const TArray<TObjectPtr<UMassAttributeSet>>& Sets,
		FMassCompiledAttributeLayout& OutLayout, FString& OutError);
};
