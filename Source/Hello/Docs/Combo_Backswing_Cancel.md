# GAS 连段后摇取消（Backswing Cancel）· 实现方案

> 目标：在现有 5 段平A连段（`GA_NormalAttack`）上加入**后摇取消**能力。
> 需求（已与用户确认）：
> - R1 攻击动画播放中若已确认接下一段（攻击键已按下、预输入存在），动画走到**后摇起点**时直接剪掉后摇，无缝接下一段；
> - R2 后摇阶段内按下攻击键 → 同样**立即剪断**剩余后摇接下一段（不必等动画自然播完）；
> - R3 动画进入后摇阶段后被**闪避打断** → 该段攻击视作已完成：连段记忆存 **N+1**（而非现状的 N），闪避后下一次攻击直接从第 N+1 段开始；
> - R4 （**2026-09-12 需求变更：连段成环**）第 5 段后摇同样可剪断/窗口内推进——循环回第 1 段接下一套；后摇期闪避打断记忆第 1 段。
>
> 本文只给实现思路与分阶段任务，不修改现有代码。

---

## 0. 现状与接入点（基于现有代码梳理）

| 现有结构 | 位置 | 对本功能的影响 |
|---|---|---|
| 动态 Montage：`CreateSlotAnimationAsDynamicMontage(Seq, "DefaultSlot", 0.0f, 0.0f, 1.0f)` | `GA_NormalAttack::PlayCurrentSegment()` | **BlendIn/Out 均为 0**：剪断=硬切，需要给一个小混合时长；单 Section 动态 Montage **无处可 JumpToSection**，"跳过后摇"只能用"提前 Stop + 短混合"实现 |
| 预输入：`bHasPendingInput` | `GA_NormalAttack` | R1 的"确认还有下一段"判定直接复用此标志，零新增状态 |
| 窗口推进 / 宽容期 | `OnMontageCompleted()` 开窗口 GE | 后摇取消后，整段播完才推进的旧路径退化为**兜底**（动画漏配通知时仍工作） |
| 闪避打断链 | `GA_Dodge` CancelAbilitiesWithTag → `OnMontageCancelled()` → `EndAbility(bWasCancelled=true)` → `SaveComboMemory(N)` | R3 只需在 `EndAbility` 的记忆段号计算里加"后摇偏移 +1" |
| 事件链路 | `HelloPlayerController` 用 `ASC->HandleGameplayEvent(Event_Combo_AttackPressed, &Payload)`；GA 用 `WaitGameplayEvent` 监听 | 后摇通知沿用**同款 GameplayEvent 链路**，风格统一 |
| 动画通知先例 | `AnimNotify_Enable/DisableWeaponTrace`（Cast 到 `AHelloCharacterBase` 调角色函数） | 新通知类照此风格写；`AHelloCharacterBase::GetAbilitySystemComponent()` 已有 override，可直接取 ASC |

**关键约束：动画是 `UAnimSequence`（ComboConfig 里每段配一个），运行时临时转 Montage。** 后摇区间的标记只能放在 AnimSequence 资产上（通知随资产走），或放在 ComboConfig 里配秒数（见第 2 节，已否决）。

---

## 1. 总体思路

把"后摇"从"动画尾巴的一部分"提升为**连段状态机里的一个显式阶段**：

```
第 N 段动画时间轴：
 ├─ 前摇 ─┬─ 有效帧（伤害窗口）─┬────────── 后摇 ──────────┤
          │ EnableWeaponTrace   │↑Backswing区间通知起点      │ 动画尾(BlendOut)
          │     …伤害检测…       │(建议摆在 DisableWeaponTrace│
          │                     │ 之后，见 4.3 摆放建议)     │
          │                     │                            │
 输入状态  │ 按下→bHasPendingInput=true                       │
          │                     │←通知到达(事件)：            │
          │                     │  有预输入→剪断→第N+1段       │
          │                     │  无预输入→后摇正常播         │
          │                     │  (后摇中再按→立即剪断)       │ OnMontageCompleted
          │                     │                            │→开窗口GE(现状不变)
 闪避打断  │ 记忆 N（现状不变）   │ 记忆 N+1（本方案新增）       │
```

核心只有一个新概念：**`Event.Combo.BackswingStart` 游戏事件**，由动画上的区间通知发出，由 GA 消费。收到事件 = "动画进入后摇"，GA 在此做两件事之一：剪断推进（有预输入；成环变更后第 5 段剪断则循环回第 1 段）、仅标记后摇状态（无预输入，供闪避记忆判定）。

---

## 2. 技术选型：为什么用动画通知（而不是配置秒数 + 定时器）

