// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "MassAttributeSchema.generated.h"

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

struct MASSBILITY_API FMassCompiledAttributeSchema
{
	uint16 SchemaId = Massbility::InvalidId; //World Subsystem 内注册后的 Schema ID
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

/**
 * Editor-authored composition of reusable attribute sets. It is compiled once
 * into a fixed runtime slot layout when first registered by the world subsystem.
 */
UCLASS(BlueprintType)
class MASSBILITY_API UMassAttributeSchema : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	const TArray<TObjectPtr<UMassAttributeSet>>& GetAttributeSets() const { return AttributeSets; }

	/**
	 * Flattens attribute sets in array order and validates invalid/duplicate tags,
	 * invalid ranges, and the fixed extended-attribute capacity.
	 */
	bool Compile(FMassCompiledAttributeSchema& OutCompiledSchema, FString& OutError) const;

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Attribute", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UMassAttributeSet>> AttributeSets;
};
