# Massbility Phase 4：原生 GAS 双向桥接 C++ 落地方案

## 1. 阶段定位

Phase 4 在已经完成的 Mass 单位、属性、效果、伤害、死亡、能力和空间目标系统之上，增加 Massbility 与 Unreal 原生 Gameplay Ability System 的双向桥接。

本方案覆盖并取代《Massbility 总体方案与实现计划》中 Phase 4 的具体实施口径。本阶段只依赖原生 `GameplayAbilities`、`GameplayTags` 和 `GameplayTasks` 模块，不依赖任何项目层 GAS 框架、项目 AttributeSet、项目队伍系统或项目伤害消息。

阶段目标：

- 普通 Mass 单位能够发现、选择并攻击拥有 `UAbilitySystemComponent` 的 Actor。
- Mass 输出能够转换为真正的 `FGameplayEffectSpec`，通过原生 ASC 施加。
- 原生 GAS Ability 能够使用已有的 `FGameplayEffectSpec` 对 Mass Entity 提交 MassGE 或直接伤害。
- 两个运行时保持单一权威：Mass 单位由 Massbility 管理生命与效果；GAS Actor 由其 ASC 和 GameplayEffect 管理属性与效果。
- `Massbility` 核心模块继续不包含 `GameplayAbilities` 依赖，不在 Mass Fragment 中保存 Actor、ASC、GameplayEffect 或其他 UObject 指针。

本阶段不实现：

- 任意项目专属的队伍判断、Health AttributeSet、伤害事件或死亡消息。
- 对任意 `UGameplayEffect` 的自动反编译或完整语义模拟。
- 为每个 Mass Entity 创建 ASC。
- 客户端预测、Mass Entity 网络稳定 ID 或跨网络 TargetData。
- GameplayCue 到 Mass 表现系统的转换。
- Source ASC 属性捕获到 Mass 属性的自动映射。

### 实现状态（2026-10-13）

Phase 4A、4B 和 4C 的第一轮 C++ 主链已落地，并通过 `PrjInitEditor Win64 Development` 编译：

- Core 已增加 External Target Proxy、带 Serial 的 Handle、SemanticTag、Route 预检和跨域输出队列。
- `Massbility` 核心模块仍不依赖 `GameplayAbilities`。
- `MassbilityGAS` 模块、映射资产类型、Endpoint 同步、World Bridge ASC 和自定义 EffectContext 已实现。
- Mass -> GAS 已使用原生 GameplayEffectSpec、SetByCaller 和 ASC 应用流程。
- GAS -> Mass 已提供直接 Spec API 和 Authority-only Mass TargetData。
- Phase 4D 的内容资产配置、自动化测试、编辑器 Data Validation 和实际场景性能采样仍待完成。

## 2. 已有实现与前置缺口

Phase 3 已经具备以下可复用基础：

- Ability Definition、共享 Loadout、冷却和统一激活校验。
- 2D Spatial Hash、最近敌对目标查询和错峰 Target Refresh。
- Effect Request、Damage Request、周期效果、死亡和击杀来源。
- Runtime Tag Catalog 和固定宽度 Tag Mask。
- 明确的 Processor 顺序和 World Simulation Tick。

当前所有战斗对象仍默认是 Mass Entity：

- `FMassbilityTargetFragment` 只保存 `FMassEntityHandle`。
- Spatial Entry 只索引 `FMassEntityHandle`。
- Effect Request、Damage Context、Active Effect Slot 和 Death Event 的 Source 都是 `FMassEntityHandle`。
- Ability 输出直接调用 Mass Effect/Combat Subsystem。
- Damage 类型 Ability Output 没有稳定的跨系统语义身份，当前直接伤害通常不携带有效 EffectId。
- Runtime Tag Catalog 的现有 `BuildMask` 对未登记 Tag 报错，不适合直接接收 ASC 的完整 Owned Tags。

Phase 4 必须先建立一个与具体外部战斗系统无关的边界，再由 `MassbilityGAS` 使用该边界。禁止在 Ability、Combat 或 Targeting Processor 中直接包含 GAS 头文件或查询 ASC。

## 3. 总体数据流

### 3.1 Mass 攻击 GAS Actor

```text
GAS Actor + ASC
  -> 注册为 External Target
  -> 创建轻量 Mass Proxy Entity
  -> Transform / TeamId / RuntimeTagMask 快照
  -> 进入现有 Spatial Hash
  -> 被 Mass 单位选为 CurrentTarget
  -> Mass Ability 校验成功
  -> FMassbilityExternalOutputRequest
  -> MassbilityGAS Bridge Processor（Game Thread）
  -> Mapping: SemanticTag -> GameplayEffect Class
  -> Bridge ASC 创建 FGameplayEffectSpec
  -> 写入 SetByCaller 和 FMassbilityGameplayEffectContext
  -> SourceASC->ApplyGameplayEffectSpecToTarget(TargetASC)
```

### 3.2 GAS 攻击 Mass Entity

