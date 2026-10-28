// Copyright Hello Project. All Rights Reserved.

#include "Character/HelloCharacterBase.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/HelloPlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "DataAsset/ComboConfigDataAsset.h"
#include "GameplayTags/HelloGameplayTags.h"

AHelloCharacterBase::AHelloCharacterBase()
{
	// 创建能力系统组件
	AbilitySystemComponent = CreateDefaultSubobject<UHelloASC>("HelloAbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);

	// 创建属性集
	AttributeSet = CreateDefaultSubobject<UHelloAttributeSet>("HelloAttributeSet");
}

UAbilitySystemComponent* AHelloCharacterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

float AHelloCharacterBase::GetHealth() const
{
	if (AttributeSet)
	{
		return AttributeSet->GetHealth();
	}
	return 0.f;
}

float AHelloCharacterBase::GetMaxHealth() const
{
	if (AttributeSet)
	{
		return AttributeSet->GetMaxHealth();
	}
	return 1.f;
}

float AHelloCharacterBase::GetAttack() const
{
	if (AttributeSet)
	{
		return AttributeSet->GetAttack();
	}
	return 0.f;
}

float AHelloCharacterBase::GetDefense() const
{
	if (AttributeSet)
	{
		return AttributeSet->GetDefense();
	}
	return 0.f;
}

bool AHelloCharacterBase::IsDead() const
{
	if (AttributeSet)
	{
		return AttributeSet->IsDead();
	}
	return false;
}

FDelegateHandle AHelloCharacterBase::RegisterHealthChanged(FOnAttributeChanged::FDelegate Callback)
{
	if (AttributeSet)
	{
		FDelegateHandle Handle = AttributeSet->OnHealthChanged.Add(Callback);
		return Handle;
	}
	return FDelegateHandle();
}

FDelegateHandle AHelloCharacterBase::RegisterMaxHealthChanged(FOnAttributeChanged::FDelegate Callback)
{
	if (AttributeSet)
	{
		FDelegateHandle Handle = AttributeSet->OnMaxHealthChanged.Add(Callback);
		return Handle;
	}
	return FDelegateHandle();
}

FDelegateHandle AHelloCharacterBase::RegisterAttackChanged(FOnAttributeChanged::FDelegate Callback)
{
	if (AttributeSet)
	{
		FDelegateHandle Handle = AttributeSet->OnAttackChanged.Add(Callback);
		return Handle;
	}
	return FDelegateHandle();
}

FDelegateHandle AHelloCharacterBase::RegisterDefenseChanged(FOnAttributeChanged::FDelegate Callback)
{
	if (AttributeSet)
	{
		FDelegateHandle Handle = AttributeSet->OnDefenseChanged.Add(Callback);
		return Handle;
	}
	return FDelegateHandle();
}

FDelegateHandle AHelloCharacterBase::RegisterAnyAttributeChanged(FOnAttributeChanged::FDelegate Callback)
{
	if (AttributeSet)
	{
		FDelegateHandle Handle = AttributeSet->OnAnyAttributeChanged.Add(Callback);
		return Handle;
	}
	return FDelegateHandle();
}

void AHelloCharacterBase::UnregisterHealthChanged(FDelegateHandle Handle)
{
	if (AttributeSet)
	{
		AttributeSet->OnHealthChanged.Remove(Handle);
	}
}

void AHelloCharacterBase::UnregisterMaxHealthChanged(FDelegateHandle Handle)
{
	if (AttributeSet)
	{
		AttributeSet->OnMaxHealthChanged.Remove(Handle);
	}
}

void AHelloCharacterBase::UnregisterAttackChanged(FDelegateHandle Handle)
{
	if (AttributeSet)
	{
		AttributeSet->OnAttackChanged.Remove(Handle);
	}
}

void AHelloCharacterBase::UnregisterDefenseChanged(FDelegateHandle Handle)
{
	if (AttributeSet)
	{
		AttributeSet->OnDefenseChanged.Remove(Handle);
	}
}

void AHelloCharacterBase::UnregisterAnyAttributeChanged(FDelegateHandle Handle)
{
	if (AttributeSet)
	{
		AttributeSet->OnAnyAttributeChanged.Remove(Handle);
	}
}

void AHelloCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}

	// 初始化默认属性
	InitializeDefaultAttributes();

	// 授予技能
	InitialStartUpAbilities();
}

void AHelloCharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}

	// 网络模式下，在服务器端初始化技能
	if (HasAuthority())
	{
		InitialStartUpAbilities();
	}
}

void AHelloCharacterBase::UnPossessed()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->CancelAllAbilities();
	}

	Super::UnPossessed();
}

void AHelloCharacterBase::InitialStartUpAbilities()
{
	if (HasAuthority() && AbilitySystemComponent)
	{
		// 授予主动技能
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : CharacterActiveAbilities)
		{
			if (!AbilityClass) continue;
			if (AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
				continue;

			FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, this);
			AbilitySystemComponent->GiveAbility(Spec);
		}

		// 授予被动技能
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : CharacterPositiveAbilities)
		{
			if (!AbilityClass) continue;
			if (AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
				continue;

			FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, this);
			AbilitySystemComponent->GiveAbility(Spec);
		}
	}
}

void AHelloCharacterBase::InitializeDefaultAttributes()
{
	if (!AttributeSet || !HasAuthority()) return;

	// InitXxx 由 ATTRIBUTE_ACCESSORS 宏生成，直接写入 BaseValue
	// （不走 GE，一次性初始化；将来等级/装备系统可用 GE 在此基础上叠加）
	AttributeSet->InitMaxHealth(DefaultMaxHealth);
	AttributeSet->InitHealth(DefaultMaxHealth);   // 满血出生
	AttributeSet->InitAttack(DefaultAttack);
	AttributeSet->InitDefense(DefaultDefense);

	UE_LOG(LogTemp, Warning, TEXT("[%s] 属性初始化: HP=%.0f/%.0f, ATK=%.0f, DEF=%.0f"),
		*GetName(), AttributeSet->GetHealth(), AttributeSet->GetMaxHealth(),
		AttributeSet->GetAttack(), AttributeSet->GetDefense());
}

// ---- 相机贴近淡出 ----

void AHelloCharacterBase::SetCameraProximityFade(float Alpha)
{
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!CharacterMesh)
	{
		return;
	}

	// 完全淡出后才真正隐藏，省掉 0 透明度的渲染开销
	if (Alpha <= KINDA_SMALL_NUMBER)
	{
		if (CurrentCameraFadeAlpha > KINDA_SMALL_NUMBER)
		{
			CharacterMesh->SetVisibility(false, true);
		}
		CurrentCameraFadeAlpha = Alpha;
		return;
	}

	// 从完全隐藏恢复时先把 Mesh 打开，否则改材质参数也不会被渲染
	if (CurrentCameraFadeAlpha <= KINDA_SMALL_NUMBER)
	{
		CharacterMesh->SetVisibility(true, true);
	}

	// 参数没变化就跳过，避免每帧遍历材质
	if (FMath::IsNearlyEqual(Alpha, CurrentCameraFadeAlpha, 0.001f))
	{
		return;
	}
	CurrentCameraFadeAlpha = Alpha;

	EnsureCameraFadeMIDs();
	for (UMaterialInstanceDynamic* MID : CameraFadeMIDs)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(CameraFadeParameterName, Alpha);
		}
	}
}

