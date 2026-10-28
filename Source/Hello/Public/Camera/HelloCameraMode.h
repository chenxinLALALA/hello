// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HelloCameraMode.generated.h"

/**
 * 镜头模式输出的相机视图。
 * 一个 FOV + 一个位置 + 一个朝向。
 */
USTRUCT(BlueprintType)
struct HELLO_API FHelloCameraModeView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    float FieldOfView = 80.f;

    UPROPERTY(BlueprintReadOnly)
    FVector Location = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly)
    FRotator Rotation = FRotator::ZeroRotator;
};

/**
 * 每帧传入镜头模式的上下文。
 * 由相机管理器从当前 ViewTarget 组装。
 */
USTRUCT()
struct HELLO_API FHelloCameraContext
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<AActor> ViewTarget = nullptr;

    UPROPERTY()
    FRotator ControlRotation = FRotator::ZeroRotator;

    UPROPERTY()
    FVector TargetLocation = FVector::ZeroVector;

    UPROPERTY()
    FVector AnchorLocation = FVector::ZeroVector;

    UPROPERTY()
    FRotator AnchorRotation = FRotator::ZeroRotator;

    UPROPERTY()
    float DeltaTime = 0.f;
};

/**
 * 镜头模式基类：纯计算单元，无场景组件开销。
 * 子类在 UpdateView 中根据 FHelloCameraContext 计算理想相机，相机管理器负责栈混合。
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew)
class HELLO_API UHelloCameraMode : public UObject
{
    GENERATED_BODY()

public:
    UHelloCameraMode();

    /** 初始化时由相机管理器设置，用于后续平滑/BlendAlpha 推进 */
    void SetOwner(APlayerCameraManager* InOwner) { Owner = InOwner; bHasLastView = false; }

    /** 推进淡入/淡出进度，返回当前是否还在混合中 */
    void UpdateBlending(float DeltaTime);

    /**
     * 子类实现：每帧在 UpdateBlending 之前调用，用于模式自管状态（计时、到期自动淡出等）。
     * 只会在模式存在于栈内时被调用。
     */
    virtual void TickMode(float DeltaTime) {}

    /** 当前混合权重（0~1，考虑 BlendExponent） */
    float GetBlendWeight() const;

    /** 是否已完全淡出，可以被移除 */
    bool IsFinished() const { return bIsBlendingOut && BlendAlpha <= 0.f; }

    /** 子类实现：计算本模式的理想视图 */
    virtual void UpdateView(const FHelloCameraContext& Ctx, FHelloCameraModeView& OutView) {}

    /** 取当前已平滑过的输出视图 */
    const FHelloCameraModeView& GetCurrentView() const { return CurrentView; }

    /**
     * 相机管理器在每帧 UpdateView 之后、栈合成之前调用。
     * 提交的是本模式自身的输出（不是合成后的 POV），供下一帧平滑使用。
     */
    void CommitView(const FHelloCameraModeView& View);

    /** 标记开始淡出（Pop 时调用） */
    void StartBlendOut();

    /** 是否正在淡出 */
    bool IsBlendingOut() const { return bIsBlendingOut; }

    /** 淡入时长（秒） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Blend", meta = (ClampMin = "0.0"))
    float BlendTime = 0.0f;

    /** 淡出时长（秒） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Blend", meta = (ClampMin = "0.0"))
    float BlendOutTime = 0.0f;

    /** 混合曲线指数，越大后半程越陡 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Blend", meta = (ClampMin = "1.0"))
    float BlendExponent = 4.0f;

    /** 位置平滑速度（0 = 不平滑） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Smooth")
    float ViewSmoothSpeed = 6.0f;

    /** 旋转平滑速度（0 = 不平滑） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Smooth")
    float RotationSmoothSpeed = 10.0f;

protected:
    /** 上帧输出是否有效。用显式状态代替“零值 = 未初始化”的哨兵判断 */
    bool HasValidLastView() const { return bHasLastView; }

    /** 内部记录的上帧输出，用于子类做滞后平滑 */
    UPROPERTY()
    FHelloCameraModeView LastView;

    /** 对位置做指数衰减平滑（帧率无关） */
    FVector SmoothLocation(const FVector& Target, float DeltaTime, float Speed) const;

    /** 对旋转做 Slerp/短路径平滑，Pitch 钳制（帧率无关） */
    FRotator SmoothRotation(const FRotator& Target, float DeltaTime, float Speed) const;

private:
    UPROPERTY()
    TObjectPtr<APlayerCameraManager> Owner = nullptr;

    float BlendAlpha = 0.f;
    bool bIsBlendingOut = false;

    /** 是否已有可用的 LastView；首次帧/输出含 NaN 时为 false */
    bool bHasLastView = false;

    UPROPERTY()
    FHelloCameraModeView CurrentView;
};
