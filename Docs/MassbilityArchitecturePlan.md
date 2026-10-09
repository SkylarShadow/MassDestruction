# Massbility 总体方案与实现计划

## 1. 定位与目标

`MassDestruction` 是插件容器。`Massbility` 是其面向 Unreal Mass Entity 的大规模战斗、能力、属性和效果运行时。

Massbility 服务于 FPS roguelike 中大量普通丧尸、Mass 化投射物和范围效果。它使用与 Gameplay Ability System（GAS）一致的玩法词汇，例如 Gameplay Tag、属性、效果、伤害上下文和能力，但不复刻 ASC、Gameplay Ability 实例、Gameplay Effect Spec 或 Active Gameplay Effect 的对象模型。

目标：

- 普通怪物以 Mass Fragment 和 Processor 批处理，不为每个实体创建 Actor、Controller、ASC 或 UObject Ability。
- 玩家、精英和 Boss 继续使用 Skyra GAS；普通怪和 GAS 角色可双向伤害、施加效果和触发击杀事件。
- 设计期使用 Gameplay Tag、数据资产和效果定义；Mass 热路径使用紧凑 ID、属性槽位、位标志和位掩码。
- 核心运行时模块不依赖 GAS；GAS 集成作为可选模块存在。

## 2. 系统边界

```text
玩家 / 精英 / Boss
  Actor + Skyra GAS
          <-> MassbilityGAS Bridge <->
普通丧尸 / Mass 投射物 / 范围效果
  Mass Entity + Massbility
```

| 系统 | 管理对象 | 责任 |
| --- | --- | --- |
| Skyra GAS | 玩家、精英、Boss | 复杂能力、装备、属性、完整 GE、预测和角色复制 |
| Massbility | 普通丧尸群、Mass 投射物、范围效果 | 群体移动、目标选择、能力、MassGE、属性、伤害、死亡和击杀 |
| MassbilityGAS | 两侧边界 | 目标解析、GE 映射、属性映射、Context 和 SetByCaller 转换 |

一个单位在同一时刻只能有一个生命值与效果状态的权威系统：普通怪由 Massbility 处理；GAS 角色由 ASC 处理。表现切换到骨骼网格不改变该归属。

## 3. 模块布局

```text
MassDestruction
├── MassDestruction             插件入口和全局设置
├── Massbility                  核心 Runtime，不依赖 GAS
│   ├── Entity                  Fragments、单位模板、Archetype
│   ├── Ability                 能力定义、请求、冷却与执行
│   ├── Effect                  MassGE、Modifier、Stack、周期效果、Kernel
│   ├── Attribute               Schema、属性句柄、属性存储
│   ├── Combat                  Damage/Effect Request、结算、死亡
│   ├── Targeting               空间索引、射线、范围和最近目标查询
│   ├── Tags                    Runtime Tag Catalog、Tag 编译和 Mask
│   └── Representation          表现状态同步接口
├── MassbilityGAS               可选 Runtime，依赖 GameplayAbilities 与 Skyra
│   ├── EffectBridge
│   ├── AttributeBridge
│   ├── ContextBridge
│   └── SkyraAdapter
└── MassDestructionEditor       可选 Editor，数据编译、校验和调试
```

## 4. 核心运行时模型

### 4.1 Ability

`UMassbilityAbilityDefinition` 是共享的只读数据资产。实体运行时只持有 `AbilityId`、目标 Handle、活动阶段索引、阶段结束 Tick 和冷却结束 Tick。

能力定义在加载或 Cook 时编译为紧凑执行计划。即时伤害能力可立即完成；需要前摇、攻击窗口或持续移动的能力才保存阶段数据。冷却由 `NextReadyTick` 判断，不需要独立的能力 UObject 或 Timer。

### 4.2 MassGE

`UMassbilityEffectDefinition` 定义效果身份、持续时间、周期、层数规则、Tag 修改、属性 Modifier 与 Execution Kernel。它本身不保存 `MassOnly`、`MassToGAS` 等运行域；效果只表达玩法意图，例如 `Effect.Burning`。

定义会编译为 `EffectId` 和 `FMassbilityCompiledEffect`。实体的 Active Effect Slot 保存 EffectId、层数、过期 Tick、下次周期 Tick 和最小必要上下文。

