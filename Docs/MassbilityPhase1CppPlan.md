# Massbility Phase 1：C++ 落地方案

## 1. 目标和范围

Phase 1 只建立普通 Mass 单位的基础数据层：单位定义、组合式 Attribute Set 与 Schema、紧凑属性槽位、Mass Runtime Tag Catalog、可配置的 Skyra 状态 Tag 映射，以及创建单位所需的世界子系统。

本阶段不实现 Ability、MassGE、周期效果、伤害公式、空间查询、表现、GAS Bridge 或网络同步。这些内容依赖 Phase 1 的 ID、Handle、Fragment 和配置编译结果。

## 2. 文件布局

```text
Source/Massbility
├── Public
│   ├── Core
│   │   ├── MassbilityTypes.h
│   │   ├── MassbilitySettings.h
│   │   └── MassbilitySubsystem.h
│   ├── Tags
│   │   ├── MassbilityRuntimeTagCatalog.h
│   │   └── MassbilityStateTagMapping.h
│   ├── Attributes
│   │   └── MassbilityAttributeSchema.h
│   └── Entity
│       ├── MassbilityUnitDefinition.h
│       ├── MassbilityFragments.h
│       └── MassbilityEntityFactory.h
└── Private
    ├── Core
    │   ├── MassbilitySettings.cpp
    │   └── MassbilitySubsystem.cpp
    ├── Tags
    │   ├── MassbilityRuntimeTagCatalog.cpp
    │   └── MassbilityStateTagMapping.cpp
    ├── Attributes
    │   └── MassbilityAttributeSchema.cpp
    └── Entity
        ├── MassbilityUnitDefinition.cpp
        └── MassbilityEntityFactory.cpp
```

`MassbilityTypes.h` 与 `MassbilityFragments.h` 是热路径头文件。它们不可引入 GAS 头文件、Skyra 头文件、`FGameplayTagContainer`、`TMap` 或动态 `TArray` 成员。

## 3. 基础类型：MassbilityTypes.h

该文件定义稳定 ID、容量和高频位标志。

```cpp
namespace Massbility
{
    constexpr uint16 InvalidId = MAX_uint16;
    constexpr int32 MaxExtendedAttributes = 16;
    constexpr int32 RuntimeTagWordCount = 2; // 128 个热路径 Tag
}

struct FMassbilityAttributeHandle
{
    uint16 Index = Massbility::InvalidId;
    bool IsValid() const;
};

enum class EMassbilityStateFlags : uint32
{
    None            = 0,
    Dying           = 1 << 0,
    Dead            = 1 << 1,
    AbilityBlocked  = 1 << 2,
    MovementBlocked = 1 << 3,
    Targetable      = 1 << 4,
};

struct FMassbilityTagMask
{
    uint64 Words[Massbility::RuntimeTagWordCount] = {};
};
```

`StateFlags` 只保留处理器需要以位判断的固定系统语义。`AbilityBlocked` 表示此刻不能执行 Massbility 能力；眩晕、冻结、沉默等具体原因由 Runtime Tag 或后续效果系统表达。燃烧、中毒、元素免疫、狂暴等可扩展玩法状态也不加入该枚举，而是进入 Runtime Tag Mask。

## 4. 属性 Schema：MassbilityAttributeSchema

作者数据：

```cpp
USTRUCT(BlueprintType)
struct FMassbilityAttributeDefinition
{
    FGameplayTag AttributeTag;
    float DefaultValue;
    float MinValue;
    float MaxValue;
};

UCLASS(BlueprintType)
class UMassbilityAttributeSet : public UPrimaryDataAsset
{
    TArray<FMassbilityAttributeDefinition> Attributes;
};

UCLASS(BlueprintType)
class UMassbilityAttributeSchema : public UPrimaryDataAsset
{
    TArray<TObjectPtr<UMassbilityAttributeSet>> AttributeSets;
};
```

运行时编译结果不作为 UPROPERTY Fragment 数据保存：

```cpp
struct FMassbilityCompiledAttributeSchema
{
    uint16 SchemaId;
    uint8 AttributeCount;
    TStaticArray<FMassbilityCompiledAttribute, Massbility::MaxExtendedAttributes> Attributes;
};
```

