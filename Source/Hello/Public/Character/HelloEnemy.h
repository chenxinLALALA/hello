// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/HelloCharacterBase.h"
#include "HelloEnemy.generated.h"

class UWidgetComponent;
class UEnemyHealthBarWidget;

/**
 * 敌人：继承角色基类，头顶挂血条 Widget，由 OnHealthChanged 委托驱动刷新
 */
UCLASS()
class HELLO_API AHelloEnemy : public AHelloCharacterBase
{
	GENERATED_BODY()

public:
	AHelloEnemy();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 头顶血条组件（屏幕空间，始终面向相机） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UWidgetComponent> HealthBarWidgetComponent;

	// ---- 攻击测试（完美闪避验证用，正式敌人 AI 接入后删除）----

	/** 开始循环攻击：按攻击间隔持续激活普攻 GA（GA 未运行才激活，连段/冷却逻辑照常走 GAS） */
	UFUNCTION(BlueprintCallable, Category = "Combat|Test")
	void StartLoopAttack();

	/** 停止循环攻击 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Test")
	void StopLoopAttack();

	/** 攻击间隔（秒） */
	UPROPERTY(EditAnywhere, Category = "Combat|Test")
	float TestAttackInterval = 2.0f;

private:
	/** 初始化血条：取 Widget、同步初始血量、注册属性回调 */
	void InitHealthBar();

	/** 循环攻击计时器回调：普攻 GA 未在运行则按 Tag 激活 */
	void LoopAttackTick();

	/** 血量变化回调（AttributeSet 的 OnHealthChanged 广播 → 刷新血条） */
	void OnHealthChanged_Internal(FGameplayAttribute Attribute, float NewValue, float OldValue, AActor* InstigatorActor);

	/** 回调句柄（EndPlay 注销用） */
	FDelegateHandle HealthChangedHandle;

	/** 缓存的血条 Widget */
	UPROPERTY()
	TObjectPtr<UEnemyHealthBarWidget> HealthBarWidget = nullptr;

	/** 循环攻击计时器 */
	FTimerHandle LoopAttackTimerHandle;
};