void AHelloCharacterBase::EnsureCameraFadeMIDs()
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!CharacterMesh)
	{
		return;
	}

	const int32 NumMaterials = CharacterMesh->GetNumMaterials();
	if (CameraFadeMIDs.Num() == NumMaterials)
	{
		return;
	}

	CameraFadeMIDs.Reset();
	CameraFadeMIDs.Reserve(NumMaterials);

	for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
	{
		// 已经是指向本组件的 MID 就复用，避免重复创建导致材质切换闪一帧
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CharacterMesh->GetMaterial(MaterialIndex));
		if (!MID)
		{
			MID = CharacterMesh->CreateDynamicMaterialInstance(MaterialIndex);
		}
		CameraFadeMIDs.Add(MID);
	}
}

void AHelloCharacterBase::PopActiveSkillCamera()
{
	// 延迟回位定时器先清：通知 End、GA 结束、新区间开始都以“本次”为准
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SkillCameraWindow.PendingPopTimer);
	}
	SkillCameraWindow.bPushed = false;

	if (ActiveSkillCameraHandle == INDEX_NONE)
	{
		return;
	}

	if (AHelloPlayerCameraManager* CameraMgr = AHelloPlayerCameraManager::GetForActor(this))
	{
		CameraMgr->PopCameraMode(ActiveSkillCameraHandle);
	}

	ActiveSkillCameraHandle = INDEX_NONE;
}

void AHelloCharacterBase::ArmSkillCameraForHit(const FHelloCameraModeConfig& Config, float BlendInTime, float BlendOutTime, float MinHoldAfterHit)
{
	// 同一角色同时只支持一个技能镜头区间：新区间开始时先收掉上一个
	PopActiveSkillCamera();
	ClearSkillCameraWindow();

	FHelloSkillCameraWindow& Window = SkillCameraWindow;
	Window.bArmed = true;
	Window.bPushed = false;
	Window.Config = Config;
	// 存活时长由通知区间 / MinHoldAfterHit 控制，关掉模式自带的 Hold 到期逻辑
	Window.Config.HoldDuration = 0.f;
	Window.BlendInTime = BlendInTime;
	Window.BlendOutTime = BlendOutTime;
	Window.MinHoldAfterHit = FMath::Max(MinHoldAfterHit, 0.f);
	Window.HitTime = 0.0;

	UE_LOG(LogTemp, Verbose, TEXT("[HelloCamera] SkillCamera armed, waiting for hit (%s)"), *GetName());
}

void AHelloCharacterBase::TryConsumeSkillCameraOnHit()
{
	FHelloSkillCameraWindow& Window = SkillCameraWindow;
	if (!Window.bArmed || Window.bPushed) return;

	// 只驱动本地玩家的相机；AI / 远端角色直接作废
	AHelloPlayerCameraManager* CameraMgr = AHelloPlayerCameraManager::GetForActor(this);
	if (!CameraMgr)
	{
		Window.bArmed = false;
		return;
	}

	// 新镜头接管前先收掉其它通知镜头（与区间通知 Begin 的旧语义一致）
	PopActiveSkillCamera();

	const int32 Handle = CameraMgr->PushCameraMode(
		UHelloCameraMode_Anchored::StaticClass(), Window.Config, Window.BlendInTime, Window.BlendOutTime, /*bTransient=*/true);

	Window.bPushed = true;
	Window.HitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	SetActiveSkillCameraHandle(Handle);

	UE_LOG(LogTemp, Log, TEXT("[HelloCamera] SkillCamera pushed by hit (%s, Handle=%d)"), *GetName(), Handle);
}

void AHelloCharacterBase::EndSkillCameraWindow()
{
	FHelloSkillCameraWindow& Window = SkillCameraWindow;
	const bool bWasArmed = Window.bArmed;
	Window.bArmed = false;

	// 没推过镜头：整个区间都没命中，什么都不用收。
	// 注意不能拿 bArmed 判断“这个区间还需不需要处理”——DisableWeaponTrace 会提前清待命，
	// 但已推入的镜头（bPushed）必须由本次 End 负责回位
	if (!Window.bPushed)
	{
		if (bWasArmed)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[HelloCamera] SkillCamera window ended without hit (%s)"), *GetName());
		}
		return;
	}

	// 命中太晚（靠近区间尾巴）时，按最小保持时间延迟回位，避免只闪一帧
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : Window.HitTime;
	const float Remaining = static_cast<float>((Window.HitTime + Window.MinHoldAfterHit) - Now);

	if (World && Remaining > 0.f)
	{
		World->GetTimerManager().SetTimer(Window.PendingPopTimer, this,
			&AHelloCharacterBase::PopActiveSkillCamera, Remaining, false);

		UE_LOG(LogTemp, Verbose, TEXT("[HelloCamera] SkillCamera pop delayed %.2fs (min hold after hit)"), Remaining);
	}
	else
	{
		PopActiveSkillCamera();
	}
}

