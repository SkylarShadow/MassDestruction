# Massbility Ability 触发语义 TODO

## 背景与当前约定

当前 `UMassAbilityProcessor` 在每个模拟 Tick 中先消费 `QueueAbility` 的显式请求，随后按 Loadout 槽位顺序尝试自动能力：

- `Self` 与 `CurrentTarget` 能参与自动尝试；
- `ExplicitTarget` 必须通过 `QueueAbility(Source, Slot, Target)` 提交；
- 每个实体每个模拟 Tick 最多成功自动释放一个能力；
- `TryActivate` 是手动和自动路径共享的唯一校验与输出提交点。

当前不引入 `EMassAbilityActivationPolicy`。它会与 `TargetPolicy` 产生多余组合，且现阶段没有足够玩法需求证明需要三态配置。

## 优先级定义

- **P0**：缺失会导致错误结算、无确定性或阻断当前玩法。
- **P1**：目标玩法确定后必须实现，但可以独立排期。
- **P2**：扩展性工作；出现多个实际使用方后再实现。
- **P3**：仅优化作者体验或调试体验。

## TODO

### [P1] 轻量 Ability 生命周期（前摇、命中窗口、结束与取消）

- [ ] 将当前瞬时 `TryActivate` 路径拆分为 `CanActivate`、`BeginAbility`、`ExecuteImpact`、`EndAbility` 与 `CancelAbility`；所有路径仍复用同一套状态、冷却、Tag、目标和范围校验。
- [ ] 为实体增加固定容量的 Active Ability 运行时状态：槽位、目标 Handle、`InstanceId`、阶段、`ImpactTick`、`NextPeriodTick`、`EndTick`；普通单位默认同时只允许一个活动技能。
- [ ] 在 Definition / 编译数据中支持前摇、命中窗口、后摇、持续时间、周期，以及“冷却在 Begin 或命中后提交”的明确语义；瞬时能力保持零额外状态的现有快路径。
- [ ] 新增仅查询 Active Ability Tag 的 Execution Processor；它在命中 Tick 投递单体 Output，在周期 Tick 投递持续 Output，到 EndTick 统一收尾。非活动单位不得进入该查询。
- [ ] 定义取消策略：死亡、`AbilityBlocked`、眩晕类 Effect、目标丢失（可配置）应调用 `CancelAbility`，清理临时 Runtime Tag 与表现关联，且不得遗留活动状态。
- [ ] 范围技能只在 ImpactTick / PeriodTick 执行空间查询，并限制候选或命中数量；不得每 Tick 为全部单位扫描邻居。
- [ ] 为英雄 / External Proxy 提供可选动画事件桥接：`Entity + InstanceId + EventTag` 仅可推进匹配的活动技能；普通 Mass 单位仍以模拟 Tick 决定伤害，动画只跟随表现。
- [ ] 覆盖测试：前摇后单体命中、目标丢失取消、死亡取消、周期伤害、同 Tick 动画事件去重、旧 `InstanceId` 事件被拒绝、瞬时能力保持原有行为。

**风险：中到高。** 生命周期状态必须保持固定容量、无 UObject / Timer 实例，并由模拟 Tick 驱动；动画通知不能成为大量 Mass 单位伤害结算的唯一时钟。范围查询和事件链须有容量、最大命中数及可观测性预算。

### [P1] 自动与显式触发解耦的最小开关

- [ ] 当出现“已有 `CurrentTarget`，但只能由玩家、AI 指令或 GAS 事件触发”的实际能力时，为 `UMassAbilityDefinition` 增加 `bAllowAutomaticActivation`，默认 `true`。
- [ ] 自动 Processor 仅跳过 `bAllowAutomaticActivation == false` 的能力；显式 `QueueAbility` 不受该字段限制。
- [ ] 保持 `TargetPolicy` 只描述目标来源，不把它继续扩展为触发来源。
- [ ] 增加覆盖测试：`CurrentTarget + 自动关闭`、`Self + 自动关闭`、`ExplicitTarget + 显式请求`。

