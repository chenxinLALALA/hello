// Copyright Hello Project. All Rights Reserved.

#include "Camera/HelloPlayerCameraManager.h"
#include "Camera/HelloCameraMode_ThirdPerson.h"
#include "DataAsset/CameraModeConfigDataAsset.h"
#include "Character/HelloCharacterBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"

AHelloPlayerCameraManager::AHelloPlayerCameraManager()
{
    // 阶段 1：让相机管理器自己更新 POV
    // （bUseViewTargetRotation 在部分引擎版本中不可用，这里不设置）
}

AHelloPlayerCameraManager* AHelloPlayerCameraManager::GetHelloCameraManager(const UObject* WorldContextObject)
{
    if (!WorldContextObject) return nullptr;

    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return nullptr;

    if (APlayerController* PC = World->GetFirstPlayerController())
    {
        if (AHelloPlayerCameraManager* Mgr = Cast<AHelloPlayerCameraManager>(PC->PlayerCameraManager))
        {
            return Mgr;
        }
    }

    return nullptr;
}

AHelloPlayerCameraManager* AHelloPlayerCameraManager::GetForActor(const AActor* Actor)
{
    const APawn* Pawn = Cast<APawn>(Actor);
    if (!Pawn)
    {
        return nullptr;
    }

    // 只有本地控制的 Pawn 才驱动本地相机；AI/远端返回空，避免敌人动作抢玩家镜头
    APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
    if (!PC || !PC->IsLocalController())
    {
        return nullptr;
    }

    return Cast<AHelloPlayerCameraManager>(PC->PlayerCameraManager);
}

void AHelloPlayerCameraManager::SetCameraModeConfig(UCameraModeConfigDataAsset* InConfig)
{
    CameraModeConfig = InConfig;
    DetermineBaseCameraMode();
}

int32 AHelloPlayerCameraManager::PushCameraMode(TSubclassOf<UHelloCameraMode> ModeClass, const FHelloCameraModeConfig& Config,
    float InBlendTime, float InBlendOutTime, bool bTransient)
{
    if (!ModeClass) return INDEX_NONE;

    UHelloCameraMode* NewMode = NewObject<UHelloCameraMode>(this, ModeClass);
    if (!NewMode) return INDEX_NONE;

    NewMode->SetOwner(this);

    // < 0 表示沿用模式类默认值（ThirdPerson 0.15 / Anchored 0.25+0.35）
    if (InBlendTime >= 0.f)
    {
        NewMode->BlendTime = InBlendTime;
    }
    if (InBlendOutTime >= 0.f)
    {
        NewMode->BlendOutTime = InBlendOutTime;
    }

    if (UHelloCameraMode_Anchored* Anchored = Cast<UHelloCameraMode_Anchored>(NewMode))
    {
        Anchored->SetConfig(Config);
    }

    FHelloCameraModeStackEntry Entry;
    Entry.CameraMode = NewMode;
    Entry.Handle = GenerateHandle();
    Entry.bTransient = bTransient;

    CameraModeStack.Add(Entry);

    UE_LOG(LogTemp, Log, TEXT("[HelloCamera] PushCameraMode %s (Handle=%d, Transient=%d, StackSize=%d)"),
        *ModeClass->GetName(), Entry.Handle, bTransient ? 1 : 0, CameraModeStack.Num());

    return Entry.Handle;
}

void AHelloPlayerCameraManager::PopCameraMode(int32 Handle)
{
    if (Handle == INDEX_NONE) return;

    for (FHelloCameraModeStackEntry& Entry : CameraModeStack)
    {
        if (Entry.Handle == Handle && Entry.CameraMode && !Entry.CameraMode->IsBlendingOut())
        {
            Entry.CameraMode->StartBlendOut();
            UE_LOG(LogTemp, Log, TEXT("[HelloCamera] PopCameraMode Handle=%d (StackSize=%d)"), Handle, CameraModeStack.Num());
            return;
        }
    }
}

void AHelloPlayerCameraManager::PopAllTransientModes()
{
    int32 Popped = 0;
    for (FHelloCameraModeStackEntry& Entry : CameraModeStack)
    {
        if (Entry.bTransient && Entry.CameraMode && !Entry.CameraMode->IsBlendingOut())
        {
            Entry.CameraMode->StartBlendOut();
            ++Popped;
        }
    }

    if (Popped > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[HelloCamera] PopAllTransientModes popped=%d"), Popped);
    }
}