```text
Gameplay Ability / Weapon
  -> 原生 FGameplayEffectSpec
  -> ApplyGameplayEffectSpecToMassTarget
     或 FGameplayAbilityTargetData_MassEntity
  -> Mapping: GameplayEffect Class -> Mass Damage / Mass Effect
  -> FMassbilityDamageRequest / FMassbilityEffectRequest
  -> 现有 EffectApply / EffectTick / Death Processor
  -> Mass Health、Active Effect 和 DeathEvent
```

两条链路都只在边界处接触 UObject/GAS。Mass 热路径继续使用 Entity Handle、紧凑 ID、固定 Fragment 和复用队列。

## 4. 模块边界

### 4.1 `Massbility` 核心模块

继续只依赖：

```text
Core
CoreUObject
Engine
DeveloperSettings
GameplayTags
MassCore
MassEntity
MassCommon
```

新增的通用能力：

- External Target Proxy Entity。
- External Target Handle 和注册表。
- External Output Request Queue。
- Ability Output 的跨系统 Semantic Tag。
- Runtime Tag 的过滤式投影。
- EffectId/AbilityId 的只读元数据查询。

核心模块不知道 External Target 背后是 ASC、Actor、测试桩还是其他系统。

### 4.2 `MassbilityGAS` 模块

新增 Runtime 模块：

```text
MassbilityGAS
  Public dependencies:
    Core
    CoreUObject
    Engine
    GameplayTags
    GameplayAbilities
    Massbility

  Private dependencies:
    GameplayTasks
    MassEntity
```

该模块负责：

- ASC Endpoint 注册与生命周期。
- ASC Transform、TeamId、Owned Tags 到 External Target Proxy 的快照同步。
- Effect 映射资产和静态校验。
- Mass -> GAS Spec 创建与应用。
- GAS -> Mass Spec 翻译与请求提交。
- 自定义 GameplayEffectContext 和 Mass TargetData。
- Bridge 统计、错误计数和非 Shipping 调试信息。

`MassbilityGAS` 可以依赖 `Massbility`；`Massbility` 不允许反向依赖或包含 `MassbilityGAS` 头文件。

## 5. Core 准备：External Target Proxy

### 5.1 选择 Proxy Entity 而不是通用 UObject Target Handle

每个需要被 Mass AI 发现和攻击的 GAS Actor 对应一个轻量 Mass Proxy Entity。该代理不是战斗属性权威，只是目标选择和跨域路由入口。

采用 Proxy Entity 的原因：

- 复用现有 Spatial Hash、TeamId、RuntimeTagMask、距离和 Targetable 校验。
- `FMassbilityTargetFragment` 和 `FMassbilitySpatialEntry` 可以继续保存 `FMassEntityHandle`。
- Mass Ability 的最终距离和 Tag 条件不需要两套实现。
- 不在每个普通单位上增加 Actor/ASC 指针或跨域联合类型。
- GAS Actor 数量通常远小于普通 Mass 单位，代理实体成本可忽略。

### 5.2 External Target Handle

外部目标注册表使用带世代号的句柄，禁止仅使用可复用数组下标：

```cpp
USTRUCT(BlueprintType)
struct FMassbilityExternalTargetHandle
{
    GENERATED_BODY()

    uint32 Index = MAX_uint32;
    uint32 Serial = 0;

    bool IsValid() const;
};
```

`Index + Serial` 防止 Endpoint 注销后旧请求误命中新注册对象。该句柄只标识外部 Endpoint，不保存 UObject。

### 5.3 Proxy Fragment 与 Tag

```cpp
USTRUCT()
struct FMassbilityExternalTargetFragment : public FMassFragment
{
    GENERATED_BODY()

    FMassbilityExternalTargetHandle Handle;
};

USTRUCT()
struct FMassbilityExternalTargetTag : public FMassTag
{
    GENERATED_BODY()
};
```

Proxy Archetype 包含：

```text
FMassbilityUnitFragment
FMassbilityStateFragment
FTransformFragment
FMassbilityExternalTargetFragment
FMassbilityExternalTargetTag
```

Proxy 不包含：

```text
FMassbilityVitalFragment
FMassbilityAttributeFragment
FMassbilityCombatFragment
FMassbilityActiveEffectFragment
FMassbilityAbilityFragment
FMassbilityTargetFragment
```

`UnitDefinitionId` 和 `AttributeSchemaId` 使用 InvalidId；`TeamId`、StateFlags 和 RuntimeTags 使用注册快照。现有 Spatial Build Query 只要求 Unit、State 和 Transform，因此 Proxy 可以直接进入空间索引。

### 5.4 External Target Snapshot

```cpp
struct FMassbilityExternalTargetSnapshot
{
    FTransform Transform;
    FMassbilityTagMask RuntimeTags;
    int16 TeamId = 0;
    bool bTargetable = true;
};
```

核心 External Target Subsystem 提供：

