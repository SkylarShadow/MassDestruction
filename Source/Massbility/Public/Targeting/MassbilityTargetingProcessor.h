// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "MassbilityTargetingProcessor.generated.h"

UCLASS()
class MASSBILITY_API UMassbilitySpatialBuildProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilitySpatialBuildProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	// 空间快照不需要每个模拟 Tick 重建，按目标刷新周期错峰复用。
	FMassEntityQuery SourceQuery;
	uint32 LastBuildTick = MAX_uint32;
};

UCLASS()
class MASSBILITY_API UMassbilityTargetingProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilityTargetingProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	// 只写 Target Fragment；能力、战斗和 GAS 处理器消费这里保存的目标快照。
	FMassEntityQuery TargetingQuery;
};
