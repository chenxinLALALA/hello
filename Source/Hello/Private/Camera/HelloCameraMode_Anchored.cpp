// Copyright Hello Project. All Rights Reserved.

#include "Camera/HelloCameraMode_Anchored.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"

UHelloCameraMode_Anchored::UHelloCameraMode_Anchored()
{
    // 技能镜头希望有较明显的演出感
    BlendTime = 0.25f;
    BlendOutTime = 0.35f;
}

void UHelloCameraMode_Anchored::SetConfig(const FHelloCameraModeConfig& InConfig)
{
    Config = InConfig;

    // 新一次 Push 要重新锁存锚点
    bAnchorFrozen = false;
    FrozenAnchorTransform = FTransform::Identity;
    FrozenLookAtTarget = FVector::ZeroVector;

    // 配置里给了平滑覆盖就应用（<=0 用类默认）
    if (Config.ViewSmoothSpeedOverride > 0.f)
    {
        ViewSmoothSpeed = Config.ViewSmoothSpeedOverride;
    }
    if (Config.RotationSmoothSpeedOverride > 0.f)
    {
        RotationSmoothSpeed = Config.RotationSmoothSpeedOverride;
    }
}

void UHelloCameraMode_Anchored::TickMode(float DeltaTime)
{
    // 已到期或正在淡出：不重复计时/触发
    if (bHoldExpired || IsBlendingOut() || Config.HoldDuration <= 0.f)
    {
        return;
    }

    ElapsedTime += FMath::Max(DeltaTime, 0.f);
    if (ElapsedTime >= Config.HoldDuration)
    {
        bHoldExpired = true;
        // 到期自动淡出；GA 侧的手动 Pop 也走同一入口，StartBlendOut 幂等
        StartBlendOut();
        UE_LOG(LogTemp, Log, TEXT("[HelloCamera] Anchored hold expired after %.2fs"), ElapsedTime);
    }
}

FTransform UHelloCameraMode_Anchored::ResolveAnchorTransform(const FHelloCameraContext& Ctx) const
{
    const ACharacter* ViewChar = Cast<ACharacter>(Ctx.ViewTarget.Get());
    const USkeletalMeshComponent* Mesh = ViewChar ? ViewChar->GetMesh() : nullptr;

    if (Mesh)
    {
        const FName SocketName = Config.AnchorSocket.IsNone() ? DefaultAnchorSocket : Config.AnchorSocket;
        if (!SocketName.IsNone())
        {
            if (Mesh->DoesSocketExist(SocketName))
            {
                return Mesh->GetSocketTransform(SocketName, RTS_World);
            }

            if (!bWarnedMissingSocket)
            {
                bWarnedMissingSocket = true;
                UE_LOG(LogTemp, Warning, TEXT("[HelloCamera] Anchor socket '%s' 不存在，回退到 Mesh 变换（相机可能落在脚底）"), *SocketName.ToString());
            }
        }

        // Socket 未配置/不存在：退回 Mesh 组件变换（注意其原点通常在脚底，需靠 AnchorOffset 抬到目标高度）
        return Mesh->GetComponentTransform();
    }

    // 没有 Mesh：退回管理器给的锚点（角色根/胶囊位置）
    return FTransform(Ctx.AnchorRotation, Ctx.AnchorLocation);
}

FVector UHelloCameraMode_Anchored::ResolveLookAtTarget(const FHelloCameraContext& Ctx) const
{
    const ACharacter* ViewChar = Cast<ACharacter>(Ctx.ViewTarget.Get());
    const USkeletalMeshComponent* Mesh = ViewChar ? ViewChar->GetMesh() : nullptr;

    if (Mesh)
    {
        const FName SocketName = Config.LookAtSocket.IsNone() ? DefaultLookAtSocket : Config.LookAtSocket;
        if (!SocketName.IsNone())
        {
            if (Mesh->DoesSocketExist(SocketName))
            {
                return Mesh->GetSocketTransform(SocketName, RTS_World).GetLocation();
            }

            if (!bWarnedMissingLookAtSocket)
            {
                bWarnedMissingLookAtSocket = true;
                UE_LOG(LogTemp, Warning, TEXT("[HelloCamera] LookAt socket '%s' 不存在，回退到锚点上方 90"), *SocketName.ToString());
            }
        }
    }

    // 兜底：锚点上方 90（大致胸口/头部高度）
    return Ctx.AnchorLocation + FVector(0.f, 0.f, 90.f);
}

void UHelloCameraMode_Anchored::UpdateView(const FHelloCameraContext& Ctx, FHelloCameraModeView& OutView)
{
    // 0. 首次更新时锁存锚点与看向目标（避免逐帧跟随骨骼导致画面抖）
    const bool bFreeze = Config.bFreezeAnchorOnStart;
    if (bFreeze && !bAnchorFrozen)
    {
        FrozenAnchorTransform = ResolveAnchorTransform(Ctx);
        FrozenLookAtTarget = ResolveLookAtTarget(Ctx);
        bAnchorFrozen = true;
    }

    // 1. Socket 变换与偏移做乘法组合：偏移是相对 Socket 的，而不是世界空间加减
    const FTransform BaseAnchor = bFreeze ? FrozenAnchorTransform : ResolveAnchorTransform(Ctx);
    const FTransform AnchorTransform = BaseAnchor * Config.AnchorOffset;

    const FVector DesiredLocation = AnchorTransform.GetLocation();
    FRotator DesiredRotation = AnchorTransform.Rotator();

    // 2. 可选看向角色：覆盖偏移带来的朝向
    if (Config.bLookAtCharacter)
    {
        const FVector LookAtTarget = bFreeze ? FrozenLookAtTarget : ResolveLookAtTarget(Ctx);
        const FVector ToTarget = LookAtTarget - DesiredLocation;
        if (!ToTarget.IsNearlyZero())
        {
            DesiredRotation = ToTarget.Rotation();
        }
    }

    // 3. 模式内平滑（基类统一契约：Slerp 短路径 + Pitch 钳制，首帧直接对齐）
    const FVector SmoothedLocation = SmoothLocation(DesiredLocation, Ctx.DeltaTime, ViewSmoothSpeed);
    const FRotator SmoothedRotation = SmoothRotation(DesiredRotation, Ctx.DeltaTime, RotationSmoothSpeed);

    OutView.FieldOfView = (Config.FieldOfView > 0.f) ? Config.FieldOfView : FieldOfView;
    OutView.Location = SmoothedLocation;
    OutView.Rotation = SmoothedRotation;
}