```cpp
bool RegisterExternalTarget(
    const FMassbilityExternalTargetSnapshot& Snapshot,
    FMassbilityExternalTargetHandle& OutHandle,
    FMassEntityHandle& OutProxyEntity);

bool UpdateExternalTarget(
    FMassbilityExternalTargetHandle Handle,
    const FMassbilityExternalTargetSnapshot& Snapshot);

bool UnregisterExternalTarget(FMassbilityExternalTargetHandle Handle);

bool ResolveProxyEntity(
    FMassbilityExternalTargetHandle Handle,
    FMassEntityHandle& OutProxyEntity) const;
```

注册、更新和注销只在 Game Thread 进行。注销时先使 Proxy 失去 Targetable，再安全销毁 Entity 并递增 Serial。

External Target Subsystem 还维护一个通用的“可路由 Semantic Tag”集合。外部适配模块在 World 初始化和 Mapping 编译成功后注册自己能够消费的 SemanticTag，在 Deinitialize 时注销。核心只知道某个 Tag 是否存在有效路由，不知道该路由是否属于 GAS。

```cpp
bool RegisterExternalOutputRoute(FGameplayTag SemanticTag);
bool UnregisterExternalOutputRoute(FGameplayTag SemanticTag);
bool CanRouteExternalOutput(FGameplayTag SemanticTag) const;
```

同一 Tag 的重复注册使用引用计数，避免以后多个外部适配模块互相覆盖。没有任何有效 Route 时，External Output 必须在 Ability 写入冷却前被拒绝。

### 5.5 Team 语义

原生 GAS 没有标准队伍系统。本阶段由注册调用方显式提供 `int16 TeamId`，核心继续使用现有规则：

```text
Source.TeamId == Target.TeamId -> 非敌对
Source.TeamId != Target.TeamId -> 敌对
```

Bridge 不查询任何项目队伍 Subsystem。未来如果需要复杂关系，只在注册层把项目关系转换为 TeamId 或替换独立的 Relation Policy，不修改 GAS 映射逻辑。

## 6. Core 准备：Ability Output 与跨域请求

### 6.1 Semantic Tag

`FMassbilityAbilityOutputDefinition` 增加作者侧字段：

```cpp
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Ability")
FGameplayTag SemanticTag;
```

编译规则：

- Effect Output：`SemanticTag` 为空时使用关联 `UMassbilityEffectDefinition::EffectTag`。
- Damage Output：必须显式填写 `SemanticTag`，例如 `Effect.Damage.Melee`。
- 编译后的 Ability Output 保存 SemanticTag，但实体和请求不复制 GameplayTag。
- 跨域请求保存 `AbilityId + OutputIndex`，Bridge 通过共享编译数据取得 SemanticTag。

SemanticTag 是跨系统映射键，不是用于热路径条件判断的 Runtime Tag。

### 6.2 External Output Request

```cpp
struct FMassbilityExternalOutputRequest
{
    FMassEntityHandle Source;
    FMassbilityExternalTargetHandle Target;
    FMassbilityAbilityId AbilityId;
    uint8 OutputIndex = 0;
    float MagnitudeScale = 1.0f;
    FVector Origin = FVector::ZeroVector;
    FVector Direction = FVector::ZeroVector;
    uint32 SimulationTick = 0;
};
```

执行规则：

1. Ability 按现有逻辑完成 Source、Cooldown、Owner Tag、Target、Team、Target Tag 和 Range 校验。
2. Target 没有 `FMassbilityExternalTargetFragment` 时，继续走现有 Mass Effect/Damage Request。
3. Target 是 External Proxy 时，先确认每个 Output 的 SemanticTag 都有有效 External Route。
4. 所有 Output 都可路由时，为每个 Output 产生 External Output Request。
5. 所有 Output 成功入队后才写入 Ability Cooldown。
6. GAS Bridge 在后续 Game Thread 阶段消费该队列。

Request Queue 预留容量、处理后 Reset。队列满或 Target Handle 已失效时拒绝请求并增加统计，不回退为对 Proxy Entity 修改 Health。

Route 预检保证“缺少 Mapping”属于激活失败，不消耗冷却。Route 已通过预检但在消费前被注销、Target ASC 被销毁等竞态仍可能导致异步应用失败；此时请求只记录失败统计，不尝试回滚已经发生的能力状态。

### 6.3 元数据查询

核心模块补充只读 API：

```cpp
const FMassbilityCompiledAbility* FindAbility(FMassbilityAbilityId AbilityId) const;
const FMassbilityCompiledEffectTemplate* FindEffectTemplate(FMassbilityEffectId EffectId) const;
bool FindEffectIdByTag(FGameplayTag EffectTag, FMassbilityEffectId& OutEffectId) const;
```

Bridge 只能读取共享编译数据，不获得 Registry 容器的可写访问。

## 7. Core 准备：ASC Tag 投影

Ability Definition 的 Required/Blocked Target Tags 必须能够作用于 GAS Actor。Bridge 定期读取 ASC Owned Gameplay Tags，并将其中属于 Runtime Tag Catalog 的部分投影为 Proxy 的 `RuntimeTagMask`。

现有严格 `BuildMask` 保留给资产编译；新增过滤式接口：

