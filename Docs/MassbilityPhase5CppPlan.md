# Massbility Phase 5：通用扩展与规模化运行时 C++ 落地方案

## 1. 阶段定位

Phase 5 不实现具体项目玩法，而是在 Phase 1 至 Phase 4 的战斗闭环上补齐通用扩展接口、强制位移能力和规模化调度基础。

本阶段只对接 Unreal 原生 GAS 边界，不依赖项目专属队伍、AttributeSet、伤害事件或角色框架。

阶段目标：

- 把 Ability 的 Damage/Effect 输出抽成可被其他 Mass 系统复用的通用提交接口。
- 提供与玩法名称无关的强制位移 Request、状态、优先级和移动适配边界。
- 允许独立的 Mass 投射物、范围查询或其他命中系统向 Massbility 提交输出，而不由 Massbility 管理它们的模拟。
- 保留并加固现有死亡事件，明确生命周期和消费时序。
- 为 Targeting、Ability 和 Effect 提供通用模拟 LOD/预算入口。
- 提供只读表现快照与事件边界，但不在本阶段绑定某一种表现方案。
- 保持 `Massbility` 核心无 `GameplayAbilities`、Niagara 和项目玩法模块依赖。

### 实现状态（2026-10-13）

Phase 5A、5B 和 5D 的第一轮通用 C++ 主链已落地，并通过 `PrjInitEditor Win64 Development` 编译：

- 已新增独立 `FMassbilityOutputId`、Compiled Output Registry、批量请求队列和 `Massbility.OutputDispatch` Processor。
- 现有即时 Ability 已迁移到 Output Request；不再直接调用 Combat/Effect，也不再以 `AbilityId + OutputIndex` 驱动 GAS Bridge。
- Phase 4 External Output 与 `MassbilityGAS` 已迁移到 OutputId，Mass Target 与原生 GAS Proxy 继续使用同一输出语义。
- 已新增 Velocity、Displacement、ToLocation 三种通用强制位移模式，以及 Priority、Replace/Additive/RejectIfActive 仲裁。
- 已新增纯 C++ Movement Adapter 边界；没有 Adapter 时只允许明确的 Ignore Collision Policy。
- 当前基础 Unit Archetype 包含固定强制位移 Fragment，但 Processor 只查询带 Active Tag 的实体，更新成本与活动位移数相关。
- 已新增当前 Simulation Tick 的只读 Death Event Batch；兼容 `ConsumeDeathEvents` 不再清空共享事件。
- 输出队列和强制位移队列已有容量、高水位和拒绝原因统计。

尚未落地：

- Phase 5E Simulation LOD/Observer 调度。
- Phase 5F Representation Snapshot 测试协议。
- External GAS Target 的 Movement Adapter；当前会明确返回 Unsupported，不修改 Proxy 或 Avatar Transform。
- 正式独立投射物模块；当前只提供它所需的公共 Output 提交接口。

## 2. 明确不属于 Phase 5 的内容

以下内容全部移出 Phase 5，不作为当前框架的类型、Processor 或验收项：

- 冲锋技能、感染技能、特殊怪技能等具体玩法。
- 内建 Mass 投射物模拟、投射物移动、碰撞、穿透、追踪或弹跳。
- 内建范围伤害实体或持续区域玩法。
- 召唤物系统和召唤链归属。
- 等级、词条、单位变体和 Roguelike 成长。
- 波次导演、刷怪规则和 Threat Budget。
- 奖励事件、经验、掉落、背包、经济和拾取物聚合。
- 完整能力前摇/执行/后摇状态机；没有具体需求前继续保留即时 Ability 模型。
- ISM、Niagara、骨骼网格对象池的具体实现。
- 项目专属导航、群体避障、角色移动或网络预测。

这些系统以后可以作为独立模块使用 Phase 5 的公共接缝，但不能反向成为 `Massbility` 核心依赖。

## 3. 当前基础与真正缺口

