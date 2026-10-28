# 武器碰撞伤害 + 敌人血条系统（已实现）

> 状态：**已实现并验证通过**（2026-09-08，日志实测：20 伤害/跳、血量 100→0、血条同步递减、死亡隐藏）
> 依赖：GAS（GameplayAbilities）、UMG；模块依赖见 `Hello.Build.cs`（含 GameplayAbilities/GameplayTags/AnimationCore/UMG/Slate/SlateCore）

## 一、功能总览

| 功能 | 说明 |
|---|---|
| Damage 元属性 | GE 伤害走 `Damage` 临时属性中转，`PostGameplayEffectExecute` 消费后扣 Health 并自动清零 |
| 武器碰撞伤害 | 攻击动画指定帧开启武器碰撞，碰到敌人按当前连段段的 GE 结算伤害 |
| 敌人头顶血条 | 纯 C++ 进度条（无需 WBP），屏幕空间跟随相机，由属性回调实时驱动 |

## 二、伤害链路（核心数据流）

```
攻击动画帧
  → AnimNotify_EnableWeaponTrace          （开碰撞，清空已命中记录）
    → 角色 Mesh Overlap 敌人
      → ApplyDamageToActor                （攻击者侧）
          ① 读基类 ComboConfig 第 CurrentComboSegment 段的 DamageGE
          ② 三参构造 FGameplayEffectSpec（Def=CDO, Context含攻击者）
          ③ SetSetByCallerMagnitude(Data.Damage, 攻击力)
          ④ 目标 ASC->ApplyGameplayEffectSpecToSelf
  → AnimNotify_DisableWeaponTrace         （动画后段关碰撞）
```

目标侧（`HelloAttributeSet::PostGameplayEffectExecute` 的 Damage 分支）：

```
引擎已把伤害写入 Damage → 读现值 LocalDamage
  → Damage 清零（Base+Current）          （元属性即用即清）
  → Health = Clamp(Health - LocalDamage, 0, MaxHealth)（Base+Current 成对写入）
  → 广播 OnHealthChanged → 血条刷新
```

**伤害值** = 攻击者 Attack 属性（20），经 SetByCaller Tag `Data.Damage` 传入 GE。
Defense 属性暂未参与公式；后续接入建议用 GE 的 ExecCalc（资产层改，调用代码不动）。

## 三、敌人血条链路

```
OnHealthChanged 广播
  → AHelloEnemy::OnHealthChanged_Internal（BeginPlay 注册，EndPlay 注销）
    → UEnemyHealthBarWidget::UpdateHealthBar(Hp, MaxHp)
      → ProgressBar 百分比 = Hp/MaxHp
血量 ≤ 0 → 血条组件隐藏
```

## 四、文件清单

| 文件 | 职责 |
|---|---|
| `Public/Private/AbilitySystem/AS/HelloAttributeSet` | 属性集：Health/MaxHealth/Attack/Defense/Damage + GE 事后钳制与广播；`ClampAttribute` 为通用钳制辅助（同"事后钳制"模式，Base/Current 成对写，暂无调用方，预留给外部手动设值场景） |
| `Public/Private/Animation/AnimNotify_EnableWeaponTrace` | 动画通知：开启武器碰撞 |
| `Public/Private/Animation/AnimNotify_DisableWeaponTrace` | 动画通知：关闭武器碰撞 |
| `Public/Private/Character/HelloCharacterBase` | 碰撞开关/Overlap 回调/ApplyDamageToActor/已命中去重（`TSet HitEnemiesThisAttack`） |
| `Public/Private/Character/HelloEnemy` | 头顶血条组件 + 属性回调注册（`CurrentComboSegment` 段号在基类） |
| `Public/Private/UI/EnemyHealthBarWidget` | 纯 C++ 血条（`RebuildWidget` 构建进度条） |
| `Public/Private/GameplayTags/HelloGameplayTags` | 含 SetByCaller 数据 Tag `Data.Damage` |

## 五、编辑器侧配置（必备）

1. **GE_Damage 蓝图**：Duration Policy = Instant；Modifiers → Add →
   Attribute = `HelloAttributeSet Damage`，Op = Additive，
   Magnitude = SetByCaller，Data Tag = `Data.Damage`
2. **ComboConfig DataAsset**：各段 `DamageGE` 槽填入 GE（每段可配不同 GE）
3. **攻击动画**：有效帧添加 `EnableWeaponTrace`，结束后添加 `DisableWeaponTrace`
4. **敌人蓝图**：父类 = `HelloEnemy`；可调 EnemyHealthBar 组件的
   RelativeLocation（默认头顶 +120）/ DrawSize（默认 120×12 屏幕像素）

## 六、关键实现认识（避坑）

- `PostGameplayEffectExecute` 是**事后通知**：引擎已应用修改，分支内再做 `OldValue + Magnitude` 会双倍结算；只读现值做钳制/消费。
- 手动改属性必须 `SetBaseValue` + `SetCurrentValue` 成对写，否则 VALUE_GETTER/IsDead 读到旧值。
- GA 外构造 Spec 用三参 `FGameplayEffectSpec(Def, Context, Level)`，空默认构造的 Spec.Def 为空会崩溃。
- C++ 挂组件用 `SetupAttachment(GetRootComponent())` 显式挂接，防旧蓝图实例组件脱挂到世界原点。
