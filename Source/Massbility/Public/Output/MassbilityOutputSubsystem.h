// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Output/MassbilityOutputTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassbilityOutputSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassbilityCombatSubsystem;
class UMassbilityEffectSubsystem;
class UMassbilityExternalTargetSubsystem;
class UMassbilitySubsystem;
struct FMassEntityManager;

UCLASS()
class MASSBILITY_API UMassbilityOutputSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassbilityOutputSubsystem* Get(const UObject* WorldContextObject);

	FMassbilityOutputId RegisterOutput(const FMassbilityOutputRegistration& Registration);
	const FMassbilityCompiledOutput* FindOutput(FMassbilityOutputId OutputId) const;
	bool CanRouteExternalOutput(FMassbilityOutputId OutputId) const;
	/** Game-thread only. Parallel producers must merge into a game-thread submission stage. */
	bool QueueOutput(const FMassbilityOutputRequest& Request);
	bool QueueOutputs(TConstArrayView<FMassbilityOutputRequest> Requests);

	int32 GetPendingOutputCount() const { return PendingRequests.Num(); }
	const FMassbilityOutputStats& GetStats() const { return Stats; }

private:
	friend class UMassbilityOutputProcessor;

	void FlushRequests(FMassEntityManager& EntityManager, uint32 CurrentTick);
	EMassbilityOutputFailure Dispatch(FMassEntityManager& EntityManager,
		const FMassbilityOutputRequest& Request, uint32 CurrentTick);
	void RecordFailure(EMassbilityOutputFailure Failure);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilitySubsystem> MassbilitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityEffectSubsystem> EffectSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityCombatSubsystem> CombatSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityExternalTargetSubsystem> ExternalTargetSubsystem;

	TArray<FMassbilityCompiledOutput> CompiledOutputs;
	TArray<FMassbilityOutputRequest> PendingRequests;
	int32 MaxPendingRequests = 4096;
	FMassbilityOutputStats Stats;
};