```cpp
void BuildMaskFiltered(
    const FGameplayTagContainer& Tags,
    FMassbilityTagMask& OutMask) const;
```

过滤式规则：

- 无效 Tag 忽略并在非 Shipping 构建计数。
- Catalog 未登记 Tag 忽略，不导致整次投影失败。
- 已登记子 Tag 同样展开到 Catalog 中已编译的父 Tag 位。
- 不在每个 Targeting Query 中读取 ASC；只在 Bridge 同步阶段更新快照。

同步频率默认与 `TargetingTicksPerSecond` 一致。ASC 数量较少时可每个 Targeting Tick 全量同步，后续再根据 Tag 变更 Delegate 做脏标记优化。

## 8. GAS Endpoint 注册与生命周期

### 8.1 注册 API

`UMassbilityGASSubsystem` 提供：

```cpp
FMassbilityExternalTargetHandle RegisterAbilitySystemTarget(
    UAbilitySystemComponent* AbilitySystem,
    int16 TeamId,
    bool bTargetable = true);

bool UpdateAbilitySystemTargetTeam(
    FMassbilityExternalTargetHandle Handle,
    int16 TeamId);

bool SetAbilitySystemTargetable(
    FMassbilityExternalTargetHandle Handle,
    bool bTargetable);

bool UnregisterAbilitySystemTarget(
    FMassbilityExternalTargetHandle Handle);
```

注册校验：

- ASC 非空且拥有有效 World。
- ASC 与 Bridge Subsystem 属于同一个 World。
- 同一个 ASC 不能重复注册；重复注册返回已有 Handle 并更新快照。
- Dedicated Server、Listen Server 和 Standalone 支持注册；客户端只读实例不执行伤害桥接。

### 8.2 GAS 侧记录

```cpp
struct FMassbilityGASEndpoint
{
    FMassbilityExternalTargetHandle Handle;
    FMassEntityHandle ProxyEntity;
    TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
    TWeakObjectPtr<AActor> AvatarActor;
    int16 TeamId = 0;
    bool bTargetable = true;
};
```

该记录保存在 `MassbilityGAS` Subsystem，不进入 Fragment。

同步时：

1. 从 ASC ActorInfo 获取 Avatar Actor。
2. 读取 Avatar Transform。
3. 调用 `ASC->GetOwnedGameplayTags`。
4. 使用 `BuildMaskFiltered` 生成 RuntimeTagMask。
5. 更新核心 External Target Snapshot。
6. ASC 或 Avatar 已销毁时自动注销。

Bridge 不根据任何 Health 属性猜测死亡。调用方或 GAS Ability 负责在不应继续被攻击时调用 `SetAbilitySystemTargetable(false)`，也可以通过映射到 Runtime Tag 的 Dead/Untargetable Tag 阻止能力选择。

## 9. Effect Mapping 资产

### 9.1 不自动解释任意 GameplayEffect

`UGameplayEffect` 可能包含 Modifier、SetByCaller、MMC、Execution、条件效果、Source/Target Capture、Stack、Granted Tag 和项目自定义逻辑。MassGE 只实现其中受控子集，因此禁止尝试把任意 GameplayEffect 自动编译成 MassGE。

两侧转换必须经过显式 Mapping Entry。

### 9.2 Mass -> GAS 映射

```cpp
USTRUCT(BlueprintType)
struct FMassbilityGASOutboundEffectMapping
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly)
    FGameplayTag SemanticTag;

    UPROPERTY(EditDefaultsOnly)
    TSubclassOf<UGameplayEffect> GameplayEffectClass;

    UPROPERTY(EditDefaultsOnly)
    FGameplayTag MagnitudeSetByCallerTag;

    UPROPERTY(EditDefaultsOnly, meta = (ClampMin = "0.0"))
    float GameplayEffectLevel = 1.0f;
};
```

Magnitude 规则：

- Damage Output：`FinalMagnitude = Damage * Request.MagnitudeScale`。
- Effect Output：`FinalMagnitude = Request.MagnitudeScale`。
- `MagnitudeSetByCallerTag` 有效时写入 `Spec.SetSetByCallerMagnitude`。
- Mapping 不指定 SetByCaller Tag 时，GE 必须能够仅依赖自身配置生效。
- SemanticTag 同时通过 `Spec.AddDynamicAssetTag` 加入 Spec，便于原生 GAS 条件和调试读取。

### 9.3 GAS -> Mass 映射

```cpp
UENUM(BlueprintType)
enum class EMassbilityGASInboundPolicy : uint8
{
    Damage,
    Effect,
};

USTRUCT(BlueprintType)
struct FMassbilityGASInboundEffectMapping
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly)
    TSubclassOf<UGameplayEffect> GameplayEffectClass;

    UPROPERTY(EditDefaultsOnly)
    EMassbilityGASInboundPolicy Policy = EMassbilityGASInboundPolicy::Damage;

    UPROPERTY(EditDefaultsOnly)
    TObjectPtr<UMassbilityEffectDefinition> MassEffect;

    UPROPERTY(EditDefaultsOnly)
    FGameplayTag MagnitudeSetByCallerTag;

    UPROPERTY(EditDefaultsOnly)
    float DefaultMagnitude = 1.0f;
};
```