| 方案 | 做法 | 否决/采纳理由 |
|---|---|---|
| **A. AnimNotifyState 区间通知**（采纳） | `AnimNotifyState_ComboBackswing` 摆在每段 AnimSequence 上，NotifyBegin 发 GameplayEvent | 通知**跟随动画时间轴**：变速时自动同步。项目已有 Enable/DisableWeaponTrace 同款先例，资产即配置、所见即所得 |
| B. ComboConfig 配 `float BackswingStartTime` + GA 世界定时器 | 每段数据资产里填后摇起始秒数 | **世界 TimerManager 不吃角色 CustomTimeDilation**：完美闪避给攻击者挂 0.3 倍速时，动画放慢但定时器照常走 → 慢速期间定时器**提前触发**、后摇被剪早。这是硬伤 |
| C. 换成手工 Montage + Section 跳转 | 每段做带 `Attack/Recovery` 双 Section 的 Montage，剪断=JumpToSectionsEnd | 推翻现有"AnimSequence + 动态 Montage"资产流（闪避同款），编辑器工作量陡增，收益不明显 |

另：**"不播后摇"的实现 = 提前 Stop 当前动态 Montage + 短混合**。动态 Montage 单 Section 无处可跳，Stop(BlendOut) 是标准做法；混合时长从 0 提到可配置的 0.1s 左右，剪断不再硬切。

---

## 3. 新增 GameplayTag（C++ Native，追加进 HelloGameplayTags）

```text
Event.Combo.BackswingStart   # 后摇区间开始事件（AnimNotifyState → GA）
```

- 命名与 `Event.Combo.AttackPressed` 对齐，进 `HelloGameplayTags.h/.cpp` 的"游戏事件标签"分组；
- **可选（本期不做）**：`Status.Combo.Backswing` 挂 ASC 的状态 Tag。GA 内部 flag（`bInBackswing`）已够用；将来受击系统等要查询"目标是否在后摇"时再补，链路预留（NotifyEnd 处）。

---

## 4. 组件设计

### 4.1 `UAnimNotifyState_ComboBackswing`（新 C++ 类，照武器检测通知风格）

```cpp
// Public/Animation/AnimNotifyState_ComboBackswing.h
#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_ComboBackswing.generated.h"

/**
 * 动画区间通知：标记攻击动画的"后摇区间"。
 * NotifyBegin 时向角色 ASC 发送 Event.Combo.BackswingStart，
 * 由 GA_NormalAttack 决定剪断推进 / 仅记录后摇状态（供闪避记忆）。
 * 摆放建议（GA 侧有防御性收武器兜底，详见 4.3）：
 * - 建议位于 DisableWeaponTrace 之后（剪断会放弃之后的剩余伤害窗口）。
 */
UCLASS(Blueprintable, meta = (DisplayName = "Combo Backswing"))
class HELLO_API UAnimNotifyState_ComboBackswing : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("ComboBackswing"); }
};
```

```cpp
// Private/Animation/AnimNotifyState_ComboBackswing.cpp
#include "Animation/AnimNotifyState_ComboBackswing.h"
#include "AbilitySystemComponent.h"
#include "Character/HelloCharacterBase.h"
#include "GameplayTags/HelloGameplayTags.h"

void UAnimNotifyState_ComboBackswing::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// 与 AnimNotify_EnableWeaponTrace 同款取角色方式
	AHelloCharacterBase* Character = Cast<AHelloCharacterBase>(MeshComp->GetOwner());
	if (!Character) return;

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!ASC) return;

	// 与 PlayerController 发 Event.Combo.AttackPressed 同款链路
	FGameplayEventData Payload;
	Payload.EventTag = HelloGameplayTags::Event_Combo_BackswingStart;
	ASC->HandleGameplayEvent(HelloGameplayTags::Event_Combo_BackswingStart, &Payload);
}
```

说明：
- **NotifyEnd 本期不发事件**。后摇结束=动画播完，`OnMontageCompleted` 已覆盖该时刻（重置标志见 4.2）。将来若做"后摇 Tag 回收/最后几帧不可剪"，在 NotifyEnd 加一条事件即可，链路已预留。
- `NotifyBegin` 的签名以引擎头文件为准（UE 5.5 带第四参 `FAnimNotifyEventReference`）；若编译报错按编译器提示修，不凭记忆断言（项目既有原则）。
- `WaitGameplayEvent` 单任务只监听单 Tag，所以 GA 里需要**第二个监听任务**实例，见下。

### 4.2 `GA_NormalAttack` 改动

#### 新增成员（h）

```cpp
/** 当前段是否处于后摇区间（Event.Combo.BackswingStart 驱动；PlayCurrentSegment/OnMontageCompleted/EndAbility 重置） */
bool bInBackswing = false;

/** 换段/剪断期间抑制 Montage 回调（EndTask→StopMontage 可能同步触发 OnBlendOut/OnInterrupted，防重入） */
bool bSuppressMontageCallbacks = false;

/** 等待后摇开始事件任务（WaitGameplayEvent 单 Tag，与 WaitInputTask 各一个实例） */
UPROPERTY()
UAbilityTask_WaitGameplayEvent* WaitBackswingTask = nullptr;

/** 段间混合时长（动态 Montage 的 BlendIn/Out；0=硬切）。剪断与自然换段共用 */
UPROPERTY(EditDefaultsOnly, Category = "Combo", meta = (ClampMin = 0.0f))
float SegmentBlendTime = 0.1f;
```

#### 流程改动（逐处）