Phase 1 至 Phase 4 已具备：

- Unit、属性、Runtime Tag Mask、StateFlags 和固定 Active Effect Slot。
- Damage/Effect Request、周期效果、死亡与击杀来源。
- 即时 Ability、共享 Loadout、冷却和目标校验。
- 2D Spatial Hash、最近敌对目标和错峰 Target Refresh。
- External Target Proxy、跨域请求和原生 GAS GameplayEffectSpec 映射。

当前真正需要补的通用缺口只有：

1. Ability Output 仍以 `AbilityId + OutputIndex` 为身份，独立模块无法自然复用。
2. 没有统一的强制位移请求和所有权，Combat、Ability 或外部系统容易直接争写 Transform。
3. 外部 Mass 模拟系统没有稳定的命中输出提交协议和 Processor 可见性约定。
4. Death Event 虽已存在，但销毁时序、队列寿命和消费者约束需要固定。
5. Targeting、Ability 和 Effect 尚无统一的 LOD/预算策略入口。

## 4. 总体边界

```text
Massbility Ability ───────────────┐
                                 │
External Mass Projectile System ─┼─> FMassbilityOutputRequest
                                 │             |
External Area/Trace System ──────┘             v
                                      Output Dispatcher
                                      /               \
                              Mass Entity          External Proxy
                           Damage/Effect Queue     Phase 4 GAS Bridge

Ability / Effect / External System
                  |
                  v
       FMassbilityForcedMovementRequest
                  |
                  v
       Forced Movement Arbitration
                  |
                  v
       Movement Adapter / Default Integrator
```

边界原则：

- Massbility 管理输出语义和 Mass 战斗结算，不拥有外部投射物系统的实体或模拟循环。
- 外部系统负责“何时、何处、命中了谁”；Massbility 负责“该命中如何转成 Mass Effect/Damage 或原生 GAS 输出”。
- 强制位移只表达运动意图；具体世界碰撞和最终 Transform 写入由移动适配层负责。
- 死亡事件是通用战斗事实，不扩展为奖励或掉落事件。
- LOD 只降低自主轮询频率，不能丢失已经提交的 Damage、Effect、Forced Movement 或 Death Event。

## 5. Phase 5A：独立 Output 身份与提交接口

### 5.1 `FMassbilityOutputId`

将可执行输出从 Ability 身份中抽离：

```cpp
USTRUCT(BlueprintType)
struct MASSBILITY_API FMassbilityOutputId
{
    GENERATED_BODY()

    uint16 Value = Massbility::InvalidId;

    bool IsValid() const;
};
```

Compiled Output 仍只表达已有两种权威结果：

```cpp
UENUM()
enum class EMassbilityOutputRelationPolicy : uint8
{
    Any,
    Self,
    Friendly,
    Hostile,
};

struct FMassbilityCompiledOutput
{
    EMassbilityAbilityOutputType Type = EMassbilityAbilityOutputType::Effect;
    FMassbilityEffectId EffectId;
    FGameplayTag SemanticTag;
    FMassbilityTagMask RequiredTargetTags;
    FMassbilityTagMask BlockedTargetTags;
    float Damage = 0.0f;
    float BaseMagnitudeScale = 1.0f;
    EMassbilityOutputRelationPolicy RelationPolicy = EMassbilityOutputRelationPolicy::Any;
    bool bAllowDeadTarget = false;
    bool bAllowInvalidSource = false;
};
```

首轮可以继续复用现有作者侧 `FMassbilityAbilityOutputDefinition`，但注册完成后 Ability 只保存 `FMassbilityOutputId`。Ability 的 Target Relation 和 Required/Blocked Target Tags 在注册时一并编译进该 Output；外部模块注册时显式提供自己的 Policy。相同 Damage/Effect Payload 配合不同 Policy 时是不同 OutputId，避免运行时携带可变过滤规则。

