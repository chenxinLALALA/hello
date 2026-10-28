// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_HelloCameraPunch.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

/**
 * 动画通知：镜头冲击（FOV 收缩 + 屏幕偏移）。
 * 摆在打击帧上，只负责“上膛”——真正命中（伤害结算）时才发冲击，挥空不晃镜头。
 * 只有本地控制的角色才生效。
 */
UCLASS(Blueprintable, meta = (DisplayName = "Hello Camera Punch"))
class HELLO_API UAnimNotify_HelloCameraPunch : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("CameraPunch"); }

	/** FOV 冲击量（正=放大，负=缩小） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera Punch")
	float FOVDelta = -6.f;

	/** 屏幕偏移（屏幕比例，0.1 = 10%）；0 表示只做 FOV 冲击 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera Punch")
	FVector2D ScreenOffset = FVector2D::ZeroVector;

	/** 冲击时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera Punch", meta = (ClampMin = "0.01"))
	float Duration = 0.2f;

	/** 上膛后等待命中的时长（秒）：超时仍没打中则本次不震 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera Punch", meta = (ClampMin = "0"))
	float ConfirmWindow = 0.2f;
};
