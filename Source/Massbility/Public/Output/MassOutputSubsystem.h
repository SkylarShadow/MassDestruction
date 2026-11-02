// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Output/MassOutputTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassOutputSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassCombatSubsystem;
class UMassEffectSubsystem;
class UMassExternalTargetSubsystem;
class UMassRuntimeSubsystem;
struct FMassEntityManager;

UCLASS()
class MASSBILITY_API UMassOutputSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassOutputSubsystem* Get(const UObject* WorldContextObject);

	FMassOutputId RegisterOutput(const FMassOutputRegistration& Registration);
	const FMassCompiledOutput* FindOutput(FMassOutputId OutputId) const;
	bool CanRouteExternalOutput(FMassOutputId OutputId) const;
	/** Game-thread only. Parallel producers must merge into a game-thread submission stage. */
	bool QueueOutput(const FMassOutputRequest& Request);
	bool QueueOutputs(TConstArrayView<FMassOutputRequest> Requests);

	int32 GetPendingOutputCount() const { return PendingRequests.Num(); }
	const FMassOutputStats& GetStats() const { return Stats; }

private:
	friend class UMassOutputProcessor;

	void FlushRequests(FMassEntityManager& EntityManager, uint32 CurrentTick);
	EMassOutputFailure Dispatch(FMassEntityManager& EntityManager,
		const FMassOutputRequest& Request, uint32 CurrentTick);
	void RecordFailure(EMassOutputFailure Failure);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassRuntimeSubsystem> MassSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassEffectSubsystem> EffectSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassCombatSubsystem> CombatSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassExternalTargetSubsystem> ExternalTargetSubsystem;

	TArray<FMassCompiledOutput> CompiledOutputs;
	TArray<FMassOutputRequest> PendingRequests;
	int32 MaxPendingRequests = 4096;
	FMassOutputStats Stats;
};
