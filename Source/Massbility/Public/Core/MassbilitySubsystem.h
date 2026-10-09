// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Attributes/MassbilityAttributeSchema.h"
#include "Core/MassbilityTypes.h"
#include "Mass/EntityHandle.h"
#include "MassArchetypeTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tags/MassbilityRuntimeTagCatalog.h"
#include "Tags/MassbilityStateTagMapping.h"

#include "MassbilitySubsystem.generated.h"

class UMassEntitySubsystem;
class UMassbilityRuntimeTagCatalog;
class UMassbilityStateTagMapping;
class UMassbilityUnitDefinition;
struct FMassbilityEntityFactory;

UCLASS()
class MASSBILITY_API UMassbilitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassbilitySubsystem* Get(const UObject* WorldContextObject);

	FMassEntityHandle CreateUnit(const UMassbilityUnitDefinition& UnitDefinition);
	bool DestroyUnit(FMassEntityHandle Entity);

	bool ResolveAttributeHandle(const UMassbilityAttributeSchema& Schema, FGameplayTag AttributeTag, FMassbilityAttributeHandle& OutHandle) const;
	bool GetExtendedAttribute(FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float& OutValue) const;
	bool SetExtendedAttribute(FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float Value);
	bool AddExtendedAttribute(FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float Delta);
	bool SetStateFlags(FMassEntityHandle Entity, EMassbilityStateFlags NewFlags);
	bool HasRuntimeTags(FMassEntityHandle Entity, const FMassbilityTagMask& Required, const FMassbilityTagMask& Blocked = {}) const;

private:
	friend struct FMassbilityEntityFactory;

	bool CompileSettings();
	bool RegisterSchema(const UMassbilityAttributeSchema& Schema, uint16& OutSchemaId);
	bool RegisterUnitDefinition(const UMassbilityUnitDefinition& UnitDefinition, uint16& OutUnitDefinitionId, uint16& OutSchemaId);
	bool BuildStateMask(EMassbilityStateFlags Flags, FMassbilityTagMask& OutMask) const;
	FMassArchetypeHandle GetOrCreateUnitArchetype();

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityRuntimeTagCatalog> RuntimeTagCatalogAsset;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityStateTagMapping> StateTagMappingAsset;

	FMassbilityCompiledRuntimeTagCatalog RuntimeTagCatalog;
	TMap<const UMassbilityAttributeSchema*, uint16> SchemaIds;
	TArray<FMassbilityCompiledAttributeSchema> CompiledSchemas;
	TMap<const UMassbilityUnitDefinition*, uint16> UnitDefinitionIds;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMassbilityUnitDefinition>> UnitDefinitions;
	TMap<uint32, FMassbilityTagMask> StateTagMasks;
	FMassArchetypeHandle UnitArchetype;
};
