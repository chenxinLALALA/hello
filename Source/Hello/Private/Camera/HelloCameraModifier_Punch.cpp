// Copyright Hello Project. All Rights Reserved.

#include "Camera/HelloCameraModifier_Punch.h"

UHelloCameraModifier_Punch::UHelloCameraModifier_Punch()
{
    // 允许与其他修饰器（如相机震动）同时存在
    bExclusive = false;
}

void UHelloCameraModifier_Punch::Trigger(float InFOVDelta, FVector2D InScreenOffset, float InDuration)
{
    FHelloCameraPunchInstance Punch;
    Punch.FOVDelta = InFOVDelta;
    Punch.ScreenOffset = InScreenOffset;
    Punch.TotalDuration = FMath::Max(InDuration, KINDA_SMALL_NUMBER);
    Punch.RemainingTime = Punch.TotalDuration;

    Punches.Add(Punch);
}

bool UHelloCameraModifier_Punch::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
    bool bAppliedAny = false;

    // 倒序遍历，便于移除已结束的冲击
    for (int32 Index = Punches.Num() - 1; Index >= 0; --Index)
    {
        FHelloCameraPunchInstance& Punch = Punches[Index];

        Punch.RemainingTime -= DeltaTime;
        if (Punch.RemainingTime <= 0.f)
        {
            Punches.RemoveAt(Index);
            continue;
        }

        const float Normalized = FMath::Clamp(Punch.RemainingTime / Punch.TotalDuration, 0.f, 1.f);
        const float Strength = FMath::Pow(Normalized, DecayExponent);

        InOutPOV.FOV += Punch.FOVDelta * Strength;

        // 屏幕偏移通过轻微旋转实现（屏幕空间真实位移需要相机震动或后期，成本更高）
        InOutPOV.Rotation.Yaw += Punch.ScreenOffset.X * Strength * ScreenOffsetDegreesPerUnit;
        InOutPOV.Rotation.Pitch += -Punch.ScreenOffset.Y * Strength * ScreenOffsetDegreesPerUnit;

        bAppliedAny = true;
    }

    return bAppliedAny;
}
