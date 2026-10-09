// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

#include "MassbilityTypes.generated.h"

namespace Massbility
{
	constexpr uint16 InvalidId = MAX_uint16; //Definition、Schema 等尚未注册或找不到时使用
	constexpr int32 MaxExtendedAttributes = 16; //最多 16 个扩展属性槽位，例如护甲魔抗之类
	constexpr int32 RuntimeTagWordCount = 2; //使用两个 uint64
	constexpr int32 MaxRuntimeTags = RuntimeTagWordCount * 64; // 最多支持多少个高频Tag
}

USTRUCT()
struct MASSBILITY_API FMassbilityAttributeHandle
{
	GENERATED_BODY()

    uint16 Index = Massbility::InvalidId;//属性在编译后得到的槽位索引 例如 Attribute.Armor 对应 Idx2。


	bool IsValid() const { return Index != Massbility::InvalidId; }
};

UENUM(BlueprintType, meta = (Bitflags))
enum class EMassbilityStateFlags : uint8
{
	None = 0,
	Dying = 1 << 0,
	Dead = 1 << 1,
	AbilityBlocked = 1 << 2,
	MovementBlocked = 1 << 3,
	Targetable = 1 << 4,
};
ENUM_CLASS_FLAGS(EMassbilityStateFlags);

/** Fixed-size runtime representation of gameplay tags used by Mass hot paths. */
USTRUCT()
struct MASSBILITY_API FMassbilityTagMask
{
	GENERATED_BODY()

	uint64 Words[Massbility::RuntimeTagWordCount] = {};
    
    // 清空Tag位
	void Reset() 
	{
		FMemory::Memzero(Words, sizeof(Words));
	}
    //将某个编译后的 Tag 标记为拥有，范围0~127
	void Set(const uint16 BitIndex)
	{
		if (BitIndex < Massbility::MaxRuntimeTags)
		{
			Words[BitIndex / 64] |= (uint64{1} << (BitIndex % 64));
		}
	}
    //位或 合并 基础 tag、状态 tag 扩充tag等
	void Append(const FMassbilityTagMask& Other)
	{
		for (int32 Index = 0; Index < Massbility::RuntimeTagWordCount; ++Index)
		{
			Words[Index] |= Other.Words[Index];
		}
	}
    //判断当前实体是否包含 Required Mask 的全部 Tag, 后续能力激活条件会使用到
	bool ContainsAll(const FMassbilityTagMask& Required) const
	{
		for (int32 Index = 0; Index < Massbility::RuntimeTagWordCount; ++Index)
		{
			if ((Words[Index] & Required.Words[Index]) != Required.Words[Index])
			{
				return false;
			}
		}
		return true;
	}
    //判断是否命中任意 Tag 后续用于 Blocked Tag 条件 和GAS差不多的
	bool ContainsAny(const FMassbilityTagMask& Other) const
	{
		for (int32 Index = 0; Index < Massbility::RuntimeTagWordCount; ++Index)
		{
			if ((Words[Index] & Other.Words[Index]) != 0)
			{
				return true;
			}
		}
		return false;
	}
};
