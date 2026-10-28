// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/AS/HelloAttributeSet.h"
#include "AbilitySystem/ASC/HelloASC.h"
#include "Delegates/Delegate.h"
#include "GameFramework/Character.h"
#include "GameplayTagContainer.h"
#include "Camera/HelloCameraMode_Anchored.h"
#include "HelloCharacterBase.generated.h"

class UComboConfigDataAsset;
class UPrimitiveComponent;
class UAnimSequence;
class UMaterialInstanceDynamic;

/**
 * 已上膛待确认的镜头冲击。
 * AnimNotify 是共享 CDO，不能存实例状态，所以参数记在角色上（同技能镜头句柄的思路）。
 */
struct FHelloArmedCameraPunch
{
	bool bArmed = false;
	bool bConsumed = false;
	float FOVDelta = 0.f;
	FVector2D ScreenOffset = FVector2D::ZeroVector;
	float Duration = 0.f;
	/** 过期时刻（世界时间秒）；上膛时按 ConfirmWindow 写入 */
	double ExpireTime = 0.0;
};

/**
 * “命中才生效”的技能镜头待命状态。
 * 区间通知 Begin 时上膛（不推镜头），命中确认后推入，区间 End 时回位。
 * 同样必须存在角色上：AnimNotifyState 是共享 CDO。
 */
struct FHelloSkillCameraWindow
{
	/** 已上膛，等待命中 */
	bool bArmed = false;
	/** 命中后已推入镜头（句柄另存 ActiveSkillCameraHandle） */
	bool bPushed = false;
	FHelloCameraModeConfig Config;
	float BlendInTime = -1.f;
	float BlendOutTime = -1.f;
	/** 命中后至少保持的时长（秒）；0 = 完全由通知区间决定 */
	float MinHoldAfterHit = 0.f;
	/** 命中的时刻（世界时间秒），用于 MinHoldAfterHit 计算剩余保持时间 */
	double HitTime = 0.0;
	/** 命中太晚时，延迟回位的定时器 */
	FTimerHandle PendingPopTimer;
};

/**
 * 角色基类：提供 GAS 能力系统和连段配置的公共基础设施
 * 敌人和玩家角色都继承此类
 */