运行时实体和请求不保存 Data Asset、GameplayEffectSpec 或 GameplayTagContainer。

### 5.2 通用请求

```cpp
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
```

提交 API：

```cpp
bool SubmitOutput(const FMassbilityOutputRequest& Request);
```

`SubmitOutput` 只负责固定成本校验和入队，不同步执行 Effect、Damage 或 GAS 调用。

### 5.3 Dispatcher 路由

统一 Dispatcher：

1. 校验 OutputId 和 Target Handle；Source 无效时按 `bAllowInvalidSource` 接受或拒绝。
2. 按 Compiled Output Policy 校验目标的存活、关系和 Tag 条件。
3. 普通 Mass Target 转为现有 `DamageRequest` 或 `EffectRequest`。
4. External Proxy Target 转为 Phase 4 `ExternalOutputRequest`。
5. 记录拒绝原因、队列水位和成功路由数。

为了兼容 Phase 4，`ExternalOutputRequest` 应逐步改为保存 `OutputId`，而不是 `AbilityId + OutputIndex`。`MassbilityGAS` 通过 Output Registry 读取 SemanticTag 和数值。

### 5.4 外部模块注册规则

- Output Definition 只能在 World 初始化、资产预热或显式注册阶段编译。
- 稳定战斗阶段不能首次加载资产或扩展 Registry。
- 重复定义按稳定资产身份或内容键去重。
- Registry 对外只暴露只读查询和 `OutputId`，不暴露可写容器。
- 外部模块卸载时不能使已经排队的 ID 指向另一份定义；ID 在当前 World 生命周期内不复用。

## 6. Phase 5B：通用强制位移

### 6.1 语义范围

核心不定义 Charge、Knockback、Pull、Dash 等玩法枚举。它只提供可组合的强制位移参数。调用方可以用同一请求表达：

- 沿方向推开或拉近。
- 朝世界位置移动。
- 短时间固定速度移动。
- 瞬时位置修正。
- 覆盖、叠加或拒绝低优先级运动。

### 6.2 请求结构

```cpp
UENUM(BlueprintType)
enum class EMassbilityForcedMovementMode : uint8
{
    Velocity,
    Displacement,
    ToLocation,
};

UENUM(BlueprintType)
enum class EMassbilityForcedMovementBlend : uint8
{
    Replace,
    Additive,
    RejectIfActive,
};

UENUM(BlueprintType)
enum class EMassbilityForcedMovementCollisionPolicy : uint8
{
    AdapterDefault,
    Stop,
    Slide,
    Ignore,
};

struct MASSBILITY_API FMassbilityForcedMovementRequest
{
    FMassEntityHandle Source;
    FMassEntityHandle Target;
    FVector Vector = FVector::ZeroVector;
    uint32 DurationTicks = 1;
    uint32 RequestSerial = 0;
    uint8 Priority = 0;
    EMassbilityForcedMovementMode Mode = EMassbilityForcedMovementMode::Displacement;
    EMassbilityForcedMovementBlend Blend = EMassbilityForcedMovementBlend::Replace;
    EMassbilityForcedMovementCollisionPolicy CollisionPolicy =
        EMassbilityForcedMovementCollisionPolicy::AdapterDefault;
    uint32 SimulationTick = 0;
};
```

`Vector` 的含义由 Mode 固定：

```text
Velocity      世界空间速度
Displacement  世界空间总位移
ToLocation    世界空间目标位置
```

方向关系必须由提交方预先解析。核心不保存“远离施法者”或“朝目标冲锋”这样的玩法概念。

### 6.3 活动状态

只有需要跨 Tick 移动的 Archetype 才添加 Fragment：