编译器按 AttributeSets 数组顺序展平定义，校验 Set 有效、Tag 无重复、上下限合法、最终属性数不超过 `MaxExtendedAttributes`。Attribute Set 是作者侧组合单元；编译完成后，实体不持有任何 Set 指针。`FGameplayTag -> FMassbilityAttributeHandle` 的查找表只存在于 Subsystem Registry，用于初始化、编辑器校验和低频 API；Processor 接收已解析 Handle，不在热路径查表。

Health、MaxHealth、MoveSpeed 放在专用 Fragment；Schema 管理扩展属性，例如 Armor、FireResistance 和 StaggerResistance。这样 Armor 的增加不需要修改核心枚举或 Fragment 布局。

## 5. Runtime Tag Catalog：MassbilityRuntimeTagCatalog

`UMassbilityRuntimeTagCatalog` 是数据资产，包含项目允许进入 Mass 热路径的 Gameplay Tag。每个 Tag 在编译时获取固定 Bit Index；Catalog 总容量固定为 128。

```cpp
UCLASS(BlueprintType)
class UMassbilityRuntimeTagCatalog : public UDataAsset
{
    TArray<FGameplayTag> RuntimeTags;
};
```

编译时需要：

1. 校验所有 Tag 有效、不重复，数量不超过 128。
2. 对每个登记 Tag 生成索引。
3. 展开父 Tag：登记 `Status.Death.Dead` 时，同时使 `Status.Death` 的匹配语义可用。
4. 编译 Ability/Effect 的 Required、Blocked、Granted Tag 为 `FMassbilityTagMask`。

`FMassbilityTagMask` 是 Fragment 可直接保存的固定宽度结构。设置、清除和匹配只操作 `uint64`；禁止在普通实体上保存 `FGameplayTagContainer`。

## 6. Skyra 兼容状态映射：MassbilityStateTagMapping

`UMassbilityStateTagMapping` 是可编辑数据资产。它不依赖 Skyra C++ 模块，只使用 `FGameplayTag` 属性，因此其他项目也可替换 Tag 命名。

```cpp
USTRUCT(BlueprintType)
struct FMassbilityStateTagBinding
{
    EMassbilityStateFlags StateFlag;
    TArray<FGameplayTag> GrantedTags;
};

UCLASS(BlueprintType)
class UMassbilityStateTagMapping : public UDataAsset
{
    TArray<FMassbilityStateTagBinding> Bindings;
};
```

默认资产使用：

```text
Dying      -> Status.Death.Dying
Dead       -> Status.Death.Dead
```

初始化时，Binding 被编译为 `StateFlag -> FMassbilityTagMask`。设置状态 Flag 时，Entity Factory 或后续 State API 将预编译 Mask 合并进实体 RuntimeTagMask。死亡等一次性事件属于后续 Combat/Event 系统，不在 Phase 1 状态映射中配置。

## 7. 单位定义与 Fragment

### 7.1 Unit Definition

`UMassbilityUnitDefinition` 是共享数据资产。它引用 Attribute Schema，并定义初始生命、移速、阵营、静态身份 Tag 和初始 Runtime Tag。Phase 1 不放 Ability 或 Effect 资产。

```cpp
UCLASS(BlueprintType)
class UMassbilityUnitDefinition : public UPrimaryDataAsset
{
    TObjectPtr<UMassbilityAttributeSchema> AttributeSchema;
    float InitialHealth;
    float InitialMaxHealth;
    float InitialMoveSpeed;
    int16 TeamId;
    FGameplayTagContainer StaticIdentityTags; // 仅资产数据，不进入实体 Fragment
    FGameplayTagContainer InitialRuntimeTags; // Spawn 时编译为 TagMask
};
```

### 7.2 Fragments

