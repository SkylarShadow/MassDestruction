// Copyright MassDestruction. All Rights Reserved.

// Output Processor 在固定阶段刷新输出队列并执行统一分发。

#include "Output/MassOutputProcessor.h"

#include "Core/MassRuntimeSubsystem.h"
#include "MassExecutionContext.h"
#include "Output/MassOutputSubsystem.h"

// 构造输出处理器并配置其执行阶段。
UMassOutputProcessor::UMassOutputProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	bRequiresGameThreadExecution = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("Massbility.OutputDispatch"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("Massbility.Ability")));
}

// 输出处理不依赖实体查询裁剪，因此始终关闭查询裁剪。
bool UMassOutputProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false;
}

// 输出处理器没有实体级查询条件，但保留 Mass Processor 接口实现。
void UMassOutputProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
}

// 获取当前模拟 Tick，并刷新 Output Subsystem 中的待处理请求。
void UMassOutputProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Output 阶段统一刷新本 Tick 已排队的伤害、Effect 和外部输出。
	UWorld* World = Context.GetWorld();
	UMassOutputSubsystem* OutputSubsystem = World ? World->GetSubsystem<UMassOutputSubsystem>() : nullptr;
	UMassRuntimeSubsystem* CoreSubsystem = World ? World->GetSubsystem<UMassRuntimeSubsystem>() : nullptr;
	if (OutputSubsystem && CoreSubsystem)
	{
		OutputSubsystem->FlushRequests(EntityManager, CoreSubsystem->GetSimulationClock().GetCurrentTick());
	}
}

