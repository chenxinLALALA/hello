// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/HelloCameraMode.h"
#include "Camera/HelloCameraMode_Anchored.h"
#include "Camera/HelloCameraModifier_Punch.h"
#include "GameFramework/Character.h"
#include "HelloPlayerCameraManager.generated.h"

class UCameraModeConfigDataAsset;
class AHelloPlayerController;

/** 镜头栈条目 */
USTRUCT()
struct HELLO_API FHelloCameraModeStackEntry
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UHelloCameraMode> CameraMode = nullptr;

    /** 唯一句柄，GA 侧 Pop 时使用 */
    int32 Handle = INDEX_NONE;

    /** 是否为瞬态模式（技能镜头），PopAllTransientModes 只清这类 */
    bool bTransient = false;
};

/**
 * Hello 项目相机导演。
 * - 归 PlayerController 所有，跨 Possess 常驻，适合换人小队。
 * - 维护镜头栈（底=默认态，顶=瞬态技能镜头）。
 * - 提供 Push/Pop 技能镜头、PopAllTransientModes、AddCameraPunch 接口。
 */
UCLASS()
class HELLO_API AHelloPlayerCameraManager : public APlayerCameraManager
{
    GENERATED_BODY()

public:
    AHelloPlayerCameraManager();

    /** 静态获取：从 World 取本地 PlayerController 的相机管理器 */
    UFUNCTION(BlueprintPure, Category = "Hello|Camera", meta = (WorldContext = "WorldContextObject"))
    static AHelloPlayerCameraManager* GetHelloCameraManager(const UObject* WorldContextObject);

    /** 从角色/Pawn 取“它所属本地玩家”的相机管理器；AI 或远端控制返回 nullptr */
    UFUNCTION(BlueprintPure, Category = "Hello|Camera")
    static AHelloPlayerCameraManager* GetForActor(const AActor* Actor);

    /** 根据当前 ViewTarget 的 CameraModeTag 切换栈底默认态 */
    UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
    void SetCameraModeConfig(UCameraModeConfigDataAsset* InConfig);

    /**
     * 技能 GA 调用：压入一个瞬态镜头模式，返回 Handle。
     * InBlendTime / InBlendOutTime < 0 时沿用模式类默认值。
     */
    UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
    int32 PushCameraMode(TSubclassOf<UHelloCameraMode> ModeClass, const FHelloCameraModeConfig& Config,
        float InBlendTime = -1.f, float InBlendOutTime = -1.f, bool bTransient = true);

    /** 技能 GA 调用：弹出指定 Handle 的镜头模式 */
    UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
    void PopCameraMode(int32 Handle);

    /** 弹出所有瞬态模式（EndAbility 兜底） */
    UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
    void PopAllTransientModes();

    /**
     * 触发一次 FOV 冲击 + 屏幕偏移叠加（轻技能/连段命中）。
     * 多次调用可并存、各自衰减，不会互相覆盖。
     */
    UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
    void AddCameraPunch(float FOVDelta = -6.f, FVector2D ScreenOffset = FVector2D::ZeroVector, float Duration = 0.2f);

protected:
    virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

    /** 内层：算默认 POV 后替换为镜头栈合成结果；基类随后会在这之上叠加 CameraModifier */
    virtual void UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime) override;

    /** 确定栈底默认态：按 ViewTarget 的 CameraModeTag 查表 */
    void DetermineBaseCameraMode();

    /** 逐模式 UpdateView 后合成到 POV */
    void EvaluateCameraStack(FTViewTarget& OutVT, float DeltaTime);

    /** 从栈中移除已完全淡出的瞬态模式 */
    void CleanFinishedModes();

    /** 立即移除所有瞬态模式（不淡出）——换人/切目标时用，避免旧镜头按新角色重新解析 Socket */
    void RemoveAllTransientModes();

    /**
     * 相机贴近角色时淡出角色，避免被墙顶到角色体内后看到模型内部。
     * 只在纯第三人称生效（技能特写镜头要靠角色出镜）。
     */
    void UpdateCharacterFade(const FVector& CameraLocation, float DeltaTime);

    /** 生成新的唯一 Handle */
    int32 GenerateHandle();

protected:
    /** Tag -> 镜头模式类 配置资产 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    TObjectPtr<UCameraModeConfigDataAsset> CameraModeConfig;

    /** 镜头栈 */
    UPROPERTY()
    TArray<FHelloCameraModeStackEntry> CameraModeStack;

    /** 当前栈底默认态使用的 Tag */
    UPROPERTY()
    FGameplayTag CurrentBaseCameraTag;

    /** 相机距角色中心小于该值时开始淡出角色（避免看到模型内部） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Visibility", meta = (ClampMin = "0.0"))
    float HideCharacterDistance = 90.f;

    /** 相机距角色中心大于该值时淡入恢复（大于 HideCharacterDistance，迟滞防闪烁） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Visibility", meta = (ClampMin = "0.0"))
    float ShowCharacterDistance = 130.f;

    /** 淡出（相机靠近）时长，秒 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Visibility", meta = (ClampMin = "0.01"))
    float FadeOutDuration = 0.15f;

    /** 淡入（相机远离）时长，秒 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Visibility", meta = (ClampMin = "0.01"))
    float FadeInDuration = 0.25f;

private:
    int32 NextHandle = 1;

    /** 复用的 FOV 冲击修饰器（内部维护多个冲击，全部结束后关闭） */
    UPROPERTY(Transient)
    TObjectPtr<UHelloCameraModifier_Punch> PunchModifier = nullptr;

    /** 目标状态：true = 应该隐藏（靠近） */
    bool bCameraFadeTargetHidden = false;

    /** 当前淡出系数：1 = 完全不透明，0 = 完全隐藏 */
    float CharacterFadeAlpha = 1.f;

    /** 当前被淡出的角色（换人时要先恢复上一位） */
    TWeakObjectPtr<ACharacter> FadedCharacter;

    /** 是否已经初始化过栈底（防首帧空 ViewTarget 崩溃） */
    bool bBaseModeInitialized = false;

    /** 上一帧的 ViewTarget，用于检测换人/切目标 */
    UPROPERTY()
    TObjectPtr<AActor> LastViewTarget = nullptr;
};