UCLASS()
class HELLO_API AHelloCharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AHelloCharacterBase();

	// ---- GAS 组件和属性 ----

	/** 能力系统组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UHelloASC> AbilitySystemComponent;

	/** 属性集（包含 Health/MaxHealth/Attack/Defense） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
	TObjectPtr<UHelloAttributeSet> AttributeSet;

	/** 主动技能数组 */
	UPROPERTY(EditDefaultsOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> CharacterActiveAbilities;

	/** 被动技能数组 */
	UPROPERTY(EditDefaultsOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> CharacterPositiveAbilities;

	/** 连段配置 DataAsset（含 5 段动画/伤害/窗口时长） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	UComboConfigDataAsset* ComboConfig = nullptr;

	/** 当前连段段号（1~5；0 = 非连段中）。由 GA 播放各段时更新，ApplyDamageToActor 据此从 ComboConfig 取该段 DamageGE */
	UPROPERTY(BlueprintReadOnly, Category = "Combo")
	int32 CurrentComboSegment = 0;

	// ---- 相机默认态（Tag 驱动，相机管理器按 Tag 查表决定默认镜头模式）----

	/** 角色默认镜头模式 Tag（如 Camera.Mode.ThirdPerson），每个角色/敌人可配不同值 */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FGameplayTag CameraModeTag;

	/** 获取当前角色配置的默认镜头模式 Tag */
	UFUNCTION(BlueprintPure, Category = "Camera")
	FGameplayTag GetCameraModeTag() const { return CameraModeTag; }

	/**
	 * 相机贴近淡出：1 = 完全不透明，0 = 完全隐藏（到 0 才 SetVisibility(false) 省渲染）。
	 * 需要角色材质配合：一个标量参数（默认 "Fade"）接到 OpacityMask，混合模式 Masked + Dither Temporal AA。
	 * 材质没配该参数时表现为"到 0 才瞬间隐藏"，与旧行为一致，不会报错。
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetCameraProximityFade(float Alpha);

	/** 淡出用的材质标量参数名（需与材质中的参数名一致） */
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FName CameraFadeParameterName = TEXT("Fade");

	// ---- 动画通知驱动的技能镜头 ----

	/** 动画通知 Start 时记录句柄（AnimNotifyState 是共享 CDO，状态只能存在角色上） */
	void SetActiveSkillCameraHandle(int32 InHandle) { ActiveSkillCameraHandle = InHandle; }

	/** 当前记录中的技能镜头句柄（INDEX_NONE = 无） */
	int32 GetActiveSkillCameraHandle() const { return ActiveSkillCameraHandle; }

	/** 弹出当前记录的技能镜头（幂等）；NotifyEnd 与 GA EndAbility 共用 */
	void PopActiveSkillCamera();

	// ---- “命中才生效”的技能镜头 ----

	/** 上膛：区间通知 Begin 且勾选“命中才生效”时调用；只记参数，不推镜头 */
	void ArmSkillCameraForHit(const FHelloCameraModeConfig& Config, float BlendInTime, float BlendOutTime, float MinHoldAfterHit);

	/** 命中确认：已上膛且未推入 → 推入 Anchored 镜头；同一区间只推一次 */
	void TryConsumeSkillCameraOnHit();

	/** 区间通知 End：未命中则清膛；已推入则回位（受 MinHoldAfterHit 约束） */
	void EndSkillCameraWindow();

	/** 清掉待命的技能镜头（GA 结束兜底；已推入的镜头由 PopActiveSkillCamera 负责） */
	void ClearSkillCameraWindow();

	// ---- 闪避动画（按角色配置；GA_Dodge 运行时读取）----

	/** 前冲闪避动画（有方向输入时使用；角色转向输入方向后播放） */
	UPROPERTY(EditDefaultsOnly, Category = "Dodge")
	TObjectPtr<UAnimSequence> DodgeForwardMontage = nullptr;

	/** 后闪闪避动画（无方向输入且战斗中使用；角色转向背后后播放） */
	UPROPERTY(EditDefaultsOnly, Category = "Dodge")
	TObjectPtr<UAnimSequence> DodgeBackwardMontage = nullptr;

	/** 旧版兼容字段：若 DodgeForward/Backward 未配置，GA_Dodge 回退到此字段 */
	UPROPERTY(EditDefaultsOnly, Category = "Dodge")
	TObjectPtr<UAnimSequence> DodgeMontage = nullptr;

	// ---- 初始属性配置（每种角色在蓝图子类里配不同值） ----

	/** 初始最大生命值（生成时同时赋给 Health，满血出生） */
	UPROPERTY(EditDefaultsOnly, Category = "Attributes|Defaults")
	float DefaultMaxHealth = 100.f;

	/** 初始攻击力 */
	UPROPERTY(EditDefaultsOnly, Category = "Attributes|Defaults")
	float DefaultAttack = 20.f;

	/** 初始防御力 */
	UPROPERTY(EditDefaultsOnly, Category = "Attributes|Defaults")
	float DefaultDefense = 10.f;

	// ---- IAbilitySystemInterface 实现 ----

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// ---- 属性访问接口 ----

	/** 获取当前生命值 */
	UFUNCTION(BlueprintPure, Category = "Attributes")
	float GetHealth() const;

	/** 获取最大生命值 */
	UFUNCTION(BlueprintPure, Category = "Attributes")
	float GetMaxHealth() const;

	/** 获取攻击力 */
	UFUNCTION(BlueprintPure, Category = "Attributes")
	float GetAttack() const;

	/** 获取防御力 */
	UFUNCTION(BlueprintPure, Category = "Attributes")
	float GetDefense() const;

	/** 检查是否死亡 */
	UFUNCTION(BlueprintPure, Category = "Attributes")
	bool IsDead() const;

	// ---- 属性变化回调注册 ----

	/** 注册生命值变化回调 */
	FDelegateHandle RegisterHealthChanged(FOnAttributeChanged::FDelegate Callback);

	/** 注册最大生命值变化回调 */
	FDelegateHandle RegisterMaxHealthChanged(FOnAttributeChanged::FDelegate Callback);

	/** 注册攻击力变化回调 */
	FDelegateHandle RegisterAttackChanged(FOnAttributeChanged::FDelegate Callback);

	/** 注册防御力变化回调 */
	FDelegateHandle RegisterDefenseChanged(FOnAttributeChanged::FDelegate Callback);

	/** 注册任意属性变化回调 */
	FDelegateHandle RegisterAnyAttributeChanged(FOnAttributeChanged::FDelegate Callback);

	/** 注销生命值变化回调 */
	void UnregisterHealthChanged(FDelegateHandle Handle);

	/** 注销最大生命值变化回调 */
	void UnregisterMaxHealthChanged(FDelegateHandle Handle);

	/** 注销攻击力变化回调 */
	void UnregisterAttackChanged(FDelegateHandle Handle);

	/** 注销防御力变化回调 */
	void UnregisterDefenseChanged(FDelegateHandle Handle);

	/** 注销任意属性变化回调 */
	void UnregisterAnyAttributeChanged(FDelegateHandle Handle);

	// ---- 武器碰撞检测 ----

	/** 开启武器碰撞检测（由 AnimNotify 调用） */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EnableWeaponTrace();

	/** 关闭武器碰撞检测（由 AnimNotify 调用） */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void DisableWeaponTrace();

	// ---- 命中确认驱动的镜头冲击 ----

	/**
	 * 上膛：由 AnimNotify_HelloCameraPunch 在打击帧调用，仅记录参数，不直接震。
	 * 真正在 ApplyDamageToActor 里结算到伤害时才发射冲击——挥空完全不动镜头。
	 * @param ConfirmWindow 上膛后等待命中的时长（秒），超时仍未命中则本次作废
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void ArmCameraPunch(float FOVDelta, FVector2D ScreenOffset, float Duration, float ConfirmWindow = 0.2f);

	/**
	 * 命中确认：真正结算到伤害后调用。
	 * 已上膛、未消费、未过期 → 发一次冲击并锁定；同一刀命中多个目标也只震一次。
	 * @param HitTarget 仅用于日志，指出是打中了谁
	 */
	void TryConsumeCameraPunch(const AActor* HitTarget = nullptr);

	/** 清膛：收武器（DisableWeaponTrace）时调用，防止上一段“上了膛但没打中”的参数漏到下一刀 */
	void ClearCameraPunchArm();

	// ---- 时间减速（完美闪避）----

	/** 对自身施加时间减速：CustomTimeDilation = Scale，Duration 秒后自动恢复 1.0（GA_Dodge 完美闪避时对攻击者调用） */
	void SlowTime(float Scale, float Duration);

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	/** 初始化技能授予 */
	void InitialStartUpAbilities();

	/** 初始化默认属性值 */
	void InitializeDefaultAttributes();

	// ---- 武器碰撞检测内部方法 ----

	/** 武器碰撞回调 */
	UFUNCTION()
	void OnWeaponOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	/** 应用伤害到目标 */
	void ApplyDamageToActor(AActor* TargetActor);

	/** 清除当前攻击的已伤害记录（每次新攻击时调用） */
	void ClearHitEnemies();

	/** 当前攻击已伤害过的敌人（防重复） */
	UPROPERTY()
	TSet<AActor*> HitEnemiesThisAttack;

	/** 武器是否正在检测碰撞 */
	UPROPERTY()
	bool bWeaponTraceEnabled = false;

	/** 已上膛待确认的镜头冲击（AnimNotify 共享 CDO，状态只能存在角色上） */
	FHelloArmedCameraPunch ArmedCameraPunch;

	/** “命中才生效”的技能镜头待命状态 */
	FHelloSkillCameraWindow SkillCameraWindow;

	// ---- 时间减速（完美闪避）----

	/** 时间恢复正常（减速到期回调） */
	void RestoreTimeDilation();

	/** 减速恢复计时器（挂在世界计时器上，不受自身 CustomTimeDilation 影响，保证准时恢复） */
	FTimerHandle SlowTimeTimerHandle;

	// ---- 相机贴近淡出 ----

	/** 按需为每个材质槽创建/缓存动态材质实例（参数要写在 MID 上才生效） */
	void EnsureCameraFadeMIDs();

	/** 缓存的动态材质实例 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CameraFadeMIDs;

	/** 上一帧的淡出系数，用于跳过无变化的参数写入与可见性切换 */
	float CurrentCameraFadeAlpha = 1.f;

	/** 动画通知压入的技能镜头句柄 */
	int32 ActiveSkillCameraHandle = INDEX_NONE;
};