**① `PlayCurrentSegment()`**
- 开头重置 `bInBackswing = false;`、`bSuppressMontageCallbacks = false;`
- 动态 Montage 混合参数 `0.0f, 0.0f` → `SegmentBlendTime, SegmentBlendTime`；
- 照 `WaitInputTask` 模式启动第二个监听：

```cpp
WaitBackswingTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
	this, HelloGameplayTags::Event_Combo_BackswingStart);
WaitBackswingTask->EventReceived.AddDynamic(this, &UGA_NormalAttack::OnBackswingStarted);
WaitBackswingTask->ReadyForActivation();
```

**② 新回调 `OnBackswingStarted(FGameplayEventData)`**（R1 核心）

```cpp
void UGA_NormalAttack::OnBackswingStarted(FGameplayEventData EventData)
{
	if (!WaitBackswingTask || !IsActive()) return;

	bInBackswing = true;

	// 有预输入 → 剪掉后摇直接推进（成环变更后最终版无 < 5 守卫，第 5 段剪断环回第 1 段）
	if (bHasPendingInput)
	{
		bHasPendingInput = false;
		ScheduleAdvance();   // 复用下一帧调度，避免在通知回调栈里 EndTask Montage
	}
	// else：无预输入 → 只标记状态，后摇正常播（flag 供闪避记忆判定）
}
```

**③ `OnAttackPressed()`** —— 在 `bHasWindow` 分支之后、`ResetPending` 分支之前插入（R2 核心）：

```cpp
// 后摇区间内按下 → 即时剪断推进（成环变更后最终版无 < 5 守卫）
if (bInBackswing)
{
	bHasPendingInput = false;  // 消费预输入
	ScheduleAdvance();
	return;
}
```

**④ `OnMontageCompleted()`** —— 函数开头加 `bInBackswing = false;`（动画播完=后摇结束；避免窗口期/超时死区内 stale flag 误触发即时推进）。原有的 `bHasPendingInput → ScheduleAdvance` 路径**保留不动**——它成为动画漏配通知时的兜底（整段播完才推进，等同现状）。

**⑤ `StartNextSegment()`** —— 剪断路径下 Montage 仍在播放，`EndTask` 会触发 `StopMontage(BlendOut)`，可能**同步**触发 `OnBlendOut/OnInterrupted`。在清理任务前置标志：

```cpp
// 剪断/换段期间抑制 Montage 回调（EndTask→StopMontage 可能同步触发回调，防重入）
bSuppressMontageCallbacks = true;
if (CurrentMontageTask)
{
	if (CurrentMontageTask->IsActive()) { CurrentMontageTask->EndTask(); }
	CurrentMontageTask = nullptr;
}
// WaitInputTask / WaitBackswingTask 清理照旧（同样带 IsActive 守卫）
...
PlayCurrentSegment();   // 内部重置 bSuppressMontageCallbacks = false
```

并在 `OnMontageCompleted()` / `OnMontageCancelled()` 开头加早退：

```cpp
if (bSuppressMontageCallbacks) return;
```

**⑥ `EndAbility()`**（R3 核心）—— 记忆段号计算加后摇偏移：

```cpp
int32 InterruptedIndex = 0;
if (bWasCancelled && !bComboMemorySaved)
{
	if (ComboMemoryGE)
	{
		InterruptedIndex = GetCurrentComboIndex();
		if (bInBackswing)
		{
			// 后摇期=本段攻击已交付：闪避打断视作完成本段，记忆推进到下一段；
			// 第 5 段循环回第 1 段（2026-09-12 成环变更）
			InterruptedIndex = (InterruptedIndex >= 5) ? 1 : InterruptedIndex + 1;
		}
	}
	...
}
```

同函数内：
- 清理任务处照 `WaitInputTask` 模式补 `WaitBackswingTask` 的 EndTask；
- 末尾重置 `bInBackswing = false;` 与 `bSuppressMontageCallbacks = false;`（后者覆盖 `StartNextSegment` 早退到 `EndCombo` 的路径，保证标志不跨连段残留）。
- `SaveComboMemory()` 的日志文案同步更新（如"后摇期打断：第 %d 段视作完成 → 记忆第 %d 段"），保留 Tag 即时校验输出。

### 4.3 编辑器侧（一次性资产配置）

每段攻击 `AnimSequence`（ComboConfig 里的 5 个资产）摆放 `Combo Backswing` 区间通知：

- 顺序建议（阶段 2 落地防御性收武器后，从"硬约束"降级为"建议"）：`EnableWeaponTrace` → `DisableWeaponTrace` → `ComboBackswing`。GA 已在剪断路径与 EndAbility 显式 `DisableWeaponTrace()` 兜底（幂等），即使后摇区间里还有武器检测窗口也不会残留热的碰撞体；但剪断会**放弃**剩余伤害窗口——若希望后摇伤害完整生效，仍应把 ComboBackswing 起点摆在最后一个 DisableWeaponTrace 之后；
- 区间起点 = 该段**最早允许取消**的时刻；区间拉到动画结尾；
- 漏摆的段自动退化为现状行为（整段播完才推进、闪避记 N），不崩溃、可渐进迁移——这也是把通知放资产上的迁移红利；
- 敌人动画（`HelloEnemy` 同一 GA）适用同样摆法。

