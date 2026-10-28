// Copyright Hello Project. All Rights Reserved.

#include "Camera/HelloCameraMode_ThirdPerson.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "GameFramework/Actor.h"

UHelloCameraMode_ThirdPerson::UHelloCameraMode_ThirdPerson()
{
    // 默认态切换希望快速但平稳，BlendTime 给 0.15s
    BlendTime = 0.15f;
    BlendOutTime = 0.15f;
}

void UHelloCameraMode_ThirdPerson::UpdateView(const FHelloCameraContext& Ctx, FHelloCameraModeView& OutView)
{
    // 1. 确定关注点（腰部/胸口高度）与目标朝向
    //    注意：Ctx.AnchorLocation 是 Mesh 组件原点，通常在脚底；直接拿它做探测起点会扫到地面。
    const FVector FocusLocation = Ctx.AnchorLocation + TargetOffset;
    const FRotator TargetRotation = Ctx.ControlRotation;

    // 2. 根据弹簧臂长度计算理想相机位置
    const FVector CamForward = TargetRotation.Vector();
    const FVector IdealLocation = FocusLocation - CamForward * ArmLength;

    // 3. 阻挡规避：理想机位 → 探测 → 拉近（阶段 3）
    FVector DesiredLocation = IdealLocation;
    if (bEnablePenetrationAvoidance)
    {
        DesiredLocation = ResolvePenetration(Ctx, FocusLocation, IdealLocation, Ctx.DeltaTime);
    }

    // 4. 位置滞后：走基类统一平滑，起点为上帧由 CommitView 提交的输出
    FVector SmoothedLocation = DesiredLocation;
    if (bEnableCameraLag)
    {
        SmoothedLocation = SmoothLocation(DesiredLocation, Ctx.DeltaTime, CameraLagSpeed);
    }

    // 5. 旋转滞后：同样复用基类平滑（Slerp 短路径 + Pitch 钳制）
    FRotator SmoothedRotation = TargetRotation;
    if (bEnableRotationLag)
    {
        SmoothedRotation = SmoothRotation(TargetRotation, Ctx.DeltaTime, CameraRotationLagSpeed);
    }

    // 6. 输出
    OutView.FieldOfView = FieldOfView;
    OutView.Location = SmoothedLocation;
    OutView.Rotation = SmoothedRotation;
}

FVector UHelloCameraMode_ThirdPerson::ResolvePenetration(const FHelloCameraContext& Ctx,
    const FVector& FocusLocation, const FVector& IdealLocation, float DeltaTime)
{
    if (!Ctx.ViewTarget)
    {
        return IdealLocation;
    }

    UWorld* World = Ctx.ViewTarget->GetWorld();
    if (!World)
    {
        return IdealLocation;
    }

    const FVector ToIdeal = IdealLocation - FocusLocation;
    const float FullDistance = ToIdeal.Size();
    if (FullDistance <= KINDA_SMALL_NUMBER)
    {
        return IdealLocation;
    }

    const FVector TraceDir = ToIdeal / FullDistance;

    // 只忽略当前 ViewTarget（其 Capsule/Mesh 都挂在它身上），否则每帧必被自己挡住 → 相机贴脸
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HelloCameraPenetration), false);
    Params.AddIgnoredActor(Ctx.ViewTarget);

    FHitResult Hit;
    const bool bHit = World->SweepSingleByChannel(
        Hit,
        FocusLocation,
        IdealLocation,
        FQuat::Identity,
        ECC_Camera,
        FCollisionShape::MakeSphere(TraceRadius),
        Params);

    // 命中时把相机拉回到障碍表面的角色侧，并保留 SafeZoneRadius 间隙。
    // 注意用 Hit.ImpactPoint（真实表面接触点）而非 Hit.Location（球心落点，
    // 球扫会在离表面还有 TraceRadius 的地方停下，直接拿它算会把相机推到墙后）。
    FVector PulledLocation = IdealLocation;
    if (bHit)
    {
        const float SurfaceDistance = FVector::DotProduct(Hit.ImpactPoint - FocusLocation, TraceDir);
        const float PulledDistance = FMath::Clamp(SurfaceDistance - SafeZoneRadius, 0.f, FullDistance);
        PulledLocation = FocusLocation + TraceDir * PulledDistance;
    }

    // 穿透系数按帧率无关的指数曲线逼近目标（时间常数 = BlendIn / BlendOut）
    const float TargetPenetration = bHit ? 1.f : 0.f;
    const float PenetrationBlendTime = FMath::Max(bHit ? PenetrationBlendInTime : PenetrationBlendOutTime, KINDA_SMALL_NUMBER);

    if (!HasValidLastView())
    {
        // 首帧 / 模式重建：直接对齐当前状态，避免从 0 起步产生一次假回位
        PenetrationAlpha = TargetPenetration;
    }
    else
    {
        const float Alpha = 1.f - FMath::Exp(-FMath::Max(DeltaTime, 0.f) / PenetrationBlendTime);
        PenetrationAlpha = FMath::Lerp(PenetrationAlpha, TargetPenetration, Alpha);
    }

    return FMath::Lerp(IdealLocation, PulledLocation, PenetrationAlpha);
}
