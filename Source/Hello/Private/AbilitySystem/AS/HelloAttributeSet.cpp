// Copyright Hello Project. All Rights Reserved.

#include "AbilitySystem/AS/HelloAttributeSet.h"
#include "GameplayEffect.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UHelloAttributeSet::UHelloAttributeSet()
{
	// 设置初始默认值（这些值会被 GE_InitAttributes 覆盖，这里是兜底）
	Health.SetBaseValue(100.f);
	Health.SetCurrentValue(100.f);

	MaxHealth.SetBaseValue(100.f);
	MaxHealth.SetCurrentValue(100.f);

	Attack.SetBaseValue(20.f);
	Attack.SetCurrentValue(20.f);

	Defense.SetBaseValue(10.f);
	Defense.SetCurrentValue(10.f);

	Damage.SetBaseValue(0.f);
	Damage.SetCurrentValue(0.f);
}

void UHelloAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// 只在本地处理（服务器或单机）
	if (GetOwningActor()->GetNetMode() == NM_Client)
	{
		return;
	}

	// 认识修正：PostGameplayEffectExecute 是"事后通知"——
	// 引擎执行 Instant GE 时已把本次修改写入属性，这里只做事后钳制与广播，
	// 绝不再用 Magnitude 叠加（旧写法 OldValue + Magnitude 会双倍结算）
	// 手动写回时 Base/Current 必须一起写，否则 VALUE_GETTER（读 Current）会读到旧值

	// 获取被修改的属性
	const FGameplayAttribute ModifiedAttribute = Data.EvaluatedData.Attribute;

	// 获取修改前后的值
	float OldValue = 0.f;
	float NewValue = 0.f;

	// 处理 Health 属性变化（引擎已应用，这里只钳制 [0, MaxHealth]）
	if (ModifiedAttribute == GetHealthAttribute())
	{
		NewValue = Health.GetCurrentValue();
		// 加法修改可由现值反推旧值（当前所有 GE 均为 Additive，够用）
		OldValue = NewValue - Data.EvaluatedData.Magnitude;

		const float ClampedHealth = FMath::Clamp(NewValue, 0.f, MaxHealth.GetCurrentValue());
		if (!FMath::IsNearlyEqual(ClampedHealth, NewValue))
		{
			NewValue = ClampedHealth;
			Health.SetBaseValue(NewValue);
			Health.SetCurrentValue(NewValue);
		}
	}
	// 处理 MaxHealth 属性变化（引擎已应用，钳制最小值并联动截断 Health）
	else if (ModifiedAttribute == GetMaxHealthAttribute())
	{
		NewValue = MaxHealth.GetCurrentValue();
		OldValue = NewValue - Data.EvaluatedData.Magnitude;

		if (NewValue < 1.f)
		{
			NewValue = 1.f;
		}
		MaxHealth.SetBaseValue(NewValue);
		MaxHealth.SetCurrentValue(NewValue);

		// MaxHealth 缩水导致 Health 超上限时截断（Base/Current 一起写并广播）
		const float CurrentHealth = Health.GetCurrentValue();
		if (CurrentHealth > NewValue)
		{
			Health.SetBaseValue(NewValue);
			Health.SetCurrentValue(NewValue);
			BroadcastAttributeChange(GetHealthAttribute(), NewValue, CurrentHealth);
		}
	}
	// 处理 Attack 属性变化（引擎已应用，钳制非负）
	else if (ModifiedAttribute == GetAttackAttribute())
	{
		NewValue = Attack.GetCurrentValue();
		OldValue = NewValue - Data.EvaluatedData.Magnitude;

		NewValue = FMath::Max(NewValue, 0.f);
		Attack.SetBaseValue(NewValue);
		Attack.SetCurrentValue(NewValue);
	}
	// 处理 Defense 属性变化（引擎已应用，钳制非负）
	else if (ModifiedAttribute == GetDefenseAttribute())
	{
		NewValue = Defense.GetCurrentValue();
		OldValue = NewValue - Data.EvaluatedData.Magnitude;

		NewValue = FMath::Max(NewValue, 0.f);
		Defense.SetBaseValue(NewValue);
		Defense.SetCurrentValue(NewValue);
	}
	// 处理 Damage 元属性：读现值消费（引擎已写入），立刻清零，再扣 Health
	else if (ModifiedAttribute == GetDamageAttribute())
	{
		OldValue = Damage.GetCurrentValue();   // 本次伤害（引擎已写入，含本次 GE 的量）
		const float LocalDamage = OldValue;

		// 元属性立刻清零（Base/Current 一起），避免残留影响下次结算
		Damage.SetBaseValue(0.f);
		Damage.SetCurrentValue(0.f);

		if (LocalDamage > 0.f)
		{
			const float OldHealth = Health.GetCurrentValue();
			NewValue = FMath::Clamp(OldHealth - LocalDamage, 0.f, MaxHealth.GetCurrentValue());

			Health.SetBaseValue(NewValue);
			Health.SetCurrentValue(NewValue);

			BroadcastAttributeChange(GetHealthAttribute(), NewValue, OldHealth);
		}
		else
		{
			NewValue = 0.f;
		}

		BroadcastAttributeChange(GetDamageAttribute(), 0.f, OldValue);
		return;  // Damage 分支已自行完成广播
	}
	else
	{
		// 不是我们关心的属性，不处理
		return;
	}

	// 触发属性变化通知
	BroadcastAttributeChange(ModifiedAttribute, NewValue, OldValue);
}

void UHelloAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	// 属性值变化，触发外部回调
	BroadcastAttributeChange(GetHealthAttribute(), Health.GetCurrentValue(), OldHealth.GetCurrentValue());
}

void UHelloAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	BroadcastAttributeChange(GetMaxHealthAttribute(), MaxHealth.GetCurrentValue(), OldMaxHealth.GetCurrentValue());
}

void UHelloAttributeSet::OnRep_Attack(const FGameplayAttributeData& OldAttack)
{
	BroadcastAttributeChange(GetAttackAttribute(), Attack.GetCurrentValue(), OldAttack.GetCurrentValue());
}

void UHelloAttributeSet::OnRep_Defense(const FGameplayAttributeData& OldDefense)
{
	BroadcastAttributeChange(GetDefenseAttribute(), Defense.GetCurrentValue(), OldDefense.GetCurrentValue());
}

void UHelloAttributeSet::OnRep_Damage(const FGameplayAttributeData& OldDamage)
{
	BroadcastAttributeChange(GetDamageAttribute(), Damage.GetCurrentValue(), OldDamage.GetCurrentValue());
}

void UHelloAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float NewValue, float OldValue)
{
	// 钳制辅助：NewValue 由调用方给出（视为引擎已应用后的值），此处只做钳制与广播，
	// 不再用 Magnitude 叠加；手动写回时 Base/Current 成对写
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, MaxHealth.GetCurrentValue());
		Health.SetBaseValue(NewValue);
		Health.SetCurrentValue(NewValue);
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.f);
		MaxHealth.SetBaseValue(NewValue);
		MaxHealth.SetCurrentValue(NewValue);

		// MaxHealth 缩水导致 Health 超上限时截断（Base/Current 一起写并广播）
		const float CurrentHealth = Health.GetCurrentValue();
		if (CurrentHealth > NewValue)
		{
			Health.SetBaseValue(NewValue);
			Health.SetCurrentValue(NewValue);
			BroadcastAttributeChange(GetHealthAttribute(), NewValue, CurrentHealth);
		}
	}
	else if (Attribute == GetAttackAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
		Attack.SetBaseValue(NewValue);
		Attack.SetCurrentValue(NewValue);
	}
	else if (Attribute == GetDefenseAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
		Defense.SetBaseValue(NewValue);
		Defense.SetCurrentValue(NewValue);
	}

	// 广播属性变化
	BroadcastAttributeChange(Attribute, NewValue, OldValue);
}

void UHelloAttributeSet::BroadcastAttributeChange(const FGameplayAttribute& Attribute, float NewValue, float OldValue)
{
	// 广播通用回调
	if (OnAnyAttributeChanged.IsBound())
	{
		OnAnyAttributeChanged.Broadcast(Attribute, NewValue, OldValue, GetOwningActor());
	}

	// 广播特定属性回调
	if (Attribute == GetHealthAttribute())
	{
		if (OnHealthChanged.IsBound())
		{
			OnHealthChanged.Broadcast(Attribute, NewValue, OldValue, GetOwningActor());
		}
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		if (OnMaxHealthChanged.IsBound())
		{
			OnMaxHealthChanged.Broadcast(Attribute, NewValue, OldValue, GetOwningActor());
		}
	}
	else if (Attribute == GetAttackAttribute())
	{
		if (OnAttackChanged.IsBound())
		{
			OnAttackChanged.Broadcast(Attribute, NewValue, OldValue, GetOwningActor());
		}
	}
	else if (Attribute == GetDefenseAttribute())
	{
		if (OnDefenseChanged.IsBound())
		{
			OnDefenseChanged.Broadcast(Attribute, NewValue, OldValue, GetOwningActor());
		}
	}
	else if (Attribute == GetDamageAttribute())
	{
		if (OnDamageChanged.IsBound())
		{
			OnDamageChanged.Broadcast(Attribute, NewValue, OldValue, GetOwningActor());
		}
	}
}

bool UHelloAttributeSet::IsDead() const
{
	return Health.GetCurrentValue() <= 0.f;
}

void UHelloAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UHelloAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UHelloAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UHelloAttributeSet, Attack, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UHelloAttributeSet, Defense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UHelloAttributeSet, Damage, COND_None, REPNOTIFY_Always);
}
