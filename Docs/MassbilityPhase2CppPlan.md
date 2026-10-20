# Massbility Phase 2：MassGE 与效果运行时方案

## 1. 目标

Phase 2 在已完成的单位、Attribute Set、Schema、Runtime Tag 与基础 Fragment 之上，实现独立的 MassGE 运行时：即时效果、持续效果、周期效果、有限层数、属性修改与效果 Tag。

本阶段仍不实现 Ability、空间目标选择、投射物、Skyra GAS Bridge、Gameplay Cue、网络同步或任意自定义 Execution UObject。所有逻辑保持在 Massbility Runtime 内。

当前实现状态：Phase 2 第一轮运行时骨架已落地并通过 `PrjInitEditor Win64 Development` 编译。已包含效果注册、按 Schema 编译变体、请求队列、固定效果槽、持续/周期/叠层、Granted Tag 合并和统一属性访问。

## 2. 性能结论与约束

Attribute Set 的组合只发生于 Schema 首次注册：多个 Set 被展平为一个固定 Slot 布局，并由 `SchemaId` 缓存。实体和 Processor 不保存、遍历或查询 Attribute Set，因此组合模型没有每帧性能成本。

Phase 2 的性能约束：

- Effect Definition 在加载或首次注册时编译为 `EffectId`，实体不保存 UObject 指针。
- 每个实体使用固定数量的 Active Effect Slot；第一版容量为 4。
- `FMassbilityHasActiveEffectsTag` 只标记有活跃效果的实体，周期 Processor 不遍历无效果单位。
- 效果 Tag 只在效果添加、刷新、层数变化和移除时重建；能力/战斗查询只读取既有 RuntimeTagMask。
- Duration 和 Period 转为量化 Simulation Tick；不创建实体级 Timer。
- Effect Request 在世界级队列中批量处理；不在 Mass Chunk 遍历中创建 UObject、调用 Blueprint 或访问 ASC。
- Effect Modifier 只使用已编译 Attribute Handle；不在热路径查询 Gameplay Tag 或 TMap。

唯一应预热的路径是 Effect Definition 和 Unit Definition 的首次注册。波次开始前应登记会出现的单位和效果，避免首个特殊效果在战斗中触发一次性编译开销。

## 3. 运行时数据流

```text
UMassbilityEffectDefinition
  -> Effect Registry 编译
  -> EffectId + Compiled Effect

外部系统 / 后续 Ability
  -> FMassbilityEffectRequest
  -> UMassbilityEffectSubsystem PendingRequests
  -> Apply / Refresh / Stack
  -> Active Effect Slot

UMassbilityEffectProcessor
  -> 周期 Tick
  -> 到期移除
  -> 重建 RuntimeTagMask
  -> 应用或还原属性派生值
```

## 4. 属性访问的必要调整

Phase 1 的 `FMassbilityAttributeHandle` 目前只指向扩展 Slot。Phase 2 的 Modifier 必须也能作用于 Health、MaxHealth、MoveSpeed，因此需要将 Handle 扩展为“存储区域 + 索引”：

```cpp
enum class EMassbilityAttributeStorage : uint8
{
    Vital,
    Extended,
};

struct FMassbilityAttributeHandle
{
    EMassbilityAttributeStorage Storage;
    uint8 Index;
};
```

`Vital` 中的 Index 映射 Health、MaxHealth、MoveSpeed；`Extended` 中的 Index 指向 Schema 编译出的固定 Slot。编译效果时完成 `Attribute.* -> Handle` 的解析，执行效果时不再查询 Tag。

Phase 2 第一版只支持可逆的 `Add` Modifier。Ongoing Add 仅允许修改 MoveSpeed 与扩展属性，并采用不截断的可逆加法，移除时可准确恢复；Health、MaxHealth 仅允许 Instant/Periodic 路径。Multiply、Override、复杂捕获属性和任意公式留给后续 Aggregator/Kernel 阶段。

效果模板先按 `EffectId` 编译与 Schema 无关的数据；第一次作用到某个 Schema 时，再生成 `(EffectId, SchemaId) -> VariantId`。Variant 中保存已经解析的 Attribute Handle，Active Effect Slot 只保存 VariantId，周期热路径不会查询 GameplayTag 或 TMap。

## 5. C++ 文件与职责

```text
Public
├── Core
│   └── MassbilitySubsystem.h                 扩展 Effect Registry、预热和属性访问
├── Effect
│   ├── MassbilityEffectTypes.h               EffectId、Tick、Stack、Modifier、Request、Slot
│   ├── MassbilityEffectDefinition.h          作者侧 MassGE Data Asset
│   ├── MassbilityEffectSubsystem.h           世界级请求队列与公开 Apply API
│   └── MassbilityEffectProcessor.h           Mass Phase 周期和到期处理
├── Entity
│   └── MassbilityFragments.h                 Active Effect Fragment 与 Active Tag
└── Attributes
    └── MassbilityAttributeAccess.h           Handle 解析、读写、Clamp、可逆 Modifier

Private
├── Effect
│   ├── MassbilityEffectDefinition.cpp        Effect Definition 编译和校验
│   ├── MassbilityEffectSubsystem.cpp         Request 合并、Apply/Refresh/Remove
│   └── MassbilityEffectProcessor.cpp         批量 Tick、周期执行、到期移除
└── Attributes
    └── MassbilityAttributeAccess.cpp         Vital/Extended 的统一读写
```

### `MassbilityEffectTypes.h`

定义纯数据运行时类型：

