// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassProcessor.h"

#include "MassGASBridgeProcessor.generated.h"

UCLASS()
class MASSBILITYGAS_API UMassGASProxySyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassGASProxySyncProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSBILITYGAS_API UMassGASBridgeProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassGASBridgeProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

