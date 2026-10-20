// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Core/MassbilityTypes.h"
#include "Effect/MassbilityEffectTypes.h"
#include "External/MassbilityExternalTargetTypes.h"
#include "GameplayTagContainer.h"
#include "Mass/EntityHandle.h"

#include "MassbilityOutputTypes.generated.h"

class UMassbilityEffectDefinition;

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassbilityOutputId
{
	GENERATED_BODY()

	uint16 Value = Massbility::InvalidId;

	bool IsValid() const { return Value != Massbility::InvalidId; }
	friend bool operator==(const FMassbilityOutputId& A, const FMassbilityOutputId& B) { return A.Value == B.Value; }
};

/** Legacy author-facing name retained so existing ability assets keep their serialized field type. */
UENUM(BlueprintType)
enum class EMassbilityAbilityOutputType : uint8
{
	Effect,
	Damage,
};

/** Author-time output payload. Runtime producers use the compiled FMassbilityOutputId. */
USTRUCT(BlueprintType)
struct MASSBILITY_API FMassbilityAbilityOutputDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output")
	EMassbilityAbilityOutputType Type = EMassbilityAbilityOutputType::Effect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output")
	TObjectPtr<UMassbilityEffectDefinition> Effect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output", meta = (ClampMin = "0.0"))
	float Damage = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output")
	float MagnitudeScale = 1.0f;

	/** Stable cross-runtime identity. Effect outputs may default to the EffectTag during compilation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output")
	FGameplayTag SemanticTag;
};

UENUM(BlueprintType)
enum class EMassbilityOutputRelationPolicy : uint8
{
	Any,
	Self,
	SelfOrHostile,
	Friendly,
	Hostile,
};

/** Registration-only data. UObject references must be compiled before this reaches the registry. */
struct MASSBILITY_API FMassbilityOutputRegistration
{
	EMassbilityAbilityOutputType Type = EMassbilityAbilityOutputType::Effect;
	FMassbilityEffectId EffectId;
	FGameplayTag SemanticTag;
	FMassbilityTagMask RequiredTargetTags;
	FMassbilityTagMask BlockedTargetTags;
	float Damage = 0.0f;
	float MagnitudeScale = 1.0f;
	EMassbilityOutputRelationPolicy RelationPolicy = EMassbilityOutputRelationPolicy::Any;
	bool bRequireTargetable = true;
	bool bAllowDeadTarget = false;
	bool bAllowInvalidSource = false;
};

/** Immutable hot-path payload addressed by FMassbilityOutputId. */
struct MASSBILITY_API FMassbilityCompiledOutput : FMassbilityOutputRegistration
{
	FMassbilityOutputId OutputId;
};

/** Common request accepted from abilities and independent Mass hit producers. */
struct MASSBILITY_API FMassbilityOutputRequest
{
	FMassEntityHandle Source;
	FMassEntityHandle Target;
	FMassbilityOutputId OutputId;
	float MagnitudeScale = 1.0f;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	uint32 SimulationTick = 0;
};

/** Compact request produced only after the dispatcher resolves an external proxy target. */
struct MASSBILITY_API FMassbilityExternalOutputRequest
{
	FMassEntityHandle Source;
	FMassbilityExternalTargetHandle Target;
	FMassbilityOutputId OutputId;
	float MagnitudeScale = 1.0f;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	uint32 SimulationTick = 0;
};

UENUM()
enum class EMassbilityOutputFailure : uint8
{
	None,
	InvalidOutput,
	InvalidSource,
	InvalidTarget,
	TargetState,
	Relation,
	TargetTags,
	NoExternalRoute,
	DownstreamRejected,
	Capacity,
	WrongThread,
	Count UMETA(Hidden),
};

struct MASSBILITY_API FMassbilityOutputStats
{
	uint64 DefinitionsRegistered = 0;
	uint64 RequestsQueued = 0;
	uint64 RequestsDispatched = 0;
	uint64 RoutedToMass = 0;
	uint64 RoutedToExternal = 0;
	uint64 QueueHighWaterMark = 0;
	TStaticArray<uint64, static_cast<int32>(EMassbilityOutputFailure::Count)> Failures = {};
};