转换规则：

- Mapping 主键是 `Spec.Def->GetClass()`，不依赖实例指针。
- Damage Policy：读取 Magnitude，要求大于零，提交 Damage Request。
- Effect Policy：注册/取得 MassEffect 的 EffectId，以 Magnitude 作为 `MagnitudeScale` 提交 Effect Request。
- Mapping 中的 MassEffect 只在注册或预热阶段编译，不在命中瞬间首次加载软资产。
- 不遍历 `Spec.Modifiers`，不运行 GAS Execution 后再逆向推导伤害。

### 9.4 Mapping 资产

```cpp
UCLASS(BlueprintType)
class UMassbilityGASEffectMapping : public UPrimaryDataAsset
{
    GENERATED_BODY()

    TArray<FMassbilityGASOutboundEffectMapping> OutboundMappings;
    TArray<FMassbilityGASInboundEffectMapping> InboundMappings;
};
```

World 初始化时编译为：

```text
SemanticTag -> Outbound Compiled Mapping
GameplayEffect Class -> Inbound Compiled Mapping
```

运行时命中不遍历资产数组。映射必须在战斗开始前预热。每个成功编译的 Outbound SemanticTag 同时注册到 Core External Output Route 集合；Mapping 编译失败时不得注册部分 Route。

### 9.5 静态校验

初始化时拒绝：

- 重复的 Outbound SemanticTag。
- 重复的 Inbound GameplayEffect Class。
- 无效 SemanticTag 或空 GameplayEffect Class。
- Effect Policy 未填写 MassEffect。
- Damage Policy 填写负数 DefaultMagnitude。
- 填写了无效 SetByCaller Tag。
- MassEffect 注册或 Schema 预热失败。

对于依赖 Source Attribute Capture 或 AggregateBySource Stacking 的 Outbound GE，首版给出明确 Warning。Bridge 使用共享 Source ASC，无法把每个 Mass Entity 表现为不同 ASC 来源。

## 10. Mass -> GAS 原生应用

### 10.1 World 级 Bridge ASC

每个 Authority World 创建一个瞬态、非复制的 Bridge Actor：

```text
AMassbilityGASBridgeActor
  -> UAbilitySystemComponent
```

Bridge Actor：

- `RF_Transient`，不保存到关卡。
- 不参与碰撞、移动、表现或网络复制。
- 初始化 ASC ActorInfo，Owner 和 Avatar 均指向 Bridge Actor。
- World Deinitialize 时销毁。

所有 Mass -> GAS 的 Spec 都由该 ASC 创建和施加。只创建一个世界级 ASC，不为 Mass 单位创建 ASC。

### 10.2 GameplayEffectContext

```cpp
USTRUCT()
struct FMassbilityGameplayEffectContext : public FGameplayEffectContext
{
    GENERATED_BODY()

    FMassEntityHandle SourceMassEntity;
    FGameplayTag SemanticTag;
    float RawMagnitude = 0.0f;
    FVector_NetQuantize10 Origin;
    FVector_NetQuantizeNormal Direction;
    uint32 SimulationTick = 0;

    virtual UScriptStruct* GetScriptStruct() const override;
    virtual FMassbilityGameplayEffectContext* Duplicate() const override;
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};
```

Context 由 Bridge 显式 `new` 并包装进 `FGameplayEffectContextHandle`，不要求项目替换全局 `UAbilitySystemGlobals`。

网络规则：

- `SourceMassEntity` 只在 Authority World 本地有效，不进行网络序列化。
- SemanticTag、RawMagnitude、Origin、Direction 可以正常序列化给客户端用于表现和调试。
- 客户端读取不到有效 SourceMassEntity 是预期行为。
- 未来需要跨网络识别来源时增加稳定的 Mass Net Entity ID，禁止序列化原始 `FMassEntityHandle`。

### 10.3 Spec 创建和应用

每个 External Output Request 执行：

1. 校验 Target Handle 的 Serial。
2. 取得 Target ASC。
3. 通过 AbilityId/OutputIndex 读取 Compiled Output 和 SemanticTag。
4. 查找 Outbound Mapping。
5. 创建 `FMassbilityGameplayEffectContext`。
6. Bridge ASC 调用 `MakeOutgoingSpec`。
7. 写入 Dynamic Asset Tag 和可选 SetByCaller。
8. Bridge ASC 调用 `ApplyGameplayEffectSpecToTarget`。
9. 记录成功、无映射、无效目标和 Spec 创建失败统计。

所有 ASC 操作必须在 Game Thread 执行。Bridge Processor 设置 `bRequiresGameThreadExecution = true`，并在 `Massbility.Ability` 之后运行。

### 10.4 共享 Source ASC 的限制

首版 Outbound GE 必须遵守：

