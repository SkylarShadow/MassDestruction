# Massbility Phase 3：战斗闭环、能力与目标系统 C++ 方案

## 1. 阶段目标

Phase 3 在 Phase 2 MassGE 骨架上建立普通 Mass 单位的首个完整战斗闭环：统一模拟时钟、伤害与死亡结算、共享 Ability Definition、实体冷却、显式目标激活、空间索引、自动选敌和即时近战攻击。

本阶段第一轮不实现 Ability 前摇/Active/Recovery 状态机。即时能力在激活成功后直接产生 EffectRequest 并写入冷却；只有后续确实需要攻击窗口、蓄力、持续施法或位移过程时，才增加 Active Ability 实例。

本阶段不实现 Skyra GAS Bridge、Actor/ASC 目标、投射物、GameplayCue、动画驱动攻击窗口、技能消耗、网络预测、复杂 Execution Kernel 或 Blueprint 热路径。Phase 3 的 Target 先限定为 Mass Entity；Actor 与 GAS 目标在 Phase 4 通过 Bridge 接入。

### 实现状态（2026-10-13）

Phase 3A、3B、3C 的 C++ 代码已落地并通过 `PrjInitEditor Win64 Development` 编译：

- Effect Apply、Effect Tick、Combat、Death 已形成有序处理链，支持 Instant、Duration、Infinite 和主动移除。
- Ability Definition 会注册并编译为共享 Ability/Loadout；实体只保存 LoadoutId 和四个冷却截止 Tick。
- 显式 Ability Request 与 20Hz 自动激活共用同一套校验和输出路径。
- 标准 `FTransformFragment`、错峰 Target Fragment、10Hz 二维空间哈希和按 Ability Tag 条件过滤的最近敌人查询已接通。
- Ability 与 Spatial Subsystem 提供运行时统计；规模性能数据仍需在编辑器或目标机器上按本文第 16 节采样。

## 2. Phase 2 前置收口

Phase 2 当前已完成 Effect Definition、Schema Variant、请求队列、固定效果槽、周期/持续/叠层、Granted Tag 和统一属性访问，但总体方案中原归入 Phase 2 的 DamageRequest、死亡/击杀、主动移除仍未落地。

Phase 3 编码前先完成以下基础接口，否则 Ability 只能修改 Health，无法形成可靠的攻击闭环：

1. 将 Effect 请求施加与 Active Effect 周期更新拆成两个 Processor。
2. 请求施加每帧执行；持续效果更新使用世界初始化时固定的可配置频率。
3. 增加伤害上下文、最后伤害来源和一次性死亡状态迁移。
4. 增加主动移除 Effect 的内部能力，为死亡清理和未来 Infinite Effect 留出接口。
5. 明确 Processor 顺序：请求施加、周期效果、死亡判定、目标与能力。

Infinite Duration、明确的 Duration Policy 和主动移除接口已经实现。

## 3. 统一模拟时钟

Effect Duration、Effect Period、Ability Cooldown 和 Target Refresh 必须使用同一套 World Simulation Tick，避免各子系统分别从 `World->GetTimeSeconds()` 推导并采用不同频率。

### 配置

在 `UMassbilitySettings` 增加只在 World 初始化时读取的配置：

```text
SimulationTicksPerSecond      默认 20
TargetingTicksPerSecond       默认 10
SpatialCellSize               默认根据单位尺寸设为 300~500cm
MaxAbilitiesPerUnit           编译期固定，第一版 4
```

`SimulationTicksPerSecond` 在 World 运行期间不可热修改。修改后需要重新创建 World，因为已经编译的 DurationTick、PeriodTick、CooldownTick 和现存绝对 Tick 无法直接沿用。

### 文件职责

```text
Public/Core/MassbilitySimulationClock.h
Private/Core/MassbilitySimulationClock.cpp
```

`FMassbilitySimulationClock` 是 `UMassbilitySubsystem` 持有的轻量值对象，提供：

- `SecondsToTicksCeil`：资产秒数转 Tick。
- `GetCurrentTick`：统一获取当前绝对 Tick。
- `HasReached`：处理 `uint32` 回绕的到期比较。
- `GetTicksPerSecond`：供 Effect、Ability 与 Targeting 编译使用。

