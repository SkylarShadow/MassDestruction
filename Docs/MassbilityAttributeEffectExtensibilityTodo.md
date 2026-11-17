# Massbility 属性与效果扩展 TODO

## 背景

当前 `FMassVitalFragment` 保存 `Health`、`MaxHealth`、`MoveSpeed` 等固定核心属性；扩展属性通过 `UMassAttributeSchema` 编译到 `FMassAttributeFragment` 的连续槽位中。效果与伤害结算仍包含部分固定语义。

需要评估是否提供类似 GAS `AttributeSet` 与 `ExecutionCalculation` 的可扩展模型，以支持护盾、能量、怒气、元素屏障等玩法，而不为每种新属性持续修改核心 Fragment、枚举和结算代码。

此文档只记录待决策事项，**不代表已经选择任何实现方案**。

## 目标与约束

- [ ] 作者侧允许以可复用的 Data Asset / C++ 扩展定义组合属性与结算行为。
- [ ] 运行期保持 Mass 数据导向特性：每实体不创建 UObject、ASC、GameplayEffectSpec、动态数组或虚函数对象。
- [ ] 热路径使用编译后的 Handle、连续槽位、固定容量数据和函数表/ID，而不是 GameplayTag 查找。
- [ ] 新增护盾等玩法时，不应要求把领域语义写入 `FMassVitalFragment`、`EMassVitalAttribute` 或核心 Damage Resolver。
- [ ] Mass Entity 与 GAS Actor 对同一伤害语义应有可对齐的结算顺序。

## 待决策：属性承载方式

### 方案 A：护盾等高频属性并入 Vital

- 优点：最短访问路径；可直接复用 Vital 的读取、钳制与批处理。
- 缺点：所有实体承担内存；每种新玩法都修改核心枚举、Fragment 和访问代码；不满足长期扩展目标。

### 方案 B：Attribute Set 编译为 Extended Attribute

- 定义可继承/可组合的 `UMassAttributeSetDefinition`，如 `Shield`、`Energy`、`Boss`。
- 加载或 Cook 时将多个 Set 展平到稳定 `FMassCompiledAttributeSchema` 布局。
- 实体运行期通过 `FMassAttributeHandle` 和连续 `Values[]` 访问。
- 优点：新增普通数值无需更改全局枚举；符合已有 Schema 设计。
- 待确认：Handle 是否需要携带 Schema 身份，防止跨 Schema 错用相同 Index。

### 方案 C：Attribute Set + 可选专用 Fragment

- 基础数值（例如 Shield / MaxShield）走 Extended Attribute。
- 有复杂状态时由扩展模块声明专用 Fragment，例如破盾 Tick、回复延迟、护盾类型。
- 优点：避免所有单位携带稀有状态，也避免把复杂状态塞进通用属性数组。
- 待确认：如何由 Unit Trait / Archetype Recipe 声明该 Fragment，且避免运行期频繁结构迁移。

## 待决策：效果与伤害执行扩展点

### 方案 A：继续向核心枚举添加专用 Output / Damage 类型

- 优点：首个功能实现直接。
- 缺点：护盾、减伤、无敌、伤害分摊等都会持续扩大核心分支；不推荐作为长期方向。

### 方案 B：可注册的 Effect Execution / Damage Layer

- 核心只调度通用的 `Combat.Damage`、`Combat.Heal`、Effect Modifier 等语义。
- 扩展模块注册编译后的执行器或 Damage Layer，并定义优先级。
- 示例结算：`Shield Layer` 消耗伤害并返回剩余值，随后 `Health Layer` 处理剩余伤害。
- 执行器作者侧可采用可继承 Definition；运行期应编译为 ID、固定数据和无分配调用表。
- 待确认：注册时机、优先级/稳定顺序、失败语义、跨模块依赖和卸载规则。

## 护盾最小验证用例

- [ ] Shield / MaxShield 可作为扩展属性定义，不修改 Vital 枚举。
- [ ] 100 点伤害命中 Shield=60、Health=100 的单位后，结果为 Shield=0、Health=60。
- [ ] 无 Shield Set 的单位接收同一 Damage 时，结果为 Health=0。
- [ ] Shield 不超过 MaxShield，且 MaxShield 降低时 Shield 同步钳制。
- [ ] 可定义破盾事件、回复延迟和回复速率，而不影响无护盾单位的 Archetype。
- [ ] Mass → GAS 与 GAS → Mass 的护盾优先级及最终生命变化可对齐。
- [ ] 批量受击/周期效果中不发生每实体 UObject 分配、GameplayTag 查找或动态容器扩容。

## 建议的后续决策顺序

1. 确认 Shield 是否为所有单位的基础属性，还是可选战斗模块。
2. 确认现有 Effect / Output 层的最小通用执行器注册接口。
3. 先以 Shield 实现一条端到端验证链路，再决定是否抽象为公开扩展 API。
4. 在确定 API 前，不向 `EMassVitalAttribute` 增加 Shield，也不向核心 Output 枚举添加 Shield 专用类型。

## 待决策：战斗队列的 Project Settings 容量

`UMassCombatSubsystem::Initialize` 当前为 `PendingDamageRequests` 和 `PendingDeathCandidates` 使用固定的 `Reserve` 容量。该值属于项目级性能预算，应评估是否通过 `UMassCombatSettings : UDeveloperSettings` 暴露到 Project Settings。

- [ ] 提供 `InitialPendingDamageRequestCapacity` 与 `InitialPendingDeathCandidateCapacity`，仅用于 World 初始化时的 `TArray::Reserve`。
- [ ] 在设置项说明中明确：Initial Capacity 不是队列最大容量；超过预留值时数组仍可扩容。
- [ ] 若未来需要限流或溢出保护，单独设计 `MaxPending...`、预算和拒绝/延迟策略，不与 Reserve 复用同一配置。
- [ ] 使用项目配置（建议 `DefaultGame.ini`）而非每个 Subsystem 实例的 `UPROPERTY`；值应在重新创建 World/重新开始 PIE 后生效。
- [ ] 对配置值设置非负校验，并通过压测确定各目标平台的推荐默认值。
