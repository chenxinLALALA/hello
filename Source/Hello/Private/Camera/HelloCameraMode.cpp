// Copyright Hello Project. All Rights Reserved.

#include "Camera/HelloCameraMode.h"
#include "Camera/PlayerCameraManager.h"

UHelloCameraMode::UHelloCameraMode()
{
}

void UHelloCameraMode::UpdateBlending(float DeltaTime)
{
    const float TargetAlpha = bIsBlendingOut ? 0.f : 1.f;
    const float Duration = bIsBlendingOut ? FMath::Max(BlendOutTime, KINDA_SMALL_NUMBER) : FMath::Max(BlendTime, KINDA_SMALL_NUMBER);

    const float DeltaAlpha = DeltaTime / Duration;
    if (bIsBlendingOut)
    {
        BlendAlpha = FMath::Clamp(BlendAlpha - DeltaAlpha, 0.f, 1.f);
    }
    else
    {
        BlendAlpha = FMath::Clamp(BlendAlpha + DeltaAlpha, 0.f, 1.f);
    }
}

float UHelloCameraMode::GetBlendWeight() const
{
    return FMath::Pow(FMath::Clamp(BlendAlpha, 0.f, 1.f), BlendExponent);
}

void UHelloCameraMode::StartBlendOut()
{
    if (!bIsBlendingOut)
    {
        bIsBlendingOut = true;
        // 保持当前 Alpha，从当前权重开始淡出（而不是重置）
    }
}

void UHelloCameraMode::CommitView(const FHelloCameraModeView& View)
{
    // 含 NaN 的输出不写入历史：一旦写入，后续帧的插值会被永久污染
    if (View.Location.ContainsNaN() || View.Rotation.ContainsNaN())
    {
        bHasLastView = false;
        return;
    }

    CurrentView = View;
    LastView = View;
    bHasLastView = true;
}

FVector UHelloCameraMode::SmoothLocation(const FVector& Target, float DeltaTime, float Speed) const
{
    if (!bHasLastView || Speed <= 0.f)
    {
        return Target;
    }

    // 指数衰减（帧率无关）：Alpha = 1 - e^(-Speed * Dt)，对应 FInterpTo 语义
    const float Alpha = FMath::Clamp(1.f - FMath::Exp(-Speed * FMath::Max(DeltaTime, 0.f)), 0.f, 1.f);
    return FMath::Lerp(LastView.Location, Target, Alpha);
}

FRotator UHelloCameraMode::SmoothRotation(const FRotator& Target, float DeltaTime, float Speed) const
{
    if (!bHasLastView || Speed <= 0.f)
    {
        return Target;
    }

    const FQuat FromQuat = LastView.Rotation.Quaternion();
    const FQuat ToQuat = Target.Quaternion();

    // 指数衰减（帧率无关）：Alpha = 1 - e^(-Speed * Dt)
    const float Alpha = FMath::Clamp(1.f - FMath::Exp(-Speed * FMath::Max(DeltaTime, 0.f)), 0.f, 1.f);

    FQuat ResultQuat = FQuat::Slerp(FromQuat, ToQuat, Alpha);
    ResultQuat.Normalize();

    FRotator Result = ResultQuat.Rotator();
    Result.Pitch = FMath::ClampAngle(Result.Pitch, -89.9f, 89.9f);
    Result.Yaw = FMath::UnwindDegrees(Result.Yaw);
    return Result;
}