它不为实体创建 Timer。

## 4. Combat 与死亡闭环

### 4.1 数据结构

```text
Public/Combat/MassbilityCombatTypes.h
Public/Combat/MassbilityCombatFragments.h
```

新增：

```cpp
struct FMassbilityDamageContext
{
    FMassEntityHandle Source;
    FMassbilityEffectId EffectId;
    float RawMagnitude;
    FVector_NetQuantize10 Origin;
    FVector_NetQuantizeNormal Direction;
};
```

第一版只在请求/事件队列中携带完整 Context，不把 FVector Context 常驻每个实体。实体只保留最小击杀归属数据：

```cpp
struct FMassbilityCombatFragment : public FMassFragment
{
    FMassEntityHandle LastDamageSource;
    FMassbilityEffectId LastDamageEffect;
    uint32 LastDamageTick;
};
```

### 4.2 Damage Request

`FMassbilityDamageRequest` 表示明确的战斗意图：

- Source、Target。
- Damage EffectId 或直接 Magnitude。
- Origin、Direction。
- 可选 Damage Type/Runtime Tag Mask，第一轮可预留不参与复杂公式。

Ability 最终只产生 DamageRequest 或 EffectRequest，不直接写目标 Health。

### 4.3 死亡处理

```text
Public/Combat/MassbilityDeathProcessor.h
Private/Combat/MassbilityDeathProcessor.cpp
```

死亡 Processor 在伤害和周期效果之后运行：

1. `Health <= 0` 且没有 Dying/Dead 时设置 Dying。
2. 生成一次 `FMassbilityDeathEvent`，包含 Victim、Source、EffectId、Tick。
3. 清理 Targetable、Ability 激活资格和需要随死亡移除的效果。
4. 保留一个模拟 Tick 的 Dying 状态，并在下一模拟 Tick 进入 Dead；表现等待时间后续由 Representation 系统扩展。

死亡事件进入世界级固定复用队列，由游戏层在 Mass 处理完成后读取。核心热路径不广播逐实体 Dynamic Delegate。

## 5. Ability 作者数据与编译数据

### 5.1 文件

```text
Public/Ability/MassbilityAbilityTypes.h
Public/Ability/MassbilityAbilityDefinition.h
Private/Ability/MassbilityAbilityDefinition.cpp
```

### 5.2 `UMassbilityAbilityDefinition`

第一版字段：

- `AbilityTag`：能力的作者身份，例如 `Ability.Zombie.Melee`。
- `CooldownSeconds`：冷却时间。
- `RequiredOwnerTags`：激活所需自身 Tag。
- `BlockedOwnerTags`：阻止激活的自身 Tag。
- `RequiredTargetTags`：目标必须拥有的 Tag。
- `BlockedTargetTags`：目标不能拥有的 Tag。
- `TargetPolicy`：Self、CurrentTarget 或 ExplicitTarget。
- `Range`：能力自己的最终距离校验。
- `bUse2DDistance`：地面丧尸默认使用 XY 距离。
- `Outputs`：成功激活后产生的 Effect/Damage 输出。

第一版不把“寻找最近敌人”写进 Ability Definition。Targeting 系统负责提供 CurrentTarget，Ability 只负责验证并执行，避免每个 Ability 独立进行空间查询。

### 5.3 编译结构

```cpp
struct FMassbilityAbilityId
{
    uint16 Value;
};

struct FMassbilityCompiledAbility
{
    FMassbilityAbilityId AbilityId;
    uint32 CooldownTicks;
    FMassbilityTagMask RequiredOwnerTags;
    FMassbilityTagMask BlockedOwnerTags;
    FMassbilityTagMask RequiredTargetTags;
    FMassbilityTagMask BlockedTargetTags;
    float RangeSquared;
    uint8 OutputCount;
    TStaticArray<FMassbilityCompiledAbilityOutput, 4> Outputs;
};
```

Output 在编译时将 Effect Definition 注册为 EffectId。运行时激活不访问 Ability UObject、Effect UObject 或 GameplayTagContainer。

## 6. Ability Loadout 与实体数据

`UMassbilityUnitDefinition` 增加作者侧 `Abilities` 数组。注册 Unit Definition 时将它编译为共享 Loadout：