---

## 5. 边界情况清单

| # | 场景 | 行为 | 依据 |
|---|---|---|---|
| 1 | 第 5 段有预输入 / 后摇中按键 / 窗口内推进 | 剪断或推进后**循环回第 1 段**接下一套（2026-09-12 成环变更，`StartNextSegment` 5→1） | T3.1b |
| 2 | 第 5 段后摇期闪避打断 | 记忆第 1 段（环回）——效果等同闪避后重新开套 | T3.2 |
| 3 | 攻击有效帧（后摇通知之前）闪避 | 记 N、闪避后重播第 N 段 | 现状不变（`bInBackswing=false`） |
| 4 | 完美闪避减速（攻击者 0.3 倍速）中进入后摇 | 通知随动画时间轴自动同步，剪断/记忆判定不漂移 | 选型 A 的核心论据 |
| 5 | 后摇通知与按键同帧到达 | 只推进一次 | `bAdvancePending` 去重（现有机制） |
| 6 | 剪断触发 EndTask→StopMontage 回调风暴 | 被 `bSuppressMontageCallbacks` 早退拦截 | 4.2-⑤ |
| 7 | 动画漏配通知 | 整段播完推进（`OnMontageCompleted` 预输入兜底）、闪避记 N | 4.3 渐进迁移 |
| 8 | 敌人侧（无输入事件） | 后摇不剪、正常播完，行为不变；将来 AI 发 `Event.Combo.AttackPressed` 即自动享剪断 | 事件驱动红利 |
| 9 | 闪避后连段记忆接回（按攻击键） | `PlayCurrentSegment` 现有 GE→Loose 接回路径零改动，直接播 N+1 段 | 与 R3 衔接 |
| 10 | 整段播完后的窗口/宽容期被闪避打断 | **记 N+1（T4.4 已改，2026-09-12 用户确认）**：动画已完整播完（完成度高于后摇剪断），视作完成才不语义倒挂；判定 = 后摇标志 或 窗口/宽容期 Tag 存在 | EndAbility 记忆块 |
| 11 | 剪断瞬间的资产通知（位移/音效/特效） | 后摇区间内位于剪断点之后的通知不再触发——后摇区间的通知本来就应视为"可丢弃"内容，美术摆放时知悉 | 4.3 |
| 12 | 后摇区间内还有武器检测窗口（实际动画存在） | 剪断跳过剪断点后的 Enable/Disable 通知 → 剩余伤害窗口被放弃；GA 在剪断路径与 EndAbility 显式 `DisableWeaponTrace()` 兜底（幂等），武器绝不残留、无幻影命中；闪避打断的同款既有隐患一并修复 | T2.8 |

---

## 6. 避坑记录（写代码前先读）

1. **不要在 `OnBackswingStarted` / `OnAttackPressed` 回调栈内直接 `EndTask` Montage**——通知、事件回调可能处在 AnimInstance 的通知分发栈里，同步停 Montage 有重入风险。一律走 `ScheduleAdvance()`（下一帧），这是现有代码已有的模式，沿用。
2. **`EndTask` 停在播 Montage 会触发什么回调要以实测为准**（引擎源码本会话不可读，项目原则：编译器/日志说了算）。防御式写法：抑制标志 + 两回调早退 + `bAdvancePending` 三层守卫，并在测试日志里确认剪断路径没有额外触发 `OnMontageCompleted/OnInterrupted`。
3. **`bInBackswing` 的重置点有三个**：`PlayCurrentSegment`（新段开始）、`OnMontageCompleted`（本段播完）、`EndAbility`（连段结束）。漏任何一个都会造成 stale flag 误判（见边界 #10）。
4. **Loose Tag / GE 计数减穿的老坑不涉及本功能**：记忆 N+1 只改 `SaveComboMemory(InterruptedIndex)` 的入参，"读段号→清 Loose→挂 GE"的顺序原样保留，不动。
5. 动态 Montage 的 BlendIn/Out 从 0 改为 `SegmentBlendTime` 会同时影响**自然换段**的过渡观感（原来硬切的地方现在有 0.1s 混合）——这是预期内的手感提升，但测试时注意别误判为"动画变慢"。

---

## 7. 分阶段落地任务

> 每阶段以**编译通过 + PIE 日志实测**为验收标准（项目惯例）。阶段 3 的验收依赖至少一段动画已摆通知，可与阶段 4 的 T4.1 穿插进行。

### 阶段 1 · C++ 基础设施（Tag + 通知类）

- [x] T1.1 `HelloGameplayTags.h/.cpp` 追加 `Event_Combo_BackswingStart`（`Event.Combo.BackswingStart`，进"游戏事件标签"分组）
- [x] T1.2 新建 `Public/Private/Animation/AnimNotifyState_ComboBackswing.h/.cpp`（4.1 骨架：NotifyBegin → 角色ASC → HandleGameplayEvent，含验证日志）
- [x] T1.3 编译通过；任选一段攻击动画临时摆上通知，PIE 打日志确认事件到达 GA（2026-09-12 用户实测：5/5 段触发）