void AHelloPlayerCameraManager::AddCameraPunch(float FOVDelta, FVector2D ScreenOffset, float Duration)
{
    // 复用同一个 modifier：内部维护冲击列表，多个冲击互不覆盖
    if (!PunchModifier)
    {
        PunchModifier = Cast<UHelloCameraModifier_Punch>(AddNewCameraModifier(UHelloCameraModifier_Punch::StaticClass()));
    }

    if (!PunchModifier)
    {
        return;
    }

    PunchModifier->Trigger(FOVDelta, ScreenOffset, Duration);
    PunchModifier->EnableModifier();

    UE_LOG(LogTemp, Verbose, TEXT("[HelloCamera] Punch FOV=%.1f Offset=(%.2f,%.2f) Duration=%.2f"),
        FOVDelta, ScreenOffset.X, ScreenOffset.Y, Duration);
}

void AHelloPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
    // 默认流程：基类内部会调用 UpdateViewTargetInternal（我们的栈合成），
    // 之后 ApplyCameraModifiers 叠加在合成结果之上（Punch/Shake 因此不会被栈覆盖）
    Super::UpdateViewTarget(OutVT, DeltaTime);

    // 冲击全部结束后关闭修饰器，避免每帧空转
    if (PunchModifier && !PunchModifier->IsActive())
    {
        PunchModifier->DisableModifier();
    }

    // 被墙顶到角色体内时淡出角色，避免看到模型内部
    UpdateCharacterFade(OutVT.POV.Location, DeltaTime);
}

void AHelloPlayerCameraManager::UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime)
{
    // 检测 ViewTarget 是否变化（换人 / Possess 切换）
    AActor* CurrentViewTarget = GetViewTarget();
    if (CurrentViewTarget != LastViewTarget)
    {
        LastViewTarget = CurrentViewTarget;

        // 上一个角色的技能镜头必须立刻清掉：留在栈里会按新角色重新解析 Socket
        RemoveAllTransientModes();

        DetermineBaseCameraMode();
    }

    // 先保证栈底默认态正确
    DetermineBaseCameraMode();

    // 清理已完全淡出的瞬态模式
    CleanFinishedModes();

    // 先算基类默认 POV（ViewTarget 过渡、默认相机处理等）
    Super::UpdateViewTargetInternal(OutVT, DeltaTime);

    // 用镜头栈的合成结果覆盖 POV
    EvaluateCameraStack(OutVT, DeltaTime);
}

void AHelloPlayerCameraManager::DetermineBaseCameraMode()
{
    if (!CameraModeConfig) return;

    AActor* ViewTargetActor = GetViewTarget();
    AHelloCharacterBase* HelloChar = Cast<AHelloCharacterBase>(ViewTargetActor);

    FGameplayTag DesiredTag;
    if (HelloChar)
    {
        DesiredTag = HelloChar->GetCameraModeTag();
    }

    // 没取到 Tag 或查不到类时，若栈为空则兜底用 ThirdPerson
    TSubclassOf<UHelloCameraMode> DesiredClass = CameraModeConfig->FindCameraModeClass(DesiredTag);
    if (!DesiredClass)
    {
        DesiredClass = UHelloCameraMode_ThirdPerson::StaticClass();
    }

    // 栈为空或栈底不是 DesiredClass：替换栈底
    if (CameraModeStack.Num() == 0)
    {
        UHelloCameraMode* NewMode = NewObject<UHelloCameraMode>(this, DesiredClass);
        if (NewMode)
        {
            NewMode->SetOwner(this);

            FHelloCameraModeStackEntry Entry;
            Entry.CameraMode = NewMode;
            Entry.Handle = GenerateHandle();
            Entry.bTransient = false;
            CameraModeStack.Add(Entry);

            CurrentBaseCameraTag = DesiredTag;
            bBaseModeInitialized = true;

            UE_LOG(LogTemp, Log, TEXT("[HelloCamera] Base mode initialized: %s"), *DesiredClass->GetName());
        }
    }
    else
    {
        FHelloCameraModeStackEntry& BaseEntry = CameraModeStack[0];
        if (BaseEntry.CameraMode && BaseEntry.CameraMode->GetClass() != DesiredClass)
        {
            // 旧栈底淡出，新栈底淡入（放到栈底之上，权重更高直到混合完成）
            BaseEntry.CameraMode->StartBlendOut();

            UHelloCameraMode* NewMode = NewObject<UHelloCameraMode>(this, DesiredClass);
            if (NewMode)
            {
                NewMode->SetOwner(this);

                FHelloCameraModeStackEntry Entry;
                Entry.CameraMode = NewMode;
                Entry.Handle = GenerateHandle();
                Entry.bTransient = false;
                CameraModeStack.Insert(Entry, 0);

                CurrentBaseCameraTag = DesiredTag;

                UE_LOG(LogTemp, Log, TEXT("[HelloCamera] Base mode switched to %s"), *DesiredClass->GetName());
            }
        }
    }
}