```cpp
struct FMassbilityCompiledAbilityLoadout
{
    uint8 AbilityCount;
    TStaticArray<FMassbilityAbilityId, 4> AbilityIds;
};
```

实体不重复保存 AbilityId 数组；`UnitDefinitionId` 可找到共享 Loadout。实体只保存每个 Loadout Slot 的可变冷却：

```cpp
struct FMassbilityAbilityFragment : public FMassFragment
{
    uint16 AbilityLoadoutId;
    TStaticArray<uint32, 4> NextReadyTicks;
};
```

这样 4 个能力只需约 16 字节实体状态。第一版没有 Active Ability 状态、阶段索引或每实体 UObject。

当未来加入有前摇的能力时，再单独增加：

```text
ActiveAbilitySlot
PhaseIndex
PhaseEndTick
LockedTarget
```

即时能力不承担这些字段。

## 7. Ability 请求与执行

### 7.1 请求类型

```cpp
struct FMassbilityAbilityRequest
{
    FMassEntityHandle Source;
    FMassEntityHandle ExplicitTarget;
    uint8 AbilitySlot;
    float MagnitudeScale;
};
```

请求只表示“尝试激活”。进入队列成功不等于最终激活成功。

### 7.2 激活校验顺序

`TryActivate` 依次检查：

1. Source Entity 和 Ability Slot 有效。
2. Source 未死亡，且没有 `AbilityBlocked`。
3. 当前 Tick 已达到 `NextReadyTick`。
4. Owner Required/Blocked TagMask。
5. 根据 TargetPolicy 解析目标。
6. Target 有效、Targetable、阵营敌对。
7. Target Required/Blocked TagMask。
8. 使用 Transform 做最终距离平方检查。
9. 为每个 Output 产生 EffectRequest 或 DamageRequest。
10. 写入 `NextReadyTick = CurrentTick + CooldownTicks`。

只有所有必需校验通过后才进入冷却。

### 7.3 文件职责

```text
Public/Ability/MassbilityAbilitySubsystem.h
Private/Ability/MassbilityAbilitySubsystem.cpp
```

Subsystem 负责：

- Ability Definition 注册和预热。
- Unit Ability Loadout 编译。
- 显式 Ability Request 队列。
- 激活失败原因计数。
- 产生 Effect/Damage 请求。

它不逐实体 Tick 冷却；冷却只通过绝对 `NextReadyTick` 比较。

## 8. Transform 与 Target Fragment

Phase 1 的基础 Archetype 尚未包含位置 Fragment。Phase 3 必须引入标准 `FTransformFragment`，并为创建接口增加初始 Transform 参数；保留原有创建重载时默认使用 Identity Transform。

目标状态：

```cpp
struct FMassbilityTargetFragment : public FMassFragment
{
    FMassEntityHandle TargetEntity;
    FVector LastKnownLocation;
    uint32 NextRefreshTick;
};
```

Target Fragment 保存当前决策结果，不保存候选列表，也不保存 UObject。

当目标失效、死亡、不可选、超出丢失距离或阵营不再敌对时清空。

## 9. Mass 空间索引

### 9.1 选择

第一版使用针对地面单位的 2D Uniform Hash Grid。它比逐实体遍历所有目标更适合大量丧尸，也比给每个单位做 Overlap/Trace 更可控。

不采用 `TMap<Cell, TArray<Entity>>` 作为每帧热数据，因为会产生大量小数组和分配。建议使用预留容量的扁平数组与开放寻址 Bucket：

```cpp
struct FMassbilitySpatialEntry
{
    FMassEntityHandle Entity;
    FVector Location;
    int16 TeamId;
    int32 NextEntry;
};

struct FMassbilitySpatialBucket
{
    int32 CellX;
    int32 CellY;
    int32 FirstEntry;
    uint32 Generation;
};
```

每次重建：

1. Reset Entry 数量但保留容量。
2. 增加 Generation，不必清空整个 Bucket 数组。
3. 将 Targetable 实体插入对应 Cell。
4. 每个 Entry 通过 `NextEntry` 形成 Bucket 内单链表。