- 伤害或效果强度由 SetByCaller 提供。
- 可以捕获 Target ASC 属性。
- 不依赖 Bridge ASC 上不存在的攻击力等 Source Attribute。
- 不假设每个 Mass 来源拥有不同 Source ASC。
- AggregateBySource Stacking 会把所有 Mass 来源视为同一个 Bridge ASC，首版应使用 AggregateByTarget、Context 自定义逻辑或避免该叠层策略。

如果以后确实需要 GAS Source Capture，可按 Unit Definition 或战斗阵营建立少量 Source ASC Pool；不得退化为每实体 ASC。

## 11. GAS -> Mass 原生应用

### 11.1 直接 API

先实现明确且可返回结果的 Authority API：

```cpp
bool ApplyGameplayEffectSpecToMassTarget(
    const FGameplayEffectSpec& Spec,
    FMassEntityHandle Target,
    const FHitResult* OptionalHitResult = nullptr);
```

处理步骤：

1. 从 `Spec.Def` 查找 Inbound Mapping。
2. 校验目标 Entity、Targetable 和非死亡状态。
3. 从 Spec Context 的 Instigator ASC 查找已注册 Endpoint。
4. 已注册时使用该 Endpoint 的 Proxy Entity 作为 Mass Source；未注册时 Source 为空。
5. 读取 SetByCaller Magnitude；不存在时使用 DefaultMagnitude。
6. Damage Policy 提交 Damage Request。
7. Effect Policy 提交 Effect Request。
8. Origin、Direction 和 HitLocation 从 HitResult 或 Spec Context 填充。

该 API 只排队，不在 GAS Ability 调用栈中同步写 Entity Fragment。

### 11.2 Mass TargetData

在直接 API 验证稳定后，增加原生 TargetData 包装：

```cpp
USTRUCT()
struct FGameplayAbilityTargetData_MassEntity : public FGameplayAbilityTargetData
{
    GENERATED_BODY()

    FMassEntityHandle TargetEntity;
    FHitResult HitResult;
    bool bHasHitResult = false;

    virtual UScriptStruct* GetScriptStruct() const override;
    virtual bool HasHitResult() const override;
    virtual const FHitResult* GetHitResult() const override;
    virtual TArray<FActiveGameplayEffectHandle> ApplyGameplayEffectSpec(
        FGameplayEffectSpec& Spec,
        FPredictionKey PredictionKey) override;
};
```

`ApplyGameplayEffectSpec` 调用 Bridge 的直接 API，使原生 Gameplay Ability 可以继续使用 TargetData/Spec 形态。

注意：返回的 `TArray<FActiveGameplayEffectHandle>` 对 Mass 目标为空。Mass Active Effect 不是 GAS Active Gameplay Effect，不能伪造 `FActiveGameplayEffectHandle`。需要明确成功结果的调用方使用直接 API 或 Bridge 统计。

### 11.3 Authority 与网络限制

Phase 4 TargetData 只允许在 Authority 或 Standalone 本地使用：

- 不序列化原始 `FMassEntityHandle`。
- 不允许客户端把本地 Entity Handle 当成服务器目标。
- 预测 Ability 必须在服务器重新查询/解析 Mass 目标后再提交。
- 若 TargetData 进入网络序列化路径，开发构建应明确报错并拒绝应用，不能静默命中错误实体。

后续网络阶段需要先提供稳定的 `FMassbilityNetEntityId`、服务器解析表和命中复核，再开放 TargetData NetSerialize。

## 12. Source 与击杀归属

GAS -> Mass 时，如果 Source ASC 已注册为 Endpoint，则使用它的 Proxy Entity 作为：

```text
FMassbilityEffectRequest.Source
FMassbilityActiveEffectSlot.Source
FMassbilityDamageContext.Source
FMassbilityCombatFragment.LastDamageSource
FMassbilityDeathEvent.Source
```

这样即时伤害和周期效果都能保留同一个来源，并复用现有死亡链路。

消费 DeathEvent 的上层可以把 Source Proxy Entity 交回 `UMassbilityGASSubsystem`，解析为 External Target Handle 或 ASC。核心 Massbility 仍然只看到 Entity Handle。

生命周期约束：

- 正常玩家/角色 Endpoint 应在其 ASC 生命周期内保持注册。
- 注销后不保证旧周期效果还能解析回 ASC，但其 Source Entity Handle 不会错误指向新 Endpoint，因为 Proxy Entity 和 External Handle 都带 Serial。
- 本阶段不持有强引用来延长 Actor 或 ASC 生命周期。

## 13. Processor 顺序与线程约束

建议顺序：

```text
Massbility.GASProxySync      同步少量 ASC Proxy Transform/Tags
    -> Massbility.SpatialBuild
    -> Massbility.Targeting
    -> Massbility.Ability
    -> Massbility.GASBridge  消费 Mass -> GAS 外部输出
    -> Massbility.EffectApply
    -> Massbility.EffectTick
    -> Massbility.Death
```

说明：