void AHelloCharacterBase::ClearSkillCameraWindow()
{
	// 只作废“待命中”状态；已推入的镜头与延迟回位定时器由 PopActiveSkillCamera 负责
	SkillCameraWindow.bArmed = false;
}

// ---- 武器碰撞检测 ----

void AHelloCharacterBase::EnableWeaponTrace()
{
	if (bWeaponTraceEnabled) return;

	bWeaponTraceEnabled = true;

	// 清除上次攻击的已伤害记录
	ClearHitEnemies();

	// 获取角色 SkeletalMesh 组件
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!CharacterMesh) return;

	// 为武器骨骼设置碰撞回调
	CharacterMesh->SetGenerateOverlapEvents(true);
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CharacterMesh->SetCollisionObjectType(ECC_Pawn);
	CharacterMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	CharacterMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	CharacterMesh->OnComponentBeginOverlap.AddDynamic(this, &AHelloCharacterBase::OnWeaponOverlap);
}

void AHelloCharacterBase::DisableWeaponTrace()
{
	// 收武器即清膛（幂等，放在提前 return 之前）：攻击被剪断/闪避打断时，
	// 上一段“上了膛但没打中”的参数可能还留着，不清会让下一刀在它自己的通知之前
	// 命中时用上一段的参数震；DisableWeaponTrace 在 GA 剪断/结束路径上都会兜底调用
	ClearCameraPunchArm();

	// 同理作废“等待命中”的技能镜头待命（已推入的镜头与延迟回位定时器不受影响）
	ClearSkillCameraWindow();

	if (!bWeaponTraceEnabled) return;

	bWeaponTraceEnabled = false;

	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!CharacterMesh) return;

	CharacterMesh->OnComponentBeginOverlap.RemoveDynamic(this, &AHelloCharacterBase::OnWeaponOverlap);

	// 重置碰撞设置
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AHelloCharacterBase::ArmCameraPunch(float FOVDelta, FVector2D ScreenOffset, float Duration, float ConfirmWindow)
{
	const UWorld* World = GetWorld();
	if (!World) return;

	// 只上膛不发射：命中确认之前完全不碰相机
	ArmedCameraPunch.bArmed = true;
	ArmedCameraPunch.bConsumed = false;
	ArmedCameraPunch.FOVDelta = FOVDelta;
	ArmedCameraPunch.ScreenOffset = ScreenOffset;
	ArmedCameraPunch.Duration = Duration;
	ArmedCameraPunch.ExpireTime = World->GetTimeSeconds() + FMath::Max(ConfirmWindow, 0.f);

	UE_LOG(LogTemp, Verbose, TEXT("[HelloCamera] Punch armed FOV=%.1f Duration=%.2f ConfirmWindow=%.2f"),
		FOVDelta, Duration, ConfirmWindow);
}