```cpp
USTRUCT()
struct FMassbilityForcedMovementFragment : public FMassFragment
{
    GENERATED_BODY()

    FVector StartLocation = FVector::ZeroVector;
    FVector Vector = FVector::ZeroVector;
    uint32 StartTick = 0;
    uint32 EndTick = 0;
    uint32 RequestSerial = 0;
    uint8 Priority = 0;
    EMassbilityForcedMovementMode Mode = EMassbilityForcedMovementMode::Displacement;
    EMassbilityForcedMovementCollisionPolicy CollisionPolicy =
        EMassbilityForcedMovementCollisionPolicy::AdapterDefault;
    bool bActive = false;
};
```

首版每个实体只保留一个活动强制位移。Additive Request 在接收阶段折算到当前状态，不增加动态数组。

### 6.4 仲裁规则

固定规则：

1. Dead Target 拒绝新请求并清理活动位移。
2. 更高 Priority 可以 Replace 更低 Priority。
3. 同 Priority 按 Blend 和 RequestSerial 确定结果。
4. 更低 Priority 的 Replace 请求被拒绝。
5. AbilityBlocked 默认不阻止外部强制位移；需要免疫时使用独立 StateFlag 或 Runtime Tag 条件。
6. 请求被接受与最终移动距离分开统计。

同 Tick 多请求必须按稳定键排序或使用确定性合并，不能依赖多线程提交顺序。

### 6.5 移动适配边界

Combat、Effect 和 Ability 不直接写 `FTransformFragment`。Forced Movement Processor 先计算期望位移，再交给 Adapter：

```cpp
struct FMassbilityMovementResolveParams
{
    FMassEntityHandle Entity;
    FVector CurrentLocation;
    FVector DesiredDelta;
    EMassbilityForcedMovementCollisionPolicy CollisionPolicy;
};

struct FMassbilityMovementResolveResult
{
    FVector AppliedDelta;
    bool bBlocked = false;
    bool bFinished = false;
};
```

首版可提供只服务于 `FTransformFragment` 的默认 Kinematic Adapter。以后若项目使用 `MassMovement`、ZoneGraph 或自己的移动系统，只替换 Adapter，不修改 Combat/Effect/Ability。

世界碰撞是否 Stop、Slide 或 Ignore 由 Adapter 实现。没有世界碰撞能力时，只允许显式使用 `Ignore`，不能把未检测碰撞伪装为 Stop/Slide 成功。

### 6.6 External GAS Target

原生 GAS 没有统一的强制位移协议，因此核心不能直接移动 External Proxy 背后的 Actor。

首版规则：

- `Target` 是普通 Mass Entity：进入 Forced Movement Queue。
- `Target` 是 External Proxy：默认返回 `UnsupportedExternalTarget`。
- `MassbilityGAS` 可选注册一个 External Movement Adapter，将请求转换为项目自行处理的原生 Ability/Root Motion/Movement Component 行为。
- 未注册 Adapter 时绝不直接修改 Proxy Transform 或 Avatar Actor Transform。

## 7. Phase 5C：外部 Mass 投射物与命中系统接缝

### 7.1 所有权边界

Mass 投射物可以由另一个专用 Mass 模块或插件负责，以便它自行选择更合适的 Archetype、更新频率、空间结构、SIMD 布局和表现方式。

Massbility 不实现也不假设：

- Projectile Fragment 或 Projectile Archetype。
- 投射物 Transform、Velocity、Lifetime 和对象池。
- 世界碰撞、Swept Query、穿透、追踪或弹跳。
- 投射物 LOD、复制和表现。
- 投射物 Processor 的内部顺序。

### 7.2 最小接入协议

外部投射物系统只需要：

1. 在初始化/预热阶段注册命中 Output，取得 `FMassbilityOutputId`。
2. 自己完成投射物生成、移动、碰撞和目标选择。
3. 命中时构造 `FMassbilityOutputRequest`。
4. 在 `Massbility.OutputDispatch` 之前提交请求。

示例：