**验收**：日志能看到 `Event.Combo.BackswingStart` 触达（GA 侧临时打印即可）。

### 阶段 2 · GA 后摇感知与剪断推进（R1 / R2）

- [x] T2.1 `GA_NormalAttack.h` 新增四成员：`bInBackswing` / `bSuppressMontageCallbacks` / `WaitBackswingTask` / `SegmentBlendTime`（4.2）
- [x] T2.2 `PlayCurrentSegment()`：重置两标志；动态 Montage 混合参数改 `SegmentBlendTime`；启动 `WaitBackswingTask`
- [x] T2.3 新回调 `OnBackswingStarted()`：置位 + 预输入立即剪断（`Index < 5` 守卫）
- [x] T2.4 `OnAttackPressed()`：插入后摇即时剪断分支（窗口分支之后）
- [x] T2.5 `OnMontageCompleted()`：开头重置 `bInBackswing`；预输入兜底路径保留
- [x] T2.6 `StartNextSegment()`：EndTask 前置抑制标志；两 Montage 回调加早退守卫
- [x] T2.7 清理路径（`StartNextSegment` / `EndAbility`）补 `WaitBackswingTask` 的 EndTask
- [x] T2.8 防御性收武器（实施时新增）：`StartNextSegment` 剪断路径与 `EndAbility` 取消链显式 `DisableWeaponTrace()`——剪断/闪避打断会跳过未触发的 Disable 通知，武器会残留热着（后者为既有隐患，一并修复）；见边界 #12

**验收（日志实测）**：a) 攻击有效帧连点 → 后摇起点即剪、直接出下一段；b) 后摇起点之后按 → 立即剪；c) 第 5 段不剪、正常播完（**当时标准；T3.1b 成环后已改为剪断环回第 1 段**）；d) 漏配通知的段整段播完 + 窗口推进不回归；e) 剪断路径无多余 Montage 回调日志（避坑 #2）。

### 阶段 3 · 闪避后摇打断记忆 N+1（R3 / R4）

- [x] T3.1 `EndAbility()`：记忆段号计算加后摇偏移（N+1；第 5 段环回记 1——成环变更）；`SaveComboMemory` 日志文案更新（`bInBackswing` 重置在阶段 2 已落地）
- [x] T3.1b 连段成环（2026-09-12 需求变更）：移除 `OnBackswingStarted`/`OnAttackPressed` 两处 `< 5` 守卫；`StartNextSegment` 第 5 段循环回第 1 段（窗口推进路径同样 5→1）
- [x] T3.2 回归测试：有效帧闪避 → 记 N 不变（2026-09-12 用户实测通过）
- [x] T3.3 新行为测试：后摇帧闪避 → 记 N+1，闪避后按攻击直接出 N+1 段；第 5 段后摇剪断/闪避 → 回第 1 段（2026-09-12 用户实测通过）

**验收**：三种打断时点的记忆段号与再攻击起手段全部符合预期（沿用现有"GE= / Tag即时校验="日志判读）。

### 阶段 4 · 编辑器资产配置 + 手感打磨

- [x] T4.1 5 段动画已全部摆放 `Combo Backswing`（阶段 1 日志证实 5/5 触发）；遗留检查项：对"后摇区间内还有武器检测窗口"的段落做取舍（保留摆法=剪断放弃该窗口 / 后移通知起点=保留窗口但取消变晚）
- [x] T4.2 手感调参：调参旋钮与实测方法见第 8 节调参指南
- [x] T4.3（2026-09-12 用户决策：**暂不加**——无消费者不加死代码，通知类与链路扩展点已留）
- [x] T4.4（2026-09-12 用户决策：**改**）窗口/宽容期闪避打断也记 N+1，"视作完成"判定扩为"后摇 或 窗口/宽容期 Tag"

**验收**：全 5 段连招（含连点/后摇点按混测）+ 闪避混合连招全链路手感确认。

---

## 8. 调参指南（阶段 4 · 2026-09-12 补）

| 旋钮 | 位置 | 默认 | 调什么 |
|---|---|---|---|
| `SegmentBlendTime` | GA 蓝图子类 → Combo | 0.1 | 段间/剪断混合时长。调大更柔但更拖沓；调 0 = 硬切（原始手感） |
| Combo Backswing 区间起点 | 各攻击 AnimSequence 通知轨道 | 手摆 | 该段**最早可取消时刻**。前移 = 更跟手但剪掉更多动画；若区间里还有武器检测窗口，前移 = 放弃该窗口的伤害（T2.8 防御性收武器保证安全） |
| `WindowDuration` | ComboConfig 每段 | 0.8 | 动画自然播完后的输入窗口（漏配通知的兜底推进路径 + T4.4 记忆判定相关） |
| `GraceDuration` | ComboConfig（第 5 段） | 0.15 | 第 5 段播完后的宽容期 |
| `ComboMemoryWindow` | GA 蓝图子类 → Combo\|ComboMemory | 2.0 | 闪避后连段记忆保留时长，过期回第 1 段 |
| `MaxComboDuration` | GA 蓝图子类 → Combo | 3.0 | 停止按键后连段总超时（**成环后是唯一防无限连的保险**） |

