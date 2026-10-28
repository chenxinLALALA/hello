// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComboConfigDataAsset.generated.h"

class UAnimSequence;
class UGameplayEffect;

/**
 * 单段连段的数据。
 */
USTRUCT(BlueprintType)
struct FComboSegmentData
{
	GENERATED_BODY()

	/** 该段动画（AnimationSequence） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	UAnimSequence* SegmentSequence = nullptr;

	/** 该段伤害 GE（可为空，无伤害） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	TSubclassOf<UGameplayEffect> DamageGE = nullptr;

	/** 该段的窗口长度（秒）—— 覆盖 GA 的默认窗口 GE */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = 0.0f))
	float WindowDuration = 0.8f;

	/** 该段的宽容期长度（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = 0.0f))
	float GraceDuration = 0.15f;
};

/**
 * 角色连段配置 DataAsset。
 * 每个角色挂一个此 DataAsset，配置 5 段平A的动画/伤害/窗口时长。
 */
UCLASS()
class HELLO_API UComboConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 连段段数（固定 5 段） */
	static constexpr int32 MaxComboSegments = 5;

	/** 各段连段配置（索引 0 = 第 1 段，索引 4 = 第 5 段） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (EditFixedSize, FixedSize = 5))
	TArray<FComboSegmentData> Segments;

	/** 默认窗口长度（每段没配置时用此值） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = 0.0f))
	float DefaultWindowDuration = 0.8f;

	/** 默认宽容期长度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = 0.0f))
	float DefaultGraceDuration = 0.15f;

	/** 获取指定段的数据（段号 1~5，越界返回无效数据） */
	const FComboSegmentData& GetSegment(int32 InSegmentIndex) const;
};