void AHelloCharacterBase::TryConsumeCameraPunch(const AActor* HitTarget)
{
	if (!ArmedCameraPunch.bArmed || ArmedCameraPunch.bConsumed) return;

	const UWorld* World = GetWorld();
	if (!World) return;

	// 超时作废：上膛后迟迟没打中（挥空/被打断），本次不震
	if (World->GetTimeSeconds() > ArmedCameraPunch.ExpireTime)
	{
		ArmedCameraPunch.bArmed = false;
		UE_LOG(LogTemp, Verbose, TEXT("[HelloCamera] Punch arm expired without hit (late by %.2f s)"),
			World->GetTimeSeconds() - ArmedCameraPunch.ExpireTime);
		return;
	}

	// 先锁定再发：一次上膛只对应一次冲击，同一刀命中多个目标不会连震
	ArmedCameraPunch.bConsumed = true;

	// 命中确认日志（Log 级）：一次上膛最多一条，用来区分"没上膛"和"上膛已消费"
	UE_LOG(LogTemp, Log, TEXT("[HelloCamera] Punch consumed by hit on %s"), *GetNameSafe(HitTarget));

	// GetForActor 只返回本地控制角色的相机管理器：AI/远端命中不会抢玩家镜头
	if (AHelloPlayerCameraManager* CameraMgr = AHelloPlayerCameraManager::GetForActor(this))
	{
		CameraMgr->AddCameraPunch(ArmedCameraPunch.FOVDelta, ArmedCameraPunch.ScreenOffset, ArmedCameraPunch.Duration);
	}
}

void AHelloCharacterBase::ClearCameraPunchArm()
{
	ArmedCameraPunch.bArmed = false;
	ArmedCameraPunch.bConsumed = false;
}

void AHelloCharacterBase::OnWeaponOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!bWeaponTraceEnabled) return;

	// 忽略自己
	if (OtherActor == this) return;

	// 检查是否为敌人角色（HelloCharacterBase 类型）
	AHelloCharacterBase* TargetCharacter = Cast<AHelloCharacterBase>(OtherActor);
	if (!TargetCharacter) return;

	// 检查是否已伤害过此敌人
	if (HitEnemiesThisAttack.Contains(OtherActor)) return;

	// 应用伤害
	ApplyDamageToActor(OtherActor);
}