- `GASProxySync` 和 `GASBridge` 都要求 Game Thread。
- Mass Ability 只入 External Queue，不调用 ASC。
- GAS -> Mass 请求可能在正常 Actor Tick、Ability 或输入阶段产生，由下一次 `EffectApply` 安全消费。
- 外部请求的同帧应用由显式 `ExecuteAfter` 保证，不依赖 Processor 注册顺序。
- Bridge 模块未加载时，核心 External Queue 不应产生请求；External Target 也不会被注册。

## 14. C++ 文件布局

### 14.1 `Massbility` 新增

```text
Source/Massbility
├── Public
│   └── External
│       ├── MassbilityExternalTargetTypes.h
│       ├── MassbilityExternalTargetFragments.h
│       └── MassbilityExternalTargetSubsystem.h
└── Private
    └── External
        └── MassbilityExternalTargetSubsystem.cpp
```

修改：

```text
Ability/MassbilityAbilityTypes.h
    增加 Output SemanticTag 与 External Output Request

Ability/MassbilityAbilityDefinition.cpp
    编译和校验 SemanticTag

Ability/MassbilityAbilitySubsystem.h/.cpp
    目标为 External Proxy 时改为跨域排队

Tags/MassbilityRuntimeTagCatalog.h/.cpp
    增加 BuildMaskFiltered

Effect/MassbilityEffectSubsystem.h
    增加 Effect Template/Tag 只读查询

Core/MassbilitySubsystem.h/.cpp
    创建 External Proxy Archetype 的内部支持
```

### 14.2 `MassbilityGAS` 新增

```text
Source/MassbilityGAS
├── MassbilityGAS.Build.cs
├── Public
│   ├── MassbilityGAS.h
│   ├── Core
│   │   ├── MassbilityGASSettings.h
│   │   └── MassbilityGASSubsystem.h
│   ├── Effect
│   │   └── MassbilityGASEffectMapping.h
│   ├── Context
│   │   └── MassbilityGameplayEffectContext.h
│   └── Targeting
│       └── MassbilityGameplayAbilityTargetData.h
└── Private
    ├── MassbilityGAS.cpp
    ├── Core
    │   ├── MassbilityGASSettings.cpp
    │   └── MassbilityGASSubsystem.cpp
    ├── Effect
    │   └── MassbilityGASEffectMapping.cpp
    ├── Context
    │   └── MassbilityGameplayEffectContext.cpp
    ├── Targeting
    │   └── MassbilityGameplayAbilityTargetData.cpp
    └── Processing
        └── MassbilityGASBridgeProcessor.cpp
```

Bridge Actor 类可保持在 Private，因为项目层不需要继承或直接访问它。

插件描述文件增加：

```text
GameplayAbilities Plugin dependency
MassbilityGAS Runtime module
```

## 15. 实施顺序

### Phase 4A：Core 跨域接缝

1. 增加 External Target Handle、Fragment、Tag 和 Registry。
2. 创建/更新/注销轻量 Proxy Entity。
3. 验证 Proxy 自动进入 Spatial Hash，并可被最近敌人查询选中。
4. 为 Ability Output 增加 SemanticTag。
5. 增加 External Output Route 注册和预检。
6. 增加 External Output Request Queue。
7. Ability 对 Proxy Target 只产生跨域请求，不修改 Proxy Health。
8. 增加 BuildMaskFiltered 和只读 Effect 元数据查询。

这一轮不加入 GameplayAbilities 依赖，完成后 Massbility 模块应单独编译。

### Phase 4B：Mass -> GAS

1. 新建 `MassbilityGAS` 模块和 Mapping 资产。
2. 实现 ASC Endpoint 注册及 Proxy 同步。
3. 实现 World Bridge Actor + ASC。
4. 实现 `FMassbilityGameplayEffectContext`。
5. 实现 Outbound Mapping 编译、SetByCaller 和 GE 应用。
6. 增加 Bridge Processor、执行顺序和统计。
7. 使用最小原生测试 ASC 验证 GE 施加，不接项目 AttributeSet。

### Phase 4C：GAS -> Mass

1. 实现 Inbound Mapping 编译。
2. 实现直接 `ApplyGameplayEffectSpecToMassTarget` API。
3. 验证 Damage 和 Effect 两种 Policy。
4. 验证 Source Proxy、周期效果和 DeathEvent 归属。
5. 实现 Authority-only Mass TargetData。
6. 验证原生 Gameplay Ability 的 Spec/TargetData 调用形态。

### Phase 4D：校验与加固

1. Mapping Data Validation。
2. Endpoint/Proxy 生命周期和 World Cleanup。
3. 无效 Handle、缺少 Mapping、空 ASC 和销毁中 Actor 的容错。
4. 稳定运行后的内存分配检查。
5. 自动化测试和性能计数。
6. 更新总体架构文档中 Phase 4 的验收描述。

每轮完成后都必须保持 Editor Target 可编译，并能单独验收。

## 16. 功能验收

### External Target

