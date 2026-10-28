// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/HelloCameraMode.h"
#include "HelloCameraMode_Anchored.generated.h"

/**
 * 技能镜头的运行时配置。
 * 由 GA 在 PushCameraMode 时传入，不直接是 EditDefaultsOnly 资产字段。
 */
USTRUCT(BlueprintType)
struct HELLO_API FHelloCameraModeConfig
{
    GENERATED_BODY()

    /** 角色 Mesh 上的 Socket；空则使用角色 Mesh 根变换 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    FName AnchorSocket = NAME_None;

    /** 相对 Socket/根的偏移（位置 + 旋转），与 Socket 变换做乘法组合 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    FTransform AnchorOffset = FTransform::Identity;

    /** 是否强制看向角色 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    bool bLookAtCharacter = false;

    /** 看向的角色 Socket；空则回退到头部（DefaultLookAtSocket） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (EditCondition = "bLookAtCharacter"))
    FName LookAtSocket = NAME_None;

    /** 覆盖 FOV；<= 0 表示使用模式默认 FOV */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
    float FieldOfView = 0.f;

    /**
     * 是否在镜头开始时锁存锚点变换。
     * 开启后镜头不再逐帧跟随骨骼（避免呼吸/挥刀导致的晃动），适合固定机位特写；
     * 关闭时逐帧读 Socket，适合“手持跟拍”式演出。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    bool bFreezeAnchorOnStart = true;

    /** 位置平滑速度覆盖；<= 0 用模式默认（ViewSmoothSpeed = 6）。越大越跟手、越小越稳 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
    float ViewSmoothSpeedOverride = 0.f;

    /** 旋转平滑速度覆盖；<= 0 用模式默认（RotationSmoothSpeed = 10） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
    float RotationSmoothSpeedOverride = 0.f;

    /** 保持时长（仅 GA 直推路径有效；动画通知会强制置 0，由区间控制） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
    float HoldDuration = 0.f;
};

/**
 * 技能镜头：锚定到角色 Socket 或世界偏移，可选看向角色，可选 Hold 时长到期自动淡出。
 */
UCLASS()
class HELLO_API UHelloCameraMode_Anchored : public UHelloCameraMode
{
    GENERATED_BODY()

public:
    UHelloCameraMode_Anchored();

    /** GA / 动画通知 Push 时写入配置（同时重置锚点锁存与平滑覆盖） */
    void SetConfig(const FHelloCameraModeConfig& InConfig);

    /** 每帧钩子：HoldDuration 计时，到期自动 StartBlendOut（幂等，与 GA 手动 Pop 不冲突） */
    virtual void TickMode(float DeltaTime) override;

    virtual void UpdateView(const FHelloCameraContext& Ctx, FHelloCameraModeView& OutView) override;

protected:
    /** 解析锚点世界变换：Config.AnchorSocket → 默认 Socket → Mesh 组件变换 → Context 锚点 */
    FTransform ResolveAnchorTransform(const FHelloCameraContext& Ctx) const;

    /** 解析看向目标点：Config.LookAtSocket → 默认 Socket → 锚点上方 */
    FVector ResolveLookAtTarget(const FHelloCameraContext& Ctx) const;

    UPROPERTY()
    FHelloCameraModeConfig Config;

    /** 未配置 Socket 时使用的默认锚点 Socket（空 = 用 Mesh 组件变换） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Anchored")
    FName DefaultAnchorSocket = NAME_None;

    /** 看向角色时默认使用的 Socket（空或不存在时回退到锚点上方） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Anchored")
    FName DefaultLookAtSocket = TEXT("head");

    /** 配置未指定 FOV 时使用的默认 FOV */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Anchored", meta = (ClampMin = "1.0"))
    float FieldOfView = 80.f;

    /** 已存活时长（从 Push 开始计） */
    float ElapsedTime = 0.f;

    /** Hold 是否已到期（到期只触发一次 StartBlendOut） */
    bool bHoldExpired = false;

    /** Socket 缺失只警告一次（每帧警告会刷屏），调试用 */
    mutable bool bWarnedMissingSocket = false;

    /** LookAt Socket 缺失只警告一次 */
    mutable bool bWarnedMissingLookAtSocket = false;

    /** 锁存模式下的锚点变换（bFreezeAnchorOnStart 时使用） */
    FTransform FrozenAnchorTransform = FTransform::Identity;

    /** 锁存模式下的看向目标（避免镜头持续追着呼吸/摆头的骨骼抖） */
    FVector FrozenLookAtTarget = FVector::ZeroVector;

    /** 锚点是否已经锁存 */
    bool bAnchorFrozen = false;
};
