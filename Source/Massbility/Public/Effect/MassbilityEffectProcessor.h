// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "MassbilityEffectProcessor.generated.h"
//当前帧或当前模拟 Tick 中排队的effect 请求
UCLASS()
class MASSBILITY_API UMassbilityEffectApplyProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilityEffectApplyProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery WriteDependencyQuery;
};

//已经存在的 active effect 的周期推进和到期处理
UCLASS()
class MASSBILITY_API UMassbilityEffectProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilityEffectProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery ActiveEffectQuery;
	uint32 LastProcessedTick = MAX_uint32;
};
