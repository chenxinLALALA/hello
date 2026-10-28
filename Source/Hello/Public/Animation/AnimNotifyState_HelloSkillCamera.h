// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Camera/HelloCameraMode_Anchored.h"
#include "AnimNotifyState_HelloSkillCamera.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

/**
 * 动画区间通知：区间内接管镜头（技能特写 / 处决演出）。
 * 默认 Begin 时 Push 一个 Anchored 镜头、End 时 Pop；镜头存活时长完全由动画区间决定，
 * CameraConfig.HoldDuration 会被强制置 0。只对本地控制的角色生效。
 *
 * 勾选 bRequireHit 后改为“命中才生效”：Begin 只上膛，区间内真正命中（伤害结算）时才推入镜头，
 * 没命中则整个区间不产生任何镜头变化。End 时回位，命中太晚可被 MinHoldAfterHit 兜住。
 */
UCLASS(Blueprintable, meta = (DisplayName = "Hello Skill Camera"))
class HELLO_API UAnimNotifyState_HelloSkillCamera : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("SkillCamera"); }

	/** 镜头参数：锚点 Socket / 偏移 / 是否看向角色 / FOV（HoldDuration 无效） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FHelloCameraModeConfig CameraConfig;

	/** 进入镜头的混合时长；<0 用模式默认值（0.25） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	float BlendInTime = -1.f;

	/** 离开镜头的混合时长；<0 用模式默认值（0.35） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	float BlendOutTime = -1.f;

	/** 勾选后：区间内命中才推镜头，空挥整个区间不产生镜头变化 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	bool bRequireHit = false;

	/**
	 * 命中后至少保持的时长（秒）；0 = 完全由区间决定。
	 * 只在 bRequireHit 时有效：命中靠近区间尾巴时，镜头会延续到满足该时长后再回位。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera",
		meta = (EditCondition = "bRequireHit", ClampMin = "0.0"))
	float MinHoldAfterHit = 0.f;
};