查询半径时只遍历覆盖的 Cell，再做精确距离、阵营和 Tag 过滤。

### 9.2 文件职责

```text
Public/Targeting/MassbilitySpatialTypes.h
Public/Targeting/MassbilitySpatialSubsystem.h
Private/Targeting/MassbilitySpatialSubsystem.cpp
Public/Targeting/MassbilityTargetingProcessor.h
Private/Targeting/MassbilityTargetingProcessor.cpp
```

Spatial Build Processor 按配置频率更新网格；Subsystem 提供只读查询。数组在初始化或首次达到峰值时 Reserve，稳定战斗阶段不应持续分配。

## 10. 自动选敌

```text
Public/Targeting/MassbilityTargetingProcessor.h
Private/Targeting/MassbilityTargetingProcessor.cpp
```

Targeting Processor：

- 只处理拥有 Ability Loadout、Targetable 且未死亡的实体。
- 只有 `CurrentTick >= NextRefreshTick` 才重新查询。
- 按 Entity Index 或固定哈希错开刷新 Tick，避免所有丧尸同帧查询。
- 从相邻空间 Cell 找最近敌对 Mass Entity。
- 设置 TargetEntity 和 LastKnownLocation。
- 没有候选时清空目标，并推迟到下一刷新 Tick。

第一版不做 LOS Trace。近战能力在激活前仍会做最终距离检查，因此旧目标不会造成超距离命中。

## 11. 自动能力激活

```text
Public/Ability/MassbilityAbilityProcessor.h
Private/Ability/MassbilityAbilityProcessor.cpp
```

Ability Processor 遍历有 Ability Loadout 的单位：

1. 跳过 Dead/Dying/AbilityBlocked。
2. 按 Loadout 顺序查找第一个 Ready Ability。
3. 使用 CurrentTarget 尝试激活。
4. 激活成功后停止本实体本 Tick 的后续 Ability 尝试。

第一版 Loadout 顺序就是能力优先级，不增加运行时评分系统。

外部系统也可以通过 AbilitySubsystem 提交显式请求；显式请求和自动请求走同一个 `TryActivate` 校验入口。

## 12. Processor 顺序

建议明确为：

```text
Massbility.SpatialBuild       低频重建目标索引
    -> Massbility.Targeting   分摊刷新 CurrentTarget
    -> Massbility.Ability     尝试激活并产生请求
    -> Massbility.EffectApply 每帧施加 Instant/Ongoing/Tag
    -> Massbility.EffectTick  配置频率处理 Period/Expire
    -> Massbility.Death       一次性死亡判定与事件
```

如果 EffectApply 与 Ability 位于同一 Mass Phase，Ability 本帧产生的请求是否能在同帧消费取决于队列写入时机与 Processor 顺序，必须用 `ExecuteAfter` 明确约束，不能依赖注册顺序。

## 13. C++ 文件总览

```text
Public
├── Ability
│   ├── MassbilityAbilityTypes.h
│   ├── MassbilityAbilityDefinition.h
│   ├── MassbilityAbilitySubsystem.h
│   └── MassbilityAbilityProcessor.h
├── Combat
│   ├── MassbilityCombatTypes.h
│   ├── MassbilityCombatFragments.h
│   └── MassbilityDeathProcessor.h
├── Core
│   └── MassbilitySimulationClock.h
├── Targeting
│   ├── MassbilitySpatialTypes.h
│   ├── MassbilitySpatialSubsystem.h
│   └── MassbilityTargetingProcessor.h       同时声明 SpatialBuild/Targeting Processor
└── Entity
    ├── MassbilityFragments.h            增加 Ability/Target/Combat Fragment
    └── MassbilityUnitDefinition.h       增加 Ability Loadout

Private
├── Ability
│   ├── MassbilityAbilityDefinition.cpp
│   ├── MassbilityAbilitySubsystem.cpp
│   └── MassbilityAbilityProcessor.cpp
├── Combat
│   └── MassbilityDeathProcessor.cpp
├── Core
│   └── MassbilitySimulationClock.cpp
└── Targeting
    ├── MassbilitySpatialSubsystem.cpp
    └── MassbilityTargetingProcessor.cpp     同时实现 SpatialBuild/Targeting Processor
```

同时修改：