```cpp
USTRUCT()
struct FMassbilityUnitFragment : public FMassFragment
{
    uint16 UnitDefinitionId = Massbility::InvalidId;
    uint16 AttributeSchemaId = Massbility::InvalidId;
    int16 TeamId = 0;
};

USTRUCT()
struct FMassbilityVitalFragment : public FMassFragment
{
    float Health = 1.0f;
    float MaxHealth = 1.0f;
    float MoveSpeed = 0.0f;
};

USTRUCT()
struct FMassbilityAttributeFragment : public FMassFragment
{
    uint8 AttributeCount = 0;
    TStaticArray<float, Massbility::MaxExtendedAttributes> Values = {};
};

USTRUCT()
struct FMassbilityStateFragment : public FMassFragment
{
    EMassbilityStateFlags StateFlags = EMassbilityStateFlags::Targetable;
    FMassbilityTagMask RuntimeTags;
};
```

没有 Fragment 包含 `FGameplayTagContainer`、`TMap`、`TArray` 或 UObject 指针。静态身份数据属于单位定义或 Shared Fragment；热路径 Fragment 仅保存 ID 与 POD 数据。

## 8. World Subsystem 与 Entity Factory

`UMassbilitySubsystem : UWorldSubsystem` 是 Phase 1 的唯一世界级入口。它依赖同一 World 的 `UMassEntitySubsystem`，在世界初始化时加载设置资产并建立三个 Registry：

```text
UnitDefinition Registry
AttributeSchema Registry
RuntimeTagCatalog Registry
```

`FMassbilityEntityFactory` 是非 UObject 的内部辅助类，负责：

1. 解析 `UMassbilityUnitDefinition`、Schema 和初始 Tag。
2. 用 `FMassEntityManager` 创建包含四个基础 Fragment 的实体 Archetype。
3. 初始化 Vital、扩展属性槽、StateFlags 与 RuntimeTagMask。
4. 返回 `FMassEntityHandle` 或后续暴露的轻量包装 Handle。

Subsystem 还提供低频 C++ API：`CreateUnit`、`DestroyUnit`、`GetAttributeHandle`、`GetAttribute`、`SetAttribute`、`AddAttribute`、`SetStateFlags` 与 `HasRuntimeTags`。这些 API 可在初始化、调试和外部系统中解析 Tag；Processor 不调用 Tag 查找 API。

## 9. 初始化顺序

```text
World 创建
  -> UMassbilitySubsystem::Initialize
  -> 获取 UMassEntitySubsystem
  -> 加载 MassbilitySettings
  -> 编译 RuntimeTagCatalog
  -> 编译 StateTagMapping
  -> 编译并注册 Attribute Schemas
  -> 注册 Unit Definitions
  -> Entity Factory 可创建单位
```

任何 Schema、Catalog 或 Mapping 校验失败时，不创建对应单位，并在 Editor 报告具体资产和 Tag。运行时不尝试动态扩容 Tag Mask 或扩展属性槽。

## 10. Phase 1 实施顺序

1. 添加 `MassbilityTypes.h` 和基础 Module 日志类别。
2. 添加 Attribute Set、组合式 Attribute Schema 与纯 C++ 编译结果；完成 Set 有效性、重复和容量校验。
3. 添加 Runtime Tag Catalog、TagMask 与 Tag 条件编译。
4. 添加 State Tag Mapping，并制作默认 Skyra 兼容资产。
5. 添加 Unit Definition 与四个基础 Fragment。
6. 添加 `UMassbilitySettings`、`UMassbilitySubsystem` 与 Entity Factory。
7. 添加基础属性和状态 Flag API。
8. 添加自动化测试：Schema 组合、Armor Handle、Tag 父级匹配、Skyra 死亡映射、实体初始化。

## 11. 验收标准

- 可用 Unit Definition 创建 Walker、Runner 和 Brute 三种 Mass 实体。
- Brute Schema 可增加 `Attribute.Armor`，无需修改核心属性枚举或 Fragment。
- `Status.Death.Dying`、`Status.Death.Dead` 默认映射为 Mass 状态；项目资产可替换映射。
- 自定义 Runtime Tag 可参与位掩码条件判断，并保留父 Tag 匹配语义。
- 所有基础 Fragment 保持固定大小、无动态分配、无 Tag Container、无 UObject 指针。
- 在单位创建之后，属性与 Tag 高频查询只访问 Handle、POD Fragment 和 TagMask。