实测方法（PIE + 日志）："第 N 段攻击动画开始/结束"两条日志的时长差 = 实际观感时长（剪断生效后应≈动画全长 − 后摇时长）；剪断日志与 `[ComboBackswing]` 日志的时间差 = 剪断响应延迟（应≈1 帧）；"记忆第 N 段"到"接回第 N 段"的时间差 = 闪避后接回耗时。

---

## 9. 一句话总结

> **后摇区间用 `AnimNotifyState_ComboBackswing` 标在动画资产上（跟随时间轴、变速免疫），NotifyBegin 发 `Event.Combo.BackswingStart` 给 GA；GA 收到事件时若有预输入就 Stop 短混合剪断接下一段（后摇中按下同理即时剪），并把"后摇中被打断"翻译成连段记忆 N+1；连段成环，第 5 段后摇同样剪断/推进循环回第 1 段——动画资产漏配通知自动退化为现状，渐进迁移零风险。**

---

## 10. 已知未覆盖 / 遗留记录（2026-09-12 审计补记）

1. **窗口 GE 推进后残留（既有行为，非本功能引入）→ 已修（2026-09-15）**：原行为——窗口 Tag 到期前每次按攻击键都命中 `bHasWindow` 分支直接推进，连点=连段加速。修正：`OpenWindowAndGrace` 记录窗口/宽容期 GE 句柄（`WindowEffectHandle`/`GraceEffectHandle`），`StartNextSegment` 推进时与 `EndAbility` 结束时显式 `RemoveActiveGameplayEffect`（**推进即关窗**，幂等）。副作用为设计意图：窗口期连点不再连续跳段，每次接段需走预输入/后摇剪断/新窗口的正常路径，节奏更受控；若嫌手感变肉可上调 `WindowDuration`。
2. **T4.4 的 PIE 用例待补测**（代码已落地）：完整播完动画 → 窗口期闪避 → 应记 N+1 且日志显示"窗口期"字样。
3. **T4.3 状态 Tag 与 NotifyEnd 事件**：按用户决策预留扩展点、未实现（无消费者不加死代码）。
4. **敌人侧**：不发输入事件，成环对敌人行为无影响；将来 AI 发 `Event.Combo.AttackPressed` 即自动获得剪断/成环能力。
5. **GE_Dodge_Cooldown 编辑器配置**：闪避系统遗留项（与后摇取消无关），仍待配置。
6. **窗口过期后输入死区 + ResetPending 死代码（2026-09-16 确认）→ 已由第 11 节修复**。
7. **`Input.Buffer.Combo` 是从未被赋予的死 Tag（2026-09-20 考古定论）**：全工程不存在任何 `AddLooseGameplayTag(Input_Buffer_Combo)`——设计文档（GAS_Combo_Design.md §3.1）原规划 PlayerController 按键时加 Tag 记缓冲、GA 推进时 Remove 消费；实际实现中缓冲职责由 GA 内部 `bHasPendingInput` bool 承担（OnAttackPressed 死区分支置位、窗口/后摇/重启三分支各自消费），Tag 形式的缓冲从未落地。`StartNextSegment` 的"消费缓冲"与 `EndAbility` 的清理两处 Remove 均为对计数 0 的幂等 no-op（无 GE 授予同名 Tag，不触发计数减穿，无害但误导读者——"Remove 的存在暗示有个 Add"）。处置待用户拍板：最小改动 = 删两处 Remove、保留 Tag 定义（将来蓝图/AI 侧可见的输入缓冲指示可用 `HasMatchingGameplayTag` 查询）。
8. **`OnMontageCompleted` 预输入检测 = 兜底通道，定论保留（2026-09-20 问答）**：通知齐全的段上不可达（预输入必然在 OnBackswingStarted/OnAttackPressed 更早被同步消费），但它是"漏配 ComboBackswing 通知的动画"的唯一推进路径——方案边界 #7"漏配通知自动退化（整段播完推进）"承诺的执行者，删掉则该类动画的预输入整个失效；也是剪断调度后换段一帧缝隙按键的最终归宿（该帧输入被 PlayCurrentSegment 重置吞没，无害边界）。日志自检：走此路的段无 `[ComboBackswing]` 日志、"动画结束"后紧接下一段且中间无"窗口开启"日志（`return` 跳过开窗）。性质 = 配置完好时恰好休眠的保险丝（同 MaxComboTimer），区别于"配置再对也不可达"的 ResetPending 死分支（后者已修，见 #6/§11）。
9. **重启期被闪避打断记 N+1 = 刻意设计（2026-09-20 复审定论，B 方案，用户拍板）**：复审议题——重启期三条出路中"按攻击=重启第 1 段"（§11.3 #1）、"自然到期=第 1 段"（`OnComboExpired→EndCombo` bWasCancelled=false 不存记忆），唯独"按闪避=记 N+1"，存在"防御键比攻击键更能推进连段"的激励倒挂；对称原则方案（打断记忆=同阶段按攻击的结果，重启期闪避改记第 1 段）被否决。**维持 N+1，设计哲学=「闪避永不亏连段」**：后摇剪断/移动释放/闪避记忆整条功能族的存在意义就是"闪避不惩罚玩家"，重启期闪避记 N+1 是该哲学的最大化表达；"重启期闪避取消→接 N+1"可视为 Dodge Offset 式高阶技巧，倒挂被明确接受为取舍（当前定论，手感实测若出问题可复审）。史实澄清：T4.4（2026-09-12）"窗口/宽容期也视作完成"的比较对象只有"记 N 重播"，且当时 ResetPending 尚为死代码——宽容期记 N+1 的真正依据是本条的闪避哲学，T4.4 的"完成度"论证只覆盖后摇/窗口期。第 5 段重启期打断环回记 1，与重启语义天然同轨，无分歧。

