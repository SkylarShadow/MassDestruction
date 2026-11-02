// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Core/MassTypes.h"
#include "Effect/MassEffectTypes.h"
#include "External/MassExternalTargetTypes.h"
#include "GameplayTagContainer.h"
#include "Mass/EntityHandle.h"

#include "MassOutputTypes.generated.h"

class UMassEffectDefinition;

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassOutputId
{
	GENERATED_BODY()

	uint16 Value = Massbility::InvalidId;

	bool IsValid() const { return Value != Massbility::InvalidId; }
	friend bool operator==(const FMassOutputId& A, const FMassOutputId& B) { return A.Value == B.Value; }
};

/** Legacy author-facing name retained so existing ability assets keep their serialized field type. */
UENUM(BlueprintType)
enum class EMassAbilityOutputType : uint8
{
	Effect,
	Damage,
};

/** Author-time output payload. Runtime producers use the compiled FMassOutputId. */
USTRUCT(BlueprintType)
struct MASSBILITY_API FMassAbilityOutputDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output")
	EMassAbilityOutputType Type = EMassAbilityOutputType::Effect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output")
	TObjectPtr<UMassEffectDefinition> Effect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output", meta = (ClampMin = "0.0"))
	float Damage = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output")
	float MagnitudeScale = 1.0f;

	/** Stable cross-runtime identity. Effect outputs may default to the EffectTag during compilation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Output")
	FGameplayTag SemanticTag;
};

UENUM(BlueprintType)
enum class EMassOutputRelationPolicy : uint8
{
	Any,
	Self,
	SelfOrHostile,
	Friendly,
	Hostile,
};

/** Registration-only data. UObject references must be compiled before this reaches the registry. */
struct MASSBILITY_API FMassOutputRegistration
{
	EMassAbilityOutputType Type = EMassAbilityOutputType::Effect;
	FMassEffectId EffectId;
	FGameplayTag SemanticTag;
	FMassTagMask RequiredTargetTags;
	FMassTagMask BlockedTargetTags;
	float Damage = 0.0f;
	float MagnitudeScale = 1.0f;
	EMassOutputRelationPolicy RelationPolicy = EMassOutputRelationPolicy::Any;
	bool bRequireTargetable = true;
	bool bAllowDeadTarget = false;
	bool bAllowInvalidSource = false;
};

/** Immutable hot-path payload addressed by FMassOutputId. */
struct MASSBILITY_API FMassCompiledOutput : FMassOutputRegistration
{
	FMassOutputId OutputId;
};

/** Common request accepted from abilities and independent Mass hit producers. */
struct MASSBILITY_API FMassOutputRequest
{
	FMassEntityHandle Source;
	FMassEntityHandle Target;
	FMassOutputId OutputId;
	float MagnitudeScale = 1.0f;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	uint32 SimulationTick = 0;
};

/** Compact request produced only after the dispatcher resolves an external proxy target. */
struct MASSBILITY_API FMassExternalOutputRequest
{
	FMassEntityHandle Source;
	FMassExternalTargetHandle Target;
	FMassOutputId OutputId;
	float MagnitudeScale = 1.0f;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	uint32 SimulationTick = 0;
};

UENUM()
enum class EMassOutputFailure : uint8
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

struct MASSBILITY_API FMassOutputStats
{
	uint64 DefinitionsRegistered = 0;
	uint64 RequestsQueued = 0;
	uint64 RequestsDispatched = 0;
	uint64 RoutedToMass = 0;
	uint64 RoutedToExternal = 0;
	uint64 QueueHighWaterMark = 0;
	TStaticArray<uint64, static_cast<int32>(EMassOutputFailure::Count)> Failures = {};
};