void AHelloCharacterBase::ApplyDamageToActor(AActor* TargetActor)
{
	if (!TargetActor) return;

	AHelloCharacterBase* TargetCharacter = Cast<AHelloCharacterBase>(TargetActor);
	if (!TargetCharacter || TargetCharacter->IsDead()) return;

	UAbilitySystemComponent* TargetASC = TargetCharacter->GetAbilitySystemComponent();
	if (!TargetASC || !AbilitySystemComponent) return;

	// ---- 完美闪避判定：目标处于无敌帧 → 免伤 + 发事件，由其 GA_Dodge 决定后续（减速攻击者）----
	// 数据层免疫（GE_Damage 的 Application Ignore Tags）由 GAS 兜底保证不掉血；
	// 这里显式查一次是为了拿到"完美"语义：记入 HitSet 防重复触发 + 发 Event.Dodge.PerfectHit
	if (TargetASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Dodge_Invincible))
	{
		// 同一挥砍对同一目标只结算一次完美闪避（武器持续贴合会反复进 Overlap）
		HitEnemiesThisAttack.Add(TargetActor);

		// 事件发到目标（闪避者）的 ASC，其 GA_Dodge 的 WaitGameplayEvent 正在监听
		FGameplayEventData PerfectEventData;
		PerfectEventData.EventTag = HelloGameplayTags::Event_Dodge_PerfectHit;
		PerfectEventData.Instigator = this;        // 攻击者 = 被减速方
		PerfectEventData.Target = TargetActor;     // 闪避者
		TargetASC->HandleGameplayEvent(HelloGameplayTags::Event_Dodge_PerfectHit, &PerfectEventData);

		UE_LOG(LogTemp, Warning, TEXT("[%s] 命中无敌帧中的 [%s] → 完美闪避，伤害无效"),
			*GetName(), *TargetActor->GetName());
		return;
	}

	// 从自身 ComboConfig 取当前段的伤害 GE（ComboConfig 是基类成员，直接读取，不查 ASC）
	TSubclassOf<UGameplayEffect> DamageGEClass = nullptr;
	if (ComboConfig)
	{
		DamageGEClass = ComboConfig->GetSegment(CurrentComboSegment).DamageGE;
	}

	if (!DamageGEClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s] 第 %d 段未配置 DamageGE（或 ComboConfig 为空），跳过伤害"),
			*GetName(), CurrentComboSegment);
		return;
	}

	// 伤害值 = 当前攻击力，通过 SetByCaller 传入 GE
	const float DamageAmount = AttributeSet ? AttributeSet->GetAttack() : 10.f;

	// 手动构造 Spec（GA 外创建 Spec 的通用写法）：
	// 1) 从 GE 类取 CDO 作为 Def —— 之前崩溃就是因为 Spec 的 Def 为空
	const UGameplayEffect* EffectDef = DamageGEClass->GetDefaultObject<UGameplayEffect>();

	// 2) 构造上下文，攻击者作为 Instigator（伤害来源，供 PostGameplayEffectExecute 查询）
	FGameplayEffectContextHandle EffectContext;
	EffectContext.AddInstigator(this, this);

	// 3) Def + Context + Level 三参构造，Def 正确绑定
	FGameplayEffectSpecHandle SpecHandle(new FGameplayEffectSpec(EffectDef, EffectContext, 1.f));
	if (!SpecHandle.Data.IsValid()) return;

	// SetByCaller 传值：GE 蓝图中 Magnitude 需配置为 SetByCaller + Tag "Data.Damage"
	SpecHandle.Data->SetSetByCallerMagnitude(HelloGameplayTags::Data_Damage, DamageAmount);

	// 应用到目标 → 触发目标 PostGameplayEffectExecute 的 Damage 分支：
	// 扣 Health、广播 OnHealthChanged、重置 Damage = 0
	TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	// 记录已伤害（同一挥砍对同一敌人只结算一次）
	HitEnemiesThisAttack.Add(TargetActor);

	// 真正打中才震：镜头冲击由打击帧通知上膛，这里确认命中后消费（同一刀只震一次）
	TryConsumeCameraPunch(TargetActor);

	// “命中才生效”的技能镜头同理：区间通知只上膛，真正命中才推镜头
	TryConsumeSkillCameraOnHit();

	// Instant GE 同步执行完毕，此时读到的是扣血后的新血量
	UE_LOG(LogTemp, Warning, TEXT("[%s] 对 [%s] 造成 %.0f 点伤害 | 敌人血量: %.0f/%.0f"),
		*GetName(), *TargetActor->GetName(), DamageAmount,
		TargetCharacter->GetHealth(), TargetCharacter->GetMaxHealth());
}

void AHelloCharacterBase::ClearHitEnemies()
{
	HitEnemiesThisAttack.Empty();
}

// ---- 时间减速（完美闪避）----

void AHelloCharacterBase::SlowTime(float Scale, float Duration)
{
	if (Scale <= 0.f || Duration <= 0.f) return;

	// CustomTimeDilation 降低该 Actor 的整体时间流速（动画/移动/组件 Tick 全部变慢）
	CustomTimeDilation = Scale;

	// 恢复计时挂在世界计时器上——世界时间不受 CustomTimeDilation 影响，到点准时恢复；
	// 先 Clear 再 Set：连续两次完美闪避时以最新一次为准，避免旧计时器提前恢复
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SlowTimeTimerHandle);
		World->GetTimerManager().SetTimer(SlowTimeTimerHandle, this,
			&AHelloCharacterBase::RestoreTimeDilation, Duration, false);
	}

	UE_LOG(LogTemp, Warning, TEXT("[%s] 时间减速至 %.2f 倍，持续 %.2f 秒"),
		*GetName(), Scale, Duration);
}

void AHelloCharacterBase::RestoreTimeDilation()
{
	CustomTimeDilation = 1.f;
	UE_LOG(LogTemp, Warning, TEXT("[%s] 时间恢复正常"), *GetName());
}