### 11.5 Plan B 落地（2026-09-17）：SetDuration 资产顶替的根治（**用户实测通过**）

**验收记录**：四条用例全过——重启期上限测试（Grace=2.0 的段在重启期 1.5s 处按键成功重启，改前必挂）、短尾巴反向用例（Grace=0.5 超期按键正确失效，每段差异化生效）、静默停手恰在 Window+Grace 秒结束、窗口推进/重启期闪避记忆 N+1/剪断成环回归无损。数值主权正式交接：**窗口/重启时长唯一真值 = ComboConfig 每段 WindowDuration/GraceDuration**。

**根因**（用户翻引擎源码定案）：`Spec.Data->SetDuration()` 只改 spec 期望值并注册时长属性捕获，应用路径在"蓝图 GE 资产 + Duration Magnitude 已配置"前提下从 Def 重新取值——运行时覆盖被资产顶替，GE 实际寿命恒等于资产 Magnitude。影响面：所有依赖 SetDuration 做差异化时长的 GE（各段 WindowDuration / 重启尾巴 / 无敌帧 / 记忆窗口）。

**改造**：窗口期与重启期彻底退役 GE（`ComboWindowGE`/`ComboResetPendingGE` 属性删除、资产不再被应用）：

- **开窗**（`OpenWindowAndGrace`）：挂 `Status.Combo.Window` Loose Tag + 两个世界定时器（`WindowTimerHandle`=WindowDuration → `OnWindowExpired`；`ComboExpiryTimerHandle`=Window+Grace → `OnComboExpired`）；
- **窗口到期**（`OnWindowExpired`，原 OnRestartPeriodStarted 升级）：摘窗口 Tag → 挂 `Status.Combo.ResetPending` Loose Tag（Grace>0 时）+ 检查点日志；
- **推进即关窗**（`StartNextSegment`）：摘两 Tag + 清两定时器（原 GE 句柄移除退役）；`PlayCurrentSegment` 漏斗兜底成对清理；
- **连段结束**（`EndAbility`）：既有 Loose 清理升为主路径 + 清定时器。
- 消费方零改动：`HasMatchingGameplayTag` 对 Loose Tag 与 GE Tag 查询语义完全一致（OnAttackPressed 三分支、T4.4 判定、闪避记忆全不动）。

**时长真值**：窗口期=ComboConfig 每段 `WindowDuration`、重启期=每段 `GraceDuration`，世界定时器直读——GE 资产 Magnitude 不再参与。**仍由资产决定的遗留**：记忆 GE（建议资产 Magnitude 对齐代码值 2.0）、无敌 GE（核对资产值≈0.3）；ComboAttackingGE 无限时长不受影响。联机注意：Loose Tag 不复制，将来做多人才需回 GE 方案（届时资产时长按段配置多资产或改 ScalarEntry）。

**MaxComboDuration 权责收缩（2026-09-17 方法2）**：`OpenWindowAndGrace` 开窗时 `ClearTimer(MaxComboTimerHandle)`——动画一旦播完，单段生命周期全权归 ComboExpiryTimer（每段必到期必 EndCombo）。MaxCombo 只剩一个职责：**动画播放期间的超长兜底**（ComboExpiry 只在动画结束后才挂）。修正的交互缺陷：原实现 MaxCombo 从动画起算，会成为重启期隐形上限（Grace=5 时连段在重启期中途被静默截断，用户实测发现）；现在超长 Grace 全额生效。

---

## 11. 宽容期改造 + 输入死区修复（2026-09-16 方案，C 选项；**同日代码落地，待用户实测**）

> 源头：§10 遗留 ①（窗口过期后输入死区）+ ③（ResetPending 死代码）。两个问题一个方案解决。

### 11.1 目标语义

```
动画播完/移动释放 (t=0)
  ├─ 窗口 GE 挂上（Duration = WindowDuration，默认 0.8s）——"接续期"：按键 → N+1/环回
  ├─ 宽容期 GE 同时挂上（Duration = WindowDuration + GraceDuration）——它的"尾巴"落在窗口过期后
  │
  t=0 ~ Window：窗口期。按键 → 窗口分支推进（现状不变）
  t=Window ~ Window+Grace：重启期（窗口过期、宽容期尾巴）。按键 → 复活原 ResetPending 分支：
  │       清段数 Tag → 挂 Count.1 → PlayCurrentSegment 重启第 1 段（原来这里是死区：存预输入干等超时）
  t=Window+Grace：段结束定时器到点 → OnComboExpired → EndCombo（GA 结束，不存记忆）
  │       之后按键 → GA 已结束 → PlayerController 走 TryActivateAbilitiesByTag 重新起手第 1 段
```