- `MassbilityTypes.h`：Ability/Loadout 固定容量常量。
- `MassbilitySettings.h/.cpp`：Simulation/Targeting 频率和 CellSize。
- `MassbilitySubsystem.h/.cpp`：统一 Clock、Transform 创建入口、共享 Unit Loadout 查询。
- `MassbilityEntityFactory.cpp`：初始化 Transform、Ability、Target、Combat Fragment。
- `MassbilityEffectSubsystem.h/.cpp`：请求施加与周期 Tick 分离、Damage Context 写入。
- `MassbilityEffectProcessor.h/.cpp`：拆分 EffectApply 与 EffectTick。

## 14. 实施顺序

### Phase 3A：基础收口

1. 增加统一 Simulation Clock 与配置快照。
2. 拆分 EffectApply 和 EffectTick Processor。
3. 增加 Combat Fragment、Damage Context、Death Processor 与死亡事件队列。
4. 增加主动移除 Effect 的内部 API。
5. 明确 Processor Group 和依赖顺序。

### Phase 3B：Ability 最小垂直切片

1. 增加 Ability Types、Definition 和静态校验。
2. 增加 Ability Registry、Compiled Ability 与 Unit Loadout。
3. 增加 Ability Fragment 和显式 Ability Request。
4. 使用外部指定 Mass Target 验证即时近战能力。
5. 验证 Required/Blocked Tag、AbilityBlocked 与 Cooldown。

### Phase 3C：Targeting

1. 为单位 Archetype 增加标准 Transform Fragment。
2. 增加 Target Fragment。
3. 实现 2D Uniform Hash Grid 和构建 Processor。
4. 实现错峰 Target Refresh 和最近敌对目标查询。
5. Ability Processor 使用 CurrentTarget 自动激活近战能力。

按 A、B、C 分三轮落代码，每轮都保持可编译和可单独验收。

## 15. 功能验收

### 基础战斗

- Instant Damage 在请求提交后的同一处理帧修改 Health。
- Health 归零只生成一次死亡事件。
- DeathEvent 保留最后有效 Source 和 EffectId。
- Dead 单位不能被再次激活能力，也不会继续作为 Target。
- 周期伤害导致死亡时同样保留来源。

### Ability

- 冷却未结束时不能激活，也不重复写冷却。
- Required/Blocked Owner Tag 正确。
- Required/Blocked Target Tag 正确。
- AbilityBlocked Flag 阻止激活。
- 超出 Range 不产生 EffectRequest。
- 同一 AbilityDefinition 对不同 Attribute Schema 目标正确产生对应 Effect Variant。

### Targeting

- 同队单位不会互相选择。
- 最近敌对单位被选中。
- 目标死亡、销毁或不可 Targetable 后会被清除。
- 目标超出丢失距离后会被刷新。
- 查询结果与暴力遍历基准在随机场景中一致。

### 完整闭环

```text
Zombie A 建立空间索引
  -> 选中 Enemy B
  -> Melee Ability Ready
  -> 产生 Damage/Effect Request
  -> Health 降低
  -> Health <= 0
  -> Dying/Dead
  -> DeathEvent(Source=A, Victim=B)
```

## 16. 性能验收

分别测试 1 万、5 万和 10 万 Mass 单位：

- Spatial Build 时间和内存容量是否稳定。
- 每次 Target Refresh 平均检查的候选数。
- 目标刷新错峰后是否仍有单帧尖峰。
- Ready Ability 检查数和成功激活数。
- 每帧 Effect/Damage Request 数量。
- 无 Ready Ability、无 Target 和无 Active Effect 时的基线开销。
- 运行稳定后是否仍发生数组扩容或 UObject 创建。

建议增加非 Shipping 计数器：

```text
SpatialEntries
SpatialCellsUsed
TargetQueries
TargetCandidatesTested
AbilityAttempts
AbilitySucceeded
AbilityRejectedByCooldown
AbilityRejectedByTags
AbilityRejectedByRange
EffectRequestsQueued
DeathsResolved
```

Phase 3 完成标准不是“丧尸能扣血”，而是目标获取、能力校验、效果施加、死亡归属形成统一数据链，并能在目标实体规模下测出每一段成本。
