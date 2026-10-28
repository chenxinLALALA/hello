// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Delegates/DelegateSignatureImpl.inl"
#include "HelloAttributeSet.generated.h"

#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)


// 属性变化回调委托，用于外部监听（普通多播委托，支持 FDelegateHandle）
// 参数顺序：Attribute, NewValue, OldValue, Instigator
DECLARE_MULTICAST_DELEGATE_FourParams(FOnAttributeChanged, FGameplayAttribute, float, float, AActor*);

/**
 * 角色属性集：Health/MaxHealth/Attack/Defense/Damage
 * 提供 GAS 体系下的属性管理和属性变化通知机制
 * Damage 是临时属性，用于 GameplayEffect 计算伤害后自动应用到 Health
 */
UCLASS()
class HELLO_API UHelloAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UHelloAttributeSet();

	// ---- 属性定义 ----

	/** 当前生命值 */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UHelloAttributeSet, Health)

	/** 最大生命值 */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UHelloAttributeSet, MaxHealth)
	
	/** 攻击力 */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Attack)
	FGameplayAttributeData Attack;
	ATTRIBUTE_ACCESSORS(UHelloAttributeSet, Attack)

	/** 防御力 */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Defense)
	FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS(UHelloAttributeSet, Defense)

	/** 伤害（临时属性，用于 GameplayEffect 计算伤害后应用到 Health） */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Damage)
	FGameplayAttributeData Damage;
	ATTRIBUTE_ACCESSORS(UHelloAttributeSet, Damage)

	// ---- 外部属性变化回调 ----

	/** 生命值变化时广播 */
	FOnAttributeChanged OnHealthChanged;

	/** 最大生命值变化时广播 */
	FOnAttributeChanged OnMaxHealthChanged;

	/** 攻击力变化时广播 */
	FOnAttributeChanged OnAttackChanged;

	/** 防御力变化时广播 */
	FOnAttributeChanged OnDefenseChanged;

	/** 伤害变化时广播 */
	FOnAttributeChanged OnDamageChanged;

	/** 任意属性变化时广播（通用） */
	FOnAttributeChanged OnAnyAttributeChanged;

	// ---- 虚函数重写 ----

	/** 属性变化时的内部回调（处理死亡、属性截断等逻辑） */
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	/** 网络同步回调：Health 变化时触发 */
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldHealth);

	/** 网络同步回调：MaxHealth 变化时触发 */
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);

	/** 网络同步回调：Attack 变化时触发 */
	UFUNCTION()
	void OnRep_Attack(const FGameplayAttributeData& OldAttack);

	/** 网络同步回调：Defense 变化时触发 */
	UFUNCTION()
	void OnRep_Defense(const FGameplayAttributeData& OldDefense);

	/** 网络同步回调：Damage 变化时触发 */
	UFUNCTION()
	void OnRep_Damage(const FGameplayAttributeData& OldDamage);

	// ---- 辅助方法 ----

	/** 检查角色是否死亡 */
	UFUNCTION(BlueprintPure, Category = "Attributes")
	bool IsDead() const;

protected:
	/** 触发属性变化回调（内部调用，统一处理广播和逻辑） */
	void ClampAttribute(const FGameplayAttribute& Attribute, float NewValue, float OldValue);

private:
	/** 根据属性类型广播对应的回调 */
	void BroadcastAttributeChange(const FGameplayAttribute& Attribute, float NewValue, float OldValue);

	/** 属性初始化时是否正在设置默认值（避免初始化时触发逻辑） */
	bool bInitializing = false;
};
