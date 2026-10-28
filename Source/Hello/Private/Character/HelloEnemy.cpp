// Copyright Hello Project. All Rights Reserved.

#include "Character/HelloEnemy.h"
#include "Character/HelloCharacterBase.h"
#include "UI/EnemyHealthBarWidget.h"
#include "Components/WidgetComponent.h"
#include "AbilitySystem/AS/HelloAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayTags/HelloGameplayTags.h"

AHelloEnemy::AHelloEnemy()
{
	// 头顶血条组件：屏幕空间（始终面向相机、不随距离缩放），无碰撞
	HealthBarWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("EnemyHealthBar"));
	// 显式挂到角色根组件下——旧蓝图实例（组件加入 C++ 类之前创建的）会丢自动挂接信息，导致组件落世界原点
	HealthBarWidgetComponent->SetupAttachment(GetRootComponent());
	HealthBarWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 120.f));   // 头顶上方
	HealthBarWidgetComponent->SetDrawSize(FVector2D(120.f, 12.f));
	HealthBarWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthBarWidgetComponent->SetWidgetClass(UEnemyHealthBarWidget::StaticClass());
}

void AHelloEnemy::BeginPlay()
{
	Super::BeginPlay();

	InitHealthBar();
}

void AHelloEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 注销血量回调，防止悬挂
	UnregisterHealthChanged(HealthChangedHandle);

	// 停掉测试攻击计时器
	StopLoopAttack();

	Super::EndPlay(EndPlayReason);
}

// ---- 攻击测试（完美闪避验证用）----

void AHelloEnemy::StartLoopAttack()
{
	if (!AbilitySystemComponent) return;

	// 防重复启动
	if (GetWorldTimerManager().IsTimerActive(LoopAttackTimerHandle)) return;

	GetWorldTimerManager().SetTimer(LoopAttackTimerHandle, this,
		&AHelloEnemy::LoopAttackTick, TestAttackInterval, true);

	UE_LOG(LogTemp, Warning, TEXT("[%s] 开始循环攻击测试，间隔 %.1f 秒"), *GetName(), TestAttackInterval);
}

void AHelloEnemy::StopLoopAttack()
{
	GetWorldTimerManager().ClearTimer(LoopAttackTimerHandle);
}

void AHelloEnemy::LoopAttackTick()
{
	if (!AbilitySystemComponent) return;

	// 普攻 GA 已在运行（连段中/窗口期）则跳过本次，避免 TryActivate 无效调用
	if (AbilitySystemComponent->HasMatchingGameplayTag(HelloGameplayTags::Status_Combo_Attacking))
	{
		return;
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(HelloGameplayTags::Ability_Combo_NormalAttack);
	AbilitySystemComponent->TryActivateAbilitiesByTag(AbilityTags);
}

void AHelloEnemy::InitHealthBar()
{
	// Widget 由组件按构造函数里设的 WidgetClass 自行创建（标准路径，渲染严格跟随组件变换）
	// 不再手动 CreateWidget + SetWidget：非标准时序曾导致血条脱离组件渲染在世界原点
	HealthBarWidget = Cast<UEnemyHealthBarWidget>(HealthBarWidgetComponent ? HealthBarWidgetComponent->GetWidget() : nullptr);
	if (!HealthBarWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HelloEnemy] 组件未自建 Widget，血条不可用（检查 WidgetClass 是否为 EnemyHealthBarWidget）"));
	}

	if (HealthBarWidget)
	{
		HealthBarWidget->UpdateHealthBar(GetHealth(), GetMaxHealth());
	}

	// 调试日志：敌人位置 vs 血条组件世界位置（位置异常时的定位手段）
	if (HealthBarWidgetComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HelloEnemy] 位置对比: Actor=%s, HealthBar=%s"),
			*GetActorLocation().ToString(), *HealthBarWidgetComponent->GetComponentLocation().ToString());
	}

	// 调试日志：确认组件/Widget/类/初始血量都到位
	/*UE_LOG(LogTemp, Warning, TEXT("[HelloEnemy] InitHealthBar: Comp=%d, Widget=%d, WidgetClass=%s, HP=%.0f/%.0f"),
		HealthBarWidgetComponent != nullptr,
		HealthBarWidget != nullptr,
		(HealthBarWidgetComponent && HealthBarWidgetComponent->GetWidgetClass())
			? *HealthBarWidgetComponent->GetWidgetClass()->GetName() : TEXT("None"),
		GetHealth(), GetMaxHealth());*/

	// 注册血量变化回调（GE 伤害路径会在服务器广播 OnHealthChanged，客户端走 OnRep 也广播）
	HealthChangedHandle = RegisterHealthChanged(
		FOnAttributeChanged::FDelegate::CreateUObject(this, &AHelloEnemy::OnHealthChanged_Internal));
}

void AHelloEnemy::OnHealthChanged_Internal(FGameplayAttribute Attribute, float NewValue, float OldValue, AActor* InstigatorActor)
{
	// 调试日志：确认回调链路通到了敌人身上
	UE_LOG(LogTemp, Warning, TEXT("[HelloEnemy] OnHealthChanged: %s %.0f -> %.0f (Widget=%d)"),
		*Attribute.GetName(), OldValue, NewValue, HealthBarWidget != nullptr);

	if (!HealthBarWidget) return;

	HealthBarWidget->UpdateHealthBar(NewValue, GetMaxHealth());

	// 死亡时隐藏血条
	if (HealthBarWidgetComponent)
	{
		HealthBarWidgetComponent->SetVisibility(NewValue > 0.f);
	}
}