- 注册 ASC 后创建且只创建一个 Proxy Entity。
- Proxy Transform 能按同步 Tick 更新。
- ASC Owned Tags 只投影 Catalog 中登记的 Tag。
- Mass 单位可按 Team、Range、Required/Blocked Tag 选择 Proxy。
- 注销 ASC 后 Proxy 不再被选中，旧 External Handle 失效。

### Mass -> GAS

- Mass 近战能力命中 Proxy 时不会尝试读取 Mass Vital Fragment。
- SemanticTag 能找到正确 GameplayEffect Class。
- Damage Output 的最终数值正确写入 SetByCaller。
- Effect Output 的 MagnitudeScale 正确写入 SetByCaller。
- Target ASC 收到真实 GameplayEffectSpec。
- 原生 ASC GameplayEffect Applied Delegate 正常触发。
- Mapping 缺失时在 External Route 预检阶段拒绝激活，不写入冷却。
- Route 预检后发生的 Target 销毁等竞态不会崩溃，并有明确统计/日志策略。
- 一个世界只有一个 Bridge ASC，不随 Mass 单位数量增长。

### GAS -> Mass

- 已映射 Damage GE 能降低 Mass Health。
- 已映射 Effect GE 能施加、刷新、叠层和到期。
- 未映射 GE 被明确拒绝，不尝试自动解析。
- SetByCaller 缺失时使用 Mapping DefaultMagnitude。
- Source ASC 已注册时，DeathEvent Source 是对应 Proxy Entity。
- 周期效果致死时仍保留同一 Source。
- TargetData 路径和直接 API 产生相同结果。

### 权威与隔离

- GAS Actor 的属性和死亡不由 Massbility判断。
- Mass Entity 的 Health 和 Active Effect 不由 ASC 保存。
- `Massbility.Build.cs` 不增加 GameplayAbilities 依赖。
- Mass Fragment 不包含 UObject、ASC、GameplayEffectSpec、GameplayTagContainer 或动态数组。
- 客户端不能提交未经服务器解析的原始 Mass Entity Handle。

## 17. 性能与统计

新增非 Shipping 统计：

```text
GASEndpointsRegistered
GASProxyUpdates
GASOwnedTagsProjected
ExternalOutputRequestsQueued
ExternalOutputRequestsApplied
ExternalOutputRejectedInvalidTarget
ExternalOutputRejectedNoMapping
InboundSpecsReceived
InboundSpecsAppliedAsDamage
InboundSpecsAppliedAsEffect
InboundSpecsRejectedNoMapping
BridgeSpecsCreated
BridgeSpecApplyFailures
```

性能约束：

- Proxy 同步成本与 GAS Endpoint 数量相关，不与 Mass Entity 数量相乘。
- Mass Targeting 仍只查询统一 Spatial Grid。
- 不在每个候选检查中访问 ASC。
- 不在 Mass Processor 中创建 GameplayEffectSpec。
- GameplayEffectSpec 只为实际跨域命中创建。
- Mapping Registry 在 World 初始化时构建，命中时 O(1) 查询。
- 稳定战斗阶段 External Request 和 Endpoint 数组不持续扩容。

## 18. 第一垂直切片

首个可运行闭环只需要：

```text
一个 Mass Zombie
一个拥有原生 ASC 的测试 Actor
一个 Mass Melee Ability
一个 Outbound Damage GameplayEffect
一个 GAS Weapon Spec
一个 Inbound Damage Mapping
```

流程：

1. 测试 Actor 注册 Endpoint，生成 Proxy。
2. Zombie 从 Spatial Grid 选中 Proxy。
3. Zombie Melee 产生 External Output Request。
4. Bridge ASC 创建 GE Spec，并把伤害写入 SetByCaller。
5. Target ASC 成功应用 GE；Bridge 不读取任何目标 Health 属性。
6. 测试 Actor 创建另一个原生 GE Spec，通过直接 API 命中 Zombie。
7. Inbound Mapping 转成 Mass Damage Request。
8. Zombie Health 归零并产生 DeathEvent，Source 指向测试 Actor Proxy。

该切片验证双向原生 GAS 边界，不需要项目队伍、项目 HealthSet 或项目伤害事件。完成后再扩展持续效果、TargetData、Tag 条件和多目标命中。

## 19. Phase 4 完成标准

Phase 4 的完成标准不是“能够从 ASC 扣一次数值”，而是：

- Mass 与 GAS 两种权威对象可以互相成为合法战斗目标。
- 两个方向都通过显式映射和队列完成，不相互侵入运行时数据模型。
- Mass -> GAS 使用真正的 GameplayEffectSpec、SetByCaller、EffectContext 和 ASC 应用流程。
- GAS -> Mass 保留 GameplayEffectSpec/TargetData 的调用形态，但由 MassGE/Combat 负责最终结算。
- Source、Target、Effect 语义和死亡归属可以跨边界追踪。
- 核心模块保持无 GameplayAbilities 依赖，普通实体保持无 ASC/UObject 热路径。
- 对 Source Capture、Stacking、TargetData 返回值和网络能力的限制有明确、可测试的边界。