```cpp
FMassbilityOutputRequest Request;
Request.Source = ProjectileSourceEntity;
Request.Target = HitMassEntityOrExternalProxy;
Request.OutputId = ProjectileHitOutputId;
Request.MagnitudeScale = ProjectileMagnitudeScale;
Request.Origin = PreviousLocation;
Request.Direction = TravelDirection;
Request.SimulationTick = Massbility->GetSimulationTick();

MassbilityOutputSubsystem->SubmitOutput(Request);
```

外部系统不需要判断 Target 是否为普通 Mass Entity 或 External GAS Proxy。

### 7.3 空间查询可选复用

如果外部投射物系统希望复用 Massbility 的目标空间索引，可增加只读、无分配的候选访问 API：

```cpp
void ForEachCandidateInBounds(
    const FBox& Bounds,
    const FMassbilitySpatialQueryFilter& Filter,
    FMassbilitySpatialVisitFunction Visit) const;
```

这只是可选查询服务：

- Projectile 系统可以使用自己的 Broad Phase。
- Massbility 不保存 Projectile Handle。
- Massbility 不回调 Projectile 对象。
- 查询返回的 Entity Handle 仍需在提交命中时重新校验。

### 7.4 来源失效

投射物可能在 Source Entity 已死亡或销毁后命中。外部系统必须在生成时决定归属策略：

- 要求 Source 仍有效，否则丢弃命中。
- 允许 Invalid Source，但保留 Output 和命中结果。
- 使用外部系统自己的稳定 Attribution，再在边界处解析为当前有效 Mass Source/External Proxy。

Massbility 首版不新增通用跨 World Source Handle。无效 Source 是否允许由 Compiled Output Policy 决定，默认 Damage/Effect 允许无 Source，但 Death Event Source 为 Invalid。

### 7.5 Processor 时序契约

```text
External Projectile/Hit Producer
  -> Massbility.OutputDispatch
  -> Massbility.GASBridge
  -> Massbility.EffectApply
  -> Massbility.EffectTick
  -> Massbility.Death
```

外部模块必须显式声明 `ExecuteBefore("Massbility.OutputDispatch")`，或接受请求在下一 Simulation Tick 结算。禁止依赖模块加载顺序。

当前队列 API 为 Game Thread only。并行 Projectile Processor 应先写入自己的 Chunk/Processor 本地缓冲，再在一个明确的 Game Thread 汇总阶段批量调用 `QueueOutputs`，不能从并行 Chunk 直接写 World Subsystem 的共享数组。

## 8. Phase 5D：死亡事件加固

### 8.1 保留现有事件

继续使用已有事件：

```cpp
struct FMassbilityDeathEvent
{
    FMassEntityHandle Victim;
    FMassEntityHandle Source;
    FMassbilityEffectId EffectId;
    uint32 DeathTick = 0;
};
```

Phase 5 不增加 RewardEvent、ExperienceValue、RewardTableId 或掉落信息。

### 8.2 生命周期协议

- 每个实体只产生一次 Death Event。
- Death Event 在实体进入 Dead 状态后、实际销毁前入队。
- 队列至少保持到当前 Simulation Tick 的所有注册消费者完成。
- 消费者不能假设下一 Tick 仍能通过 Victim Handle 读取 Fragment。
- 需要保留的数据应在消费时复制到项目自己的系统，不应要求 Massbility 延长尸体实体寿命。
- Source 失效是合法状态；消费者必须处理 Invalid Source。
- 周期效果和外部系统提交的 Output 必须继续保留正确 Source/EffectId。

### 8.3 消费 API

优先提供批量只读访问或 Drain-to-buffer API，不为每个 Death Event 广播动态 Delegate：

```cpp
TConstArrayView<FMassbilityDeathEvent> GetDeathEventsForCurrentTick() const;
```

若多个消费者需要不同生命周期，则由 Subsystem 使用双缓冲 Event Batch 和注册的 Processor 时序，而不是每个消费者移除队列元素。

## 9. Phase 5E：通用模拟 LOD 与预算

### 9.1 LOD 只控制自主更新