**机制巧思**：宽容期 GE 改为与窗口 GE 同时挂、时长 = Window + Grace——存活期覆盖 [0, Window+Grace]，其中 [Window, Window+Grace] 恰好是窗口过期后的重启期。`OnAttackPressed` 分支顺序（窗口 → 后摇 → ResetPending）天然屏蔽前段，**现有 ResetPending 分支的逻辑一行不改即复活**，且从"仅第 5 段"自动扩展到所有段（重启逻辑本来就是清全部段数 Tag 挂 Count.1，通用）。

**不用 TagRemoved 监听**（候选重构②的坑直接绕开）：感知"重启期也过了"用**段结束定时器**（世界 TimerManager），挂/清全在自己代码里，与"推进即关窗"的显式 GE 移除零交互。

### 11.2 具体修改清单（`GA_NormalAttack.h/.cpp`）

| # | 位置 | 改动 |
|---|---|---|
| 1 | h | 新增 `FTimerHandle ComboExpiryTimerHandle;` + `void OnComboExpired();` 声明 |
| 2 | `OpenWindowAndGrace()` | 宽容期 GE 块：**去掉 `CurrentIndex >= 5` 条件**（全段挂载）；`SetDuration(WindowDuration + GraceDuration)`（原固定 GraceDuration）；末尾 `SetTimer(ComboExpiryTimerHandle, OnComboExpired, WindowDuration + GraceDuration, false)` |
| 3 | `PlayCurrentSegment()` | MaxComboTimer 重置旁补 `ClearTimer(ComboExpiryTimerHandle)`——所有推进/重启路径都经过此函数，旧段周期统一作废 |
| 4 | `EndAbility()` | 清理计时器处补 `ClearTimer(ComboExpiryTimerHandle)`（防打断后残留触发） |
| 5 | `OnAttackPressed()` ResetPending 分支 | 逻辑不动；分支前补**显式移除 Window/Grace GE 句柄**（推进即关窗同款——重启后旧宽容期 GE 不清会泄漏 Tag、句柄被下一段覆盖成孤儿）；注释更新为"重启期=窗口过期后的重启机会（全段）" |
| 6 | `OnComboExpired()` | 新函数：日志（"段结束期已过，连段超时结束"）+ `EndCombo()`（bWasCancelled=false 不存记忆——玩家放弃连段的语义） |

### 11.3 行为变化与边界

| # | 场景 | 变化 |
|---|---|---|
| 1 | 窗口过期后按键（原死区） | **立刻重启第 1 段**（原：存预输入干等到 MaxComboDuration 超时）——本方案核心收益 |
| 2 | 玩家彻底停手 | GA 结束时刻从"距上段开始 3s"精确到"段结束起 Window+Grace（约 0.95s）"——**MaxComboDuration 职责被接管**，保留仅作动画超长的兜底（可择期退役，本次不动） |
| 3 | 移动释放后跑着不接段 | 同样 0.95s 后连段结束（原 3s）——手感收紧为设计意图：交付后不接就重开；嫌紧调 WindowDuration |
| 4 | 重启期被闪避打断 | ResetPending Tag 在场 → 记 N+1（2026-09-20 复审维持：「闪避永不亏连段」哲学，倒挂接受为取舍，见 §10 #9；非 T4.4 完成度论证） |
| 5 | 推进/剪断路径 | `PlayCurrentSegment` 统一清定时器 + 既有句柄移除——推进即关窗模式自动兼容，无新交互 |
| 6 | 第 5 段 | 不再特判：窗口期按键环回 1（现状）、重启期按键重启 1（新）——语义统一 |
| 7 | `GraceDuration` 配置语义 | 从"第 5 段播完后的宽容期"变为"**所有段**窗口过期后的重启期长度"（叠加在 Window 之上）；默认 0.15s 偏短，建议 ComboConfig 里调 0.2~0.3s |
| 8 | 同帧竞态（宽容期过期瞬间按键） | 定时器与输入顺序不定：先 EndCombo → 按键重新激活起手 1（正确）；先按键 → 兜底分支存预输入 → 随即 EndCombo 清掉（无害） |

### 11.4 验收用例（PIE + 日志）

1. 第 2 段播完 → 窗口期(0.8s)不按 → 再忍住 0.15s（重启期）→ 按攻击 → **直接重启第 1 段**（日志无"存预输入"死等，MaxComboTimer 未触发即已重启）
2. 播完 → 窗口期内按 → 正常 N+1（回归）
3. 彻底停手 → 约 0.95s 后"连段超时结束"日志 + `Status.Combo.Attacking` 消失 → 再按攻击重新起手 1
4. 移动释放后跑 1 秒不接 → 连段结束（手感确认点）
5. 重启期闪避 → 记忆 N+1（ResetPending Tag 参与判定）
6. 全程连打回归：剪断/成环/推进即关窗不受影响