MassGE 支持瞬时伤害、治疗、周期伤害、减速、眩晕、燃烧、中毒、易伤、护盾和有限叠层。复杂公式通过少量内建 Execution Kernel 或按 EffectId 注册的批处理函数实现，不自动执行任意 `UGameplayEffectExecutionCalculation`。

### 4.3 Attribute

属性以 `GameplayTag` 作为设计期身份，例如 `Attribute.Armor`。单位模板选择组合式 Attribute Schema；Schema 引用多个可复用 Attribute Set，并在加载/Cook 阶段展平为稳定布局。

```text
Attribute.Armor -> AttributeHandle 3
```

效果的 Modifier 编译后直接使用 AttributeHandle。核心高频属性，例如 Health、MaxHealth、MoveSpeed，可使用专用 Fragment 字段；Armor、抗性和 roguelike 专属数值使用固定上限、连续的扩展属性槽位。新增属性通过 Schema 完成，不依赖全局 C++ 枚举。

### 4.4 Tag

Gameplay Tag 是作者语言和 Mass/GAS 的跨系统协议，不直接作为 Mass 热路径的通用容器。

| 分类 | 示例 | 运行时表示 |
| --- | --- | --- |
| 静态身份 | `Unit.Zombie.Walker`、`Faction.Zombie` | Shared Definition 或 Archetype ID |
| 高频状态 | Dead、AbilityBlocked、Charging | `StateFlags` |
| 效果/条件 | `State.Immune.Fire`、`State.Burning` | `RuntimeTagMask` |

`RuntimeTagCatalog` 只收录需要进入 Mass 高频判定的 Tag，并在 Cook 时冻结。Required、Blocked、Granted Tag 与父子层级关系会编译为固定宽度 Tag Mask。运行时条件判断只进行位运算。

`StateFlags` 表示 Mass 内部的固定语义，例如 Dying、Dead、AbilityBlocked 与 Charging；它不是项目 Gameplay Tag 的硬编码副本。`AbilityBlocked` 表达“当前不能执行能力”，眩晕、冻结等具体原因由可配置 Runtime Tag 表达。`UMassbilityStateTagMapping` 设置资产负责将 Flag 映射到项目的 Gameplay Tag，并参与 Runtime Tag Catalog 编译。默认映射遵循 Skyra 的既有命名：

| Mass State Flag | 默认 Gameplay Tag | 备注 |
| --- | --- | --- |
| Dying | `Status.Death.Dying` | 已开始死亡流程 |
| Dead | `Status.Death.Dead` | 已完成死亡流程 |

项目可在编辑器中为每个 Flag 改用自己的 Tag，或绑定多个 Tag。父 Tag 例如 `Status.Death` 由编译器随子 Tag 自动展开。自定义状态不必加入 StateFlags，可作为普通 Runtime Tag 由效果和能力定义授予或移除。死亡等一次性事件由后续 Combat/Event 系统配置，不作为状态映射资产的固定字段。Massbility 核心只持有编译后的 Flag、TagMask 和 Tag ID，不包含 Skyra Tag 的 C++ 依赖。

### 4.5 Combat

所有攻击归一为 `EffectRequest` 或 `DamageRequest`，包含来源、目标、EffectTag、DamageType、数值、Tag、Origin、HitLocation 与方向。

```text
目标为 Mass Entity
  -> Massbility Combat Resolver
  -> MassGE / Attribute Fragment / Death Event

目标为 Actor + ASC
  -> MassbilityGAS Bridge
  -> Gameplay Effect + Context + SetByCaller
  -> Skyra AttributeSet / Execution / Message
```

玩家武器或技能命中普通怪时，通过 Mass 空间查询取得实体后提交 Request。普通怪攻击玩家时，Bridge 将 Request 转换为项目对应的 Gameplay Effect。

## 5. 性能约束

Mass 热路径每实体不得创建 UObject、ASC、Gameplay Effect Spec 或 Ability 实例；不得使用 `TMap`、`FGameplayTagContainer`、动态 `TArray`、Blueprint、Delegate 或逐实体 Trace/导航。

热路径只读取和写入：Entity Handle、共享定义 ID、AbilityId、EffectId、AttributeHandle、固定属性槽、StateFlags、RuntimeTagMask、时间 Tick、目标 Handle 和空间索引。

Tag Container、Gameplay Effect、UObject、Blueprint 和 GAS 只能出现在资产编译、实体生成、跨系统桥接、表现、调试和编辑器路径。周期效果按量化 Tick 或到期 Bucket 执行；空间查询和路径规划按群组、距离 LOD 和预算分摊。