```cpp
UENUM()
enum class EMassbilitySimulationLOD : uint8
{
    High,
    Medium,
    Low,
    Off,
};
```

允许降频：

- Spatial/Target Refresh。
- Ability 自动轮询。
- 非关键状态刷新。
- Effect 的扫描工作，但不能改变绝对时间语义。

不允许降频或丢弃：

- 已提交的 Output、Damage 和 Effect Request。
- Forced Movement Request 及活动位移积分。
- Death 判定和 Death Event。
- External GAS Bridge 中已经入队的请求。

### 9.2 Observer 与引擎 MassLOD

Observer 通过通用注册 API 提供位置和权重，不依赖项目 Pawn 类。优先验证并复用引擎 `MassLOD` 的 Viewer/距离计算；若 Dedicated Server 或无表现 World 不适合该路径，则使用轻量 Observer Adapter。

Massbility 的 Simulation LOD 与 Representation LOD 分离。前者决定战斗系统轮询频率，后者由表现模块决定可见形态。

### 9.3 Effect 时间正确性

Active Effect 已使用绝对 Simulation Tick：

- 到期时间不因 LOD 改变。
- 周期效果可以按绝对 Tick 计算错过次数。
- 每次唤醒设置最大补算周期，避免单 Tick 尖峰。
- 会致死的周期效果不能等到实体重新进入 High LOD 才结算。

首版仅实现统一的 `ShouldProcessThisTick(Entity, SystemKind)` 调度接口，不立即为每个系统设计独有 LOD 玩法。

### 9.4 预算与延续

所有新增队列提供：

```text
Capacity
HighWaterMark
Accepted
RejectedInvalid
RejectedCapacity
Deferred
```

只有天然可延迟的自主轮询可以 Deferred。Damage、Effect、Death 和已经开始的 Forced Movement 不允许因预算静默丢失。

## 10. Phase 5F：表现只读边界

本阶段只冻结最小协议，不实现 ISM、Niagara 或骨骼对象池：

```cpp
struct FMassbilityRepresentationSnapshot
{
    FMassEntityHandle Entity;
    FTransform Transform;
    uint32 StateFlags = 0;
    uint16 StateSequence = 0;
    uint8 SimulationLOD = 0;
};
```

规则：

- Snapshot 是值拷贝或批量只读视图，不暴露可写 Fragment。
- 表现系统不能回写 Health、Target、Effect、Forced Movement 或死亡状态。
- 一次性命中/死亡反馈使用有序事件序号，持久状态使用 Snapshot。
- 具体表现模块可独立依赖 `MassRepresentation`、`MassActors`、Niagara 或 Renderer。
- `Massbility` 核心不增加这些依赖。

若当前项目暂不需要 Mass 表现适配，Phase 5F 可以只保留接口文档和测试桩，不创建新模块。

## 11. Processor 顺序

推荐核心顺序：

```text
Massbility.SimulationLOD
  -> Massbility.ForcedMovement
  -> Massbility.SpatialBuild
  -> Massbility.Targeting
  -> Massbility.Ability
  -> External Hit Producers（可选）
  -> Massbility.OutputDispatch
  -> Massbility.GASBridge
  -> Massbility.EffectApply
  -> Massbility.EffectTick
  -> Massbility.Death
  -> External Death Consumers（可选）
  -> Massbility.RepresentationSnapshot（可选）
```

关键约束：

- Forced Movement 先提交最终 Transform，SpatialBuild 再建立本 Tick 查询快照。
- 现有 Ability 和外部命中系统都只提交 Output Request。
- Output Dispatcher 后，同 Tick 可以进入 EffectApply 和 Death。
- Death Consumer 只读取 Event Batch，不反向修改当前 Tick 的 Death Queue。
- 外部 Processor 必须通过显式 `ExecuteBefore/ExecuteAfter` 接入。

## 12. 模块与文件布局

Phase 5 只需要在 `Massbility` 内增加通用目录：