void AHelloPlayerCameraManager::EvaluateCameraStack(FTViewTarget& OutVT, float DeltaTime)
{
    AActor* ViewTargetActor = OutVT.Target;
    ACharacter* ViewCharacter = Cast<ACharacter>(ViewTargetActor);

    // 组装 Context
    FHelloCameraContext Ctx;
    Ctx.ViewTarget = ViewTargetActor;
    Ctx.ControlRotation = PCOwner ? PCOwner->GetControlRotation() : FRotator::ZeroRotator;
    Ctx.DeltaTime = DeltaTime;

    if (ViewTargetActor)
    {
        Ctx.TargetLocation = ViewTargetActor->GetActorLocation();
        Ctx.AnchorLocation = Ctx.TargetLocation;
        Ctx.AnchorRotation = ViewTargetActor->GetActorRotation();

        if (ViewCharacter && ViewCharacter->GetMesh())
        {
            // 默认锚点用 Mesh 的 Root Bone 世界位置（更稳，避免胶囊滑动导致相机抖动）
            Ctx.AnchorLocation = ViewCharacter->GetMesh()->GetComponentLocation();
        }
    }

    // 计算每个模式的视图
    TArray<FHelloCameraModeView> ModeViews;
    ModeViews.Reserve(CameraModeStack.Num());

    float TotalWeight = 0.f;
    for (FHelloCameraModeStackEntry& Entry : CameraModeStack)
    {
        if (!Entry.CameraMode) continue;

        Entry.CameraMode->TickMode(DeltaTime);
        Entry.CameraMode->UpdateBlending(DeltaTime);

        FHelloCameraModeView View;
        Entry.CameraMode->UpdateView(Ctx, View);
        // 提交本模式自身的输出（不是合成后的 POV），供下一帧平滑使用
        Entry.CameraMode->CommitView(View);

        ModeViews.Add(View);
        TotalWeight += Entry.CameraMode->GetBlendWeight();
    }

    // 合成
    if (CameraModeStack.Num() == 0)
    {
        // 没有任何模式时，保持原默认相机位置
        return;
    }

    // 从栈底开始累加
    FHelloCameraModeView FinalView = ModeViews[0];
    float AccumWeight = CameraModeStack[0].CameraMode->GetBlendWeight();

    for (int32 i = 1; i < ModeViews.Num(); ++i)
    {
        if (!CameraModeStack[i].CameraMode) continue;

        const float W = CameraModeStack[i].CameraMode->GetBlendWeight();
        if (W <= KINDA_SMALL_NUMBER) continue;

        const float Alpha = W / (AccumWeight + W);
        FinalView.FieldOfView = FMath::Lerp(FinalView.FieldOfView, ModeViews[i].FieldOfView, Alpha);
        FinalView.Location = FMath::Lerp(FinalView.Location, ModeViews[i].Location, Alpha);
        FinalView.Rotation = FQuat::Slerp(FinalView.Rotation.Quaternion(), ModeViews[i].Rotation.Quaternion(), Alpha).Rotator();
        FinalView.Rotation.Pitch = FMath::ClampAngle(FinalView.Rotation.Pitch, -89.9f, 89.9f);
        FinalView.Rotation.Yaw = FMath::UnwindDegrees(FinalView.Rotation.Yaw);

        AccumWeight += W;
    }

    // 覆盖父类 UpdateViewTarget 算好的 POV
    OutVT.POV.FOV = FinalView.FieldOfView;
    OutVT.POV.Location = FinalView.Location;
    OutVT.POV.Rotation = FinalView.Rotation;

    /*UE_LOG(LogTemp, Log, TEXT("[HelloCamera] Eval StackSize=%d | Loc=%s | Rot=%s | FOV=%.1f"),
        CameraModeStack.Num(), *FinalView.Location.ToString(), *FinalView.Rotation.ToString(), FinalView.FieldOfView);*/
}

void AHelloPlayerCameraManager::CleanFinishedModes()
{
    for (int32 i = CameraModeStack.Num() - 1; i >= 0; --i)
    {
        const FHelloCameraModeStackEntry& Entry = CameraModeStack[i];
        if (Entry.CameraMode && Entry.CameraMode->IsFinished())
        {
            // 永远保留至少一个栈底默认态
            if (i == 0 && CameraModeStack.Num() > 1)
            {
                UE_LOG(LogTemp, Log, TEXT("[HelloCamera] Mode finished: %s (Handle=%d, StackSize->%d)"),
                    *Entry.CameraMode->GetClass()->GetName(), Entry.Handle, CameraModeStack.Num() - 1);
                CameraModeStack.RemoveAt(i);
            }
            else if (i > 0)
            {
                UE_LOG(LogTemp, Log, TEXT("[HelloCamera] Mode finished: %s (Handle=%d, StackSize->%d)"),
                    *Entry.CameraMode->GetClass()->GetName(), Entry.Handle, CameraModeStack.Num() - 1);
                CameraModeStack.RemoveAt(i);
            }
        }
    }
}

