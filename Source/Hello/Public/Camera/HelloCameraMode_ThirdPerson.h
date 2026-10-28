// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/HelloCameraMode.h"
#include "HelloCameraMode_ThirdPerson.generated.h"

/**
 * 默认第三人称跟随镜头。
 * 弹簧臂 + 位置/旋转滞后 + 穿透规避（ECC_Camera 球形探测 + BlendIn/Out 拉近）。
 */
UCLASS()
class HELLO_API UHelloCameraMode_ThirdPerson : public UHelloCameraMode
{
    GENERATED_BODY()

public:
    UHelloCameraMode_ThirdPerson();

    virtual void UpdateView(const FHelloCameraContext& Ctx, FHelloCameraModeView& OutView) override;

private:
    /**
     * 阻挡规避：从关注点朝理想机位做球形探测，命中则沿视线拉近。
     * 返回已含穿透系数的目标位置（仍未做位置滞后）。
     */
    FVector ResolvePenetration(const FHelloCameraContext& Ctx, const FVector& FocusLocation,
        const FVector& IdealLocation, float DeltaTime);

    /** 相对角色锚点的目标偏移 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson")
    FVector TargetOffset = FVector(0.f, 0.f, 90.f);

    /** 弹簧臂长 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson", meta = (ClampMin = "0.0"))
    float ArmLength = 400.f;

    /** 是否开启位置滞后 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson")
    bool bEnableCameraLag = true;

    /** 位置滞后速度 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson", meta = (ClampMin = "0.0"))
    float CameraLagSpeed = 8.f;

    /** 是否开启旋转滞后 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson")
    bool bEnableRotationLag = true;

    /** 旋转滞后速度 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson", meta = (ClampMin = "0.0"))
    float CameraRotationLagSpeed = 12.f;

    /** 基础 FOV */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson", meta = (ClampMin = "1.0"))
    float FieldOfView = 80.f;

    // ---- 阻挡规避（阶段 3） ----

    /** 是否开启阻挡规避（关闭后为纯弹簧臂，便于对比调试） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson|Penetration")
    bool bEnablePenetrationAvoidance = true;

    /** 进入阻挡时的拉近平滑时长（秒，小→反应快） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson|Penetration", meta = (ClampMin = "0.01"))
    float PenetrationBlendInTime = 0.1f;

    /** 障碍移除后的回位平滑时长（秒，大→回位柔和） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson|Penetration", meta = (ClampMin = "0.01"))
    float PenetrationBlendOutTime = 0.25f;

    /** 相机距障碍表面额外保留的距离（球半径之外） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson|Penetration", meta = (ClampMin = "0.0"))
    float SafeZoneRadius = 30.f;

    /** 探测球半径（比线更抗穿缝） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|ThirdPerson|Penetration", meta = (ClampMin = "0.0"))
    float TraceRadius = 10.f;

    /** 穿透系数：0 = 完全按理想机位，1 = 完全按命中点拉近 */
    float PenetrationAlpha = 0.f;
};