```text
Source/Massbility
├── Public
│   ├── Output
│   │   ├── MassbilityOutputTypes.h
│   │   └── MassbilityOutputSubsystem.h
│   ├── Movement
│   │   ├── MassbilityForcedMovementTypes.h
│   │   ├── MassbilityForcedMovementFragments.h
│   │   └── MassbilityMovementAdapter.h
│   ├── LOD
│   │   └── MassbilitySimulationLODTypes.h
│   └── Representation
│       └── MassbilityRepresentationSnapshot.h
└── Private
    ├── Output
    ├── Movement
    └── LOD
```

不在本阶段创建：

```text
Projectile
Area
Spawn
Progression
Encounter
Reward
Loot
```

独立投射物模块只依赖 `Massbility` 的 Public Output/Spatial API；`Massbility` 不反向依赖该模块。

## 13. 实施顺序

### Phase 5A：Output 接缝

1. 新增 `FMassbilityOutputId` 和只读 Compiled Output Registry。
2. 新增 `FMassbilityOutputRequest` 队列与 Dispatcher。
3. 将现有即时 Ability 迁移到 `OutputId`。
4. 将 Phase 4 External Output Request 迁移到 `OutputId`。
5. 保持现有 Mass/GAS 双向结果不变。

验收：现有即时近战、Mass Target 和 External GAS Proxy 回归一致；外部测试 Producer 可在不知道目标域的情况下提交命中。

### Phase 5B：强制位移

1. 新增 Request、Fragment、仲裁规则和统计。
2. 实现默认 Kinematic Movement Adapter。
3. 实现 Replace/Additive/Priority/Cancel。
4. 实现死亡清理和无碰撞 Adapter 的显式限制。
5. 为 External Proxy 返回明确的 Unsupported 或进入已注册 Adapter。

验收：推、拉、定向位移和到点位移都由同一数据模型表达；Combat/Ability 不直接写 Transform。

### Phase 5C：外部系统集成验证

1. 用测试 Processor 模拟独立 Mass Projectile/Trace 模块。
2. 注册 OutputId，并对普通 Mass Target 提交命中。
3. 对 External GAS Proxy 提交同一 OutputId。
4. 验证来源失效、目标失效、队列容量和 Processor 顺序。
5. 不创建正式 Projectile Entity 或 Projectile Processor。

验收：未来独立投射物系统只依赖稳定公共协议，不要求修改 Massbility Combat/GAS Bridge。

### Phase 5D：Death Event 加固

1. 固定 Event Batch 生命周期。
2. 增加批量只读消费 API。
3. 验证直接伤害、周期效果、外部 Output 三种死亡来源。
4. 验证 Source 失效和 Victim 销毁时序。

验收：死亡事实稳定、只发一次、来源尽可能保留；不存在奖励或掉落耦合。

### Phase 5E：LOD 与预算

1. 增加 Simulation LOD/Observer 最小模型。
2. 让 Targeting、Ability 和 Effect 调度使用统一频率入口。
3. 验证周期效果绝对 Tick 和补算上限。
4. 加入队列容量、高水位和拒绝统计。
5. 对引擎 `MassLOD` 做最小 API 验证。

验收：远距离实体显著减少自主轮询，同时外部命中、位移、死亡和 GAS Bridge 不丢失。

### Phase 5F：可选表现协议

1. 冻结 Snapshot/Event Sequence。
2. 提供测试消费者验证只读和旧事件拒绝。
3. 只有出现实际表现需求时才新建表现模块。

验收：核心不依赖任何具体渲染方案。

## 14. 第一垂直切片

第一刀不制作任何具体技能或投射物玩法，只验证通用边界：

```text
Existing Instant Ability
  -> OutputId
  -> Unified Output Dispatcher
  -> Mass Target / External GAS Proxy

Test External Hit Producer
  -> same OutputId
  -> same Dispatcher

Generic Forced Movement Request
  -> priority arbitration
  -> default Movement Adapter
  -> Transform update before SpatialBuild

Death
  -> existing Death Event batch
  -> read-only test consumer
```

