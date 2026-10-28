// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "HelloCameraModifier_Punch.generated.h"

/** 一次未结束的冲击 */
USTRUCT()
struct HELLO_API FHelloCameraPunchInstance
{
    GENERATED_BODY()

    /** FOV 冲击量（正=放大，负=缩小） */
    float FOVDelta = 0.f;

    /** 屏幕偏移（单位：屏幕比例，0.1 = 10%） */
    FVector2D ScreenOffset = FVector2D::ZeroVector;

    /** 剩余时长 */
    float RemainingTime = 0.f;

    /** 总时长 */
    float TotalDuration = 0.f;
};

/**
 * 轻技能/连段命中用的 FOV 冲击 + 屏幕偏移叠加。
 * 内部维护冲击列表，多次触发可并存、各自独立衰减，不会互相覆盖。
 */
UCLASS()
class HELLO_API UHelloCameraModifier_Punch : public UCameraModifier
{
    GENERATED_BODY()

public:
    UHelloCameraModifier_Punch();

    /** 追加一次冲击；多个冲击可同时存在 */
    UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
    void Trigger(float InFOVDelta, FVector2D InScreenOffset, float InDuration);

    /** 是否还有未结束的冲击（全部结束后管理器应关闭本 modifier） */
    bool IsActive() const { return Punches.Num() > 0; }

    virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

protected:
    /** 衰减曲线指数，越大越快收敛 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Punch")
    float DecayExponent = 4.0f;

    /** 屏幕偏移到旋转角度的换算系数（0.1 屏幕比例 ≈ 0.5 度） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Punch")
    float ScreenOffsetDegreesPerUnit = 5.0f;

    /** 当前存活的冲击列表 */
    TArray<FHelloCameraPunchInstance> Punches;
};