- `FMassbilityEffectId`：Registry 中的紧凑效果 ID。
- `EMassbilityEffectStackPolicy`：Reject、RefreshDuration、AddStack。
- `EMassbilityModifierOp`：Phase 2 先只启用 Add。
- `FMassbilityEffectModifierDefinition`：作者数据，含 AttributeTag、操作和数值。
- `FMassbilityCompiledModifier`：编译后，含 AttributeHandle、操作和数值。
- `FMassbilityCompiledEffect`：DurationTick、PeriodTick、StackLimit、GrantedTagMask 与编译 Modifier。
- `FMassbilityEffectRequest`：目标 Entity、来源 Entity、EffectId 与可选 Level/Magnitude。
- `FMassbilityActiveEffectSlot`：EffectId、StackCount、ExpireTick、NextPeriodicTick、来源 Handle。

### `UMassbilityEffectDefinition`

作者创建的 MassGE Data Asset，包含：

- `EffectTag`：效果的跨系统玩法身份，例如 `Effect.Burning`。
- Duration、Period、StackPolicy、StackLimit。
- GrantedTags：效果活跃时授予的状态 Tag，例如 `State.Burning`。
- Modifiers：属性变化定义。

它没有 GAS Domain、Gameplay Effect Class、ASC 或 Blueprint 回调。GAS 映射属于未来 `MassbilityGAS` 模块。

### `UMassbilityEffectSubsystem`

World 级效果入口，职责：

- 注册和预热 Effect Definition。
- `QueueEffectRequest`：收集效果请求，不立即在任意 Processor 中修改实体。
- 在安全阶段按目标批量处理 Request。
- 分配、刷新、叠加和移除 Active Effect Slot。
- 效果状态变化后调用 `RebuildRuntimeTags`。

请求队列在初始化时 Reserve；处理结束后 Reset，不为每个请求分配独立对象。

### `UMassbilityEffectProcessor`

只查询具有 `FMassbilityHasActiveEffectsTag` 的实体：

- 比较 `NextPeriodicTick`，执行到期的周期 Modifier。
- 比较 `ExpireTick`，移除到期 Slot 并还原可逆 Modifier。
- 当最后一个 Slot 被移除时，移除 Active Effects Mass Tag。

它不解析 Gameplay Tag、不查 TMap、不调用 Subsystem 的 Tag 查找 API。

### `MassbilityAttributeAccess`

统一访问 Vital 和扩展属性：

- `ResolveAttributeHandle`：低频，将 AttributeTag 解析为 Handle。
- `GetAttribute`：热路径读取 Handle 指向的值。
- `SetAttributeClamped`：根据 Attribute 元数据写入并 Clamp。
- `ApplyAddModifier`：添加或移除可逆 Modifier。

Health 视为当前值，不能由持续 Modifier 直接覆盖；持续效果通常修改 MaxHealth、MoveSpeed、Armor 等派生属性。即时伤害与治疗通过 Health 的专用路径执行。

## 6. Fragment 变化

```cpp
USTRUCT()
struct FMassbilityActiveEffectFragment : public FMassFragment
{
    uint8 ActiveEffectCount;
    TStaticArray<FMassbilityActiveEffectSlot, 4> Slots;
};

USTRUCT()
struct FMassbilityHasActiveEffectsTag : public FMassTag
{
};
```

Effect Fragment 固定存在于基础单位 Archetype，容量固定，避免施加效果时创建动态容器。Mass Tag 仅控制 Processor 是否遍历该实体；当第一个效果加入或最后一个效果移除时才发生 Archetype 变化。

现有 `FMassbilityStateFragment` 保留 `BaseTags` 和 `RuntimeTags`。每次状态 Flag 或效果 Slot 改变时，统一执行：

```text
RuntimeTags = BaseTags
            + StateFlag 对应 TagMask
            + 所有活跃 Effect 的 GrantedTagMask
```

重建只扫描最多 4 个 Slot，发生在效果状态变化时，不发生在每帧查询时。

## 7. 实施顺序

1. 扩展 Attribute Handle，使其支持 Vital 与 Extended 存储区域。
2. 添加统一 Attribute Access，并保持 Phase 1 的扩展属性 API 兼容。
3. 添加 Effect Types 与 Effect Definition；实现 Definition 编译和静态校验。
4. 在 `UMassbilitySubsystem` 增加 Effect Registry 与预热 API。
5. 添加 Active Effect Fragment 与 Active Effects Mass Tag。
6. 实现 Effect Subsystem、Request Queue 与 AddStack/Refresh/Reject 行为。
7. 实现 Effect Processor 的周期 Tick、到期移除和 Runtime Tag 重建。
8. 验证即时伤害、Burning、Slow、Stun/AbilityBlocked、Armor Buff 与效果叠层。

## 8. 首个验证组合

```text
Effect.Burning
  Duration: 5 秒
  Period: 0.5 秒
  Granted Tag: State.Burning
  每周期: 对 Health 走专用伤害路径

Effect.Slow
  Duration: 2 秒
  Modifier: Add MoveSpeed -120

Effect.Stun
  Duration: 1 秒
  Granted Tag: State.CC.Stunned
  设置 AbilityBlocked Flag 或由后续 Ability Processor 读取 TagMask

Effect.ArmorBuff
  Duration: 6 秒
  Modifier: Add Attribute.Armor +30
```

验收重点：相同 Effect 可正确刷新或叠层；多个 Effect 同时授予同一个 Tag 时不会提前清除；周期 Tick 不遍历没有活跃效果的实体；效果移除后属性和 Runtime Tag 完整还原。