该切片完成后，具体项目可以独立实现投射物、范围查询或玩法能力，而不要求 Massbility 增加相应的专有概念。

## 15. 自动化测试

- Ability 迁移到 OutputId 前后 Damage/Effect 结果一致。
- 同一 OutputId 可由 Ability 和外部 Producer 提交。
- External Proxy 仍生成正确 SemanticTag 的原生 GAS Spec。
- 无效 OutputId、Target、Source 和队列满都有稳定拒绝结果。
- Source 失效但 Policy 允许时可以结算，Death Event Source 为 Invalid。
- 多个强制位移请求按 Priority/Blend 确定性仲裁。
- Displacement、Velocity、ToLocation 三种 Mode 正确到期。
- 死亡会取消活动强制位移。
- 未注册 External Movement Adapter 时不会移动 GAS Avatar 或 Proxy。
- Forced Movement 在 SpatialBuild 前更新位置。
- Death Event 对每个实体只产生一次。
- Death Event Consumer 不依赖 Victim 下一 Tick 仍有效。
- LOD 降频不丢 Output、Forced Movement、Effect Tick 或 Death Event。

## 16. 性能与统计

新增统计：

```text
OutputDefinitionsRegistered
OutputRequestsQueued/Dispatched
OutputRejectedInvalidId/Source/Target/Capacity
OutputRoutedToMass/External
ForcedMovementRequestsQueued/Accepted/Rejected
ForcedMovementActive/Completed/Blocked
ForcedMovementExternalUnsupported
DeathEventsProduced/Consumed
EntitiesBySimulationLOD
Targeting/Ability/EffectTicksSkippedByLOD
QueueHighWaterMarks
```

性能约束：

- 稳定战斗阶段 Registry 不增长。
- 请求队列预留容量，处理后 Reset，不逐请求分配。
- Fragment 不包含 UObject、动态数组或 GameplayTagContainer。
- 外部投射物数量不影响 Massbility Fragment 大小。
- Death Event 不使用逐实体动态 Delegate。
- Forced Movement 每实体最多一个活动状态。
- LOD 判断错峰执行，不能同帧扫描全部实体。

## 17. 固定决策

- Phase 5 是通用框架扩展，不是 Roguelike 玩法实现阶段。
- 只保留死亡事件，不增加奖励、经验或掉落事件。
- 投射物默认属于独立 Mass 系统；Massbility 只提供 Output/Spatial 接缝。
- 强制位移不使用 Charge、Knockback 等玩法枚举。
- Combat、Effect 和 Ability 不直接争写 Transform。
- External GAS Actor 的强制位移必须经过显式 Adapter。
- 不引入完整能力阶段状态机，直到出现明确的通用需求。
- 不把 GameplayAbilities、MassRepresentation、Niagara 或项目模块加入 `Massbility` 核心依赖。

## 18. Phase 5 完成标准

Phase 5 完成意味着：

- Ability 与外部 Mass 系统可以通过同一 OutputId/Request 提交 Damage 或 Effect。
- 普通 Mass Entity 与 External GAS Proxy 继续由同一 Dispatcher 自动分流。
- 独立 Mass 投射物系统无需侵入 Massbility 的 Combat 或 GAS Bridge。
- 强制位移具有通用数据模型、确定性仲裁、可替换 Movement Adapter 和明确的外部目标边界。
- Death Event 生命周期清晰、只发一次、可批量消费，且不包含奖励或掉落职责。
- Targeting、Ability 和 Effect 具备统一 LOD/预算入口，不改变战斗权威语义。
- 表现只能通过只读协议观察模拟状态，核心不依赖具体渲染实现。
- 所有新增热路径保持紧凑、可预算、无逐实体 UObject 和稳定阶段无持续分配。