void AHelloPlayerCameraManager::RemoveAllTransientModes()
{
    const int32 NumBefore = CameraModeStack.Num();
    CameraModeStack.RemoveAll([](const FHelloCameraModeStackEntry& Entry)
    {
        return Entry.bTransient;
    });

    if (CameraModeStack.Num() != NumBefore)
    {
        UE_LOG(LogTemp, Log, TEXT("[HelloCamera] RemoveAllTransientModes %d->%d"), NumBefore, CameraModeStack.Num());
    }
}

void AHelloPlayerCameraManager::UpdateCharacterFade(const FVector& CameraLocation, float DeltaTime)
{
    AActor* ViewActor = GetViewTarget();
    AHelloCharacterBase* ViewChar = Cast<AHelloCharacterBase>(ViewActor);

    // 换人：先把上一位恢复成不透明，避免旧角色半透明/隐身残留
    if (FadedCharacter.IsValid() && Cast<AActor>(FadedCharacter.Get()) != ViewActor)
    {
        if (AHelloCharacterBase* OldChar = Cast<AHelloCharacterBase>(FadedCharacter.Get()))
        {
            OldChar->SetCameraProximityFade(1.f);
        }
        FadedCharacter.Reset();
        CharacterFadeAlpha = 1.f;
        bCameraFadeTargetHidden = false;
    }

    // 只在"纯第三人称"时淡出：技能特写镜头需要角色出现在画面里
    // 注意栈底永远是 ThirdPerson，所以要额外检查是否有技能镜头正在生效
    bool bSkillModeActive = false;
    for (int32 i = 1; i < CameraModeStack.Num(); ++i)
    {
        if (CameraModeStack[i].CameraMode && CameraModeStack[i].CameraMode->GetBlendWeight() > KINDA_SMALL_NUMBER)
        {
            bSkillModeActive = true;
            break;
        }
    }

    const bool bPureThirdPerson = !bSkillModeActive
        && CameraModeStack.Num() > 0
        && CameraModeStack[0].CameraMode
        && CameraModeStack[0].CameraMode->IsA(UHelloCameraMode_ThirdPerson::StaticClass());

    // 迟滞：已进入隐藏态时要退到更远才恢复，避免在阈值附近来回闪
    if (ViewChar && bPureThirdPerson)
    {
        const float DistToCharacter = FVector::Dist(CameraLocation, ViewChar->GetActorLocation());
        if (bCameraFadeTargetHidden)
        {
            if (DistToCharacter > ShowCharacterDistance)
            {
                bCameraFadeTargetHidden = false;
            }
        }
        else if (DistToCharacter < HideCharacterDistance)
        {
            bCameraFadeTargetHidden = true;
        }
    }
    else
    {
        bCameraFadeTargetHidden = false;
    }

    // 线性推进淡入淡出：时长即完成时间，且能精确到达 0/1
    // （指数曲线永远差一点点，材质会残留一层淡淡的身影）
    const float TargetAlpha = bCameraFadeTargetHidden ? 0.f : 1.f;
    const float Duration = FMath::Max(bCameraFadeTargetHidden ? FadeOutDuration : FadeInDuration, KINDA_SMALL_NUMBER);
    const float Step = FMath::Max(DeltaTime, 0.f) / Duration;

    if (CharacterFadeAlpha < TargetAlpha)
    {
        CharacterFadeAlpha = FMath::Min(CharacterFadeAlpha + Step, TargetAlpha);
    }
    else if (CharacterFadeAlpha > TargetAlpha)
    {
        CharacterFadeAlpha = FMath::Max(CharacterFadeAlpha - Step, TargetAlpha);
    }

    if (!ViewChar)
    {
        return;
    }

    ViewChar->SetCameraProximityFade(CharacterFadeAlpha);

    // 记录正在淡出的角色，供换人时恢复；完全恢复后清空
    if (CharacterFadeAlpha < 1.f - KINDA_SMALL_NUMBER)
    {
        FadedCharacter = ViewChar;
    }
    else
    {
        FadedCharacter.Reset();
    }
}

int32 AHelloPlayerCameraManager::GenerateHandle()
{
    return NextHandle++;
}