## 6. 分阶段实现计划

### Phase 0：插件基础

- 完成 `Massbility` Runtime 模块、公共 API 宏和基础依赖。
- 定义命名空间、日志类别、原生 Gameplay Tag 入口和插件设置。
- 建立模块文档与测试目录。

验收：插件可被项目加载，Massbility 无 GAS 依赖。

### Phase 1：单位、属性与 Tag 基础

- 实现单位定义、Mass Fragment、Attribute Schema 与 Schema 编译。
- 实现核心属性、固定扩展属性槽位和 AttributeHandle。
- 实现 `UMassbilityStateTagMapping`；以 Skyra `Status.Death.Dying` 和 `Status.Death.Dead` 为默认值，但允许项目资产覆盖。
- 实现 Runtime Tag Catalog、TagMask 编译、StateFlags 与基础 Tag 条件判断。
- 实现 Mass 单位创建、销毁和基础生命值读写。

验收：不同类型丧尸可使用不同 Schema，Armor 等新属性不修改全局属性枚举；死亡状态默认能产出 Skyra 兼容 Tag，项目可在编辑器替换映射；运行时不保留每实体 TagContainer。

### Phase 2：MassGE 与伤害结算

- 实现 Effect Definition、Compiled Effect、固定 Active Effect Slot。
- 实现 Instant Damage、Heal、Slow、Stun、Burning、Poison 与有限 Stack。
- 实现 DamageRequest、EffectRequest、队伍判定、死亡和击杀事件。
- 实现周期效果的量化调度与到期处理。

验收：大量普通怪可被批量施加、刷新、叠加和移除效果；死亡只触发一次，并保留击杀来源。

### Phase 3：能力与目标系统

- 实现 Ability Definition、请求队列、冷却和基础执行计划。
- 首先实现即时近战伤害与目标范围判断。
- 实现空间网格查询：半径、最近目标、射线候选查询。
- 将能力输出统一为 EffectRequest 或 DamageRequest。

验收：普通丧尸能批量发现目标、攻击、受击和死亡，且核心循环无 UObject/Blueprint 调用。

### Phase 4：Mass 与 Skyra GAS 桥接

- 新增 `MassbilityGAS` 模块。
- 实现 `EffectTag -> GameplayEffect Class` 映射。
- 实现 `AttributeTag -> FGameplayAttribute` 和 SetByCaller 映射。
- 实现 Mass Context 与 GameplayEffectContext 的转换。
- 接入 Skyra 队伍规则、HealthSet 和伤害事件。

验收：玩家 GAS 武器可伤害 Mass 普通怪；Mass 普通怪攻击玩家时可通过 Skyra GE 正确扣血、触发消息和死亡事件。

### Phase 5：Roguelike 与群体扩展

- 扩展冲锋、投射物、范围感染、击退、召唤和特殊怪能力。
- 接入词条、等级、波次、掉落与经验归属。
- 按距离 LOD 降低感知、目标、能力和效果更新频率。
- 添加 ISM/Niagara/骨骼网格对象池表现同步。

验收：在目标规模下维持稳定帧时间，普通怪的逻辑、效果和表现均可按 LOD 降级。

### Phase 6：验证与工具

- 增加 Effect/Schema 编译校验、Tag Catalog 容量校验和跨模块映射校验。
- 增加性能计数：实体数、活跃效果数、周期 Tick 数、请求数、空间查询候选数和桥接调用数。
- 增加 Mass Debug 视图、效果状态查看和热路径分配检测。

验收：能定位每帧热点、意外分配、TagMask 溢出和不应发生的 GAS Bridge 调用。

## 7. 首个垂直切片

首个可玩闭环只包含普通近战丧尸、玩家枪械和玩家 ASC：

1. Mass 丧尸追击玩家，并以 Massbility 近战能力产生 DamageRequest。
2. 玩家枪械通过 Mass 空间查询命中普通丧尸，并提交 Mass DamageRequest。
3. 普通丧尸使用 Mass 属性、MassGE、受击和死亡流程。
4. 丧尸攻击玩家时经 MassbilityGAS 转换为 Skyra Gameplay Effect。
5. 击杀事件可提供伤害来源、目标、EffectTag 与奖励归属。

此切片验证 Massbility 的完整核心链路，再扩展特殊怪、投射物、范围技能与 roguelike 词条。