**风险：中。** 默认值或资产迁移错误会改变既有单位的自动施法行为；必须让旧资产保持当前默认行为。不要在这一项中引入 `ManualOnly / Both / Automatic` 三态枚举，以免产生尚无业务含义的组合。

### [P1] 显式目标的 AI 选择接缝

- [ ] 为 AI / 项目逻辑提供明确的“选择目标后提交能力”的接口约定：先完成查询或评分，再调用 `QueueAbility`。
- [ ] 维持 `UMassAbilityProcessor` 只做执行与校验，不在其中进行目标评分、寻路或战术决策。
- [ ] 明确请求失效语义：目标在 Flush 前死亡、失效或越界时，`TryActivate` 返回失败且不消耗冷却。

**风险：低到中。** 若把选目标逻辑塞进 Ability Processor，会让每 Tick 成本随单位数和候选数相乘，并破坏 Processor 的职责边界。

### [P1] 同一能力允许自动与强制释放

- [ ] 验证业务需要后，允许同一 `Self` 或 `CurrentTarget` 能力既被自动循环尝试，也可由 `QueueAbility` 强制请求。
- [ ] 确定同一 Tick 的优先级：保持“显式请求先于自动尝试”；成功的显式请求应使同一实体本 Tick 不再自动成功第二次。
- [ ] 为每个实体增加或复用“本 Tick 已成功激活”的轻量运行时标记，避免双重释放。
- [ ] 增加同 Tick 冷却、多个显式请求和显式成功后自动路径的确定性测试。

**风险：中到高。** 当前代码只限制“自动路径”成功一次；若显式请求先成功，自动循环仍可能继续尝试其他槽位。若能力间没有共享 Tick 门控，可能出现一个单位单 Tick 两次成功施法。

### [P2] 事件驱动的被动能力

- [ ] 定义 `FMassAbilityEvent` 或复用已有 Output / Death / Effect 事件队列，表达受击、死亡、Tag 变化、生成等事件。
- [ ] 新建事件消费 Processor，将事件转换为 `QueueAbility` 或受控的 `TryActivate` 请求。
- [ ] 明确事件传播、递归触发上限与同 Tick 重入规则。

**风险：高。** 被动触发链可能形成循环（受击→反击→受击），需要容量预算、最大深度和可观测统计；不能用每 Tick 自动轮询代替事件模型。

### [P2] 目标关系与目标来源分离

- [ ] 若出现治疗、护盾、友军 Buff，新增独立 `TargetRelationPolicy`：`SelfOnly`、`FriendlyOnly`、`HostileOnly`、`AnyValid`。
- [ ] 保持 `TargetPolicy` 只回答“目标从自身、当前目标还是显式请求取得”。
- [ ] 对齐 Mass Output Relation Policy 和 GAS GameplayEffect 的目标应用规则。

**风险：中。** 当前非自身目标的阵营判断偏向敌对目标；未拆分前直接接入治疗技能可能被错误拒绝或绕过统一校验。

### [P3] 统一可观测性与 GAS 桥接诊断

- [ ] 统计并区分自动、显式、事件三种请求来源的尝试数、成功数和失败原因。
- [ ] 在 GAS 桥接日志或调试面板显示：GAS TargetData → Mass Entity → QueueAbility → Output 的关联 ID。
- [ ] 提供单实体技能时间线 Debug，显示 Slot、目标来源、失败原因及冷却。

**风险：低。** 仅增加调试数据；注意调试采样需限额，不能为每个 Mass 实体长期保存完整历史。

## 触发路径约束

```text
输入 / AI / GAS Event / 命中系统
                |
                v
          QueueAbility
                |
                v
UMassAbilityProcessor::FlushRequests
                |
                v
          TryActivate  <--- 自动循环（仅允许自动的能力）
                |
                v
        Output / Effect / GAS Bridge
```

所有路径最终必须收敛到 `TryActivate`，以复用冷却、状态、Tag、关系、距离和输出原子提交规则。
