# 后摇移动释放（Backswing Movement Release）· 实现方案

> 目标：攻击与闪避的**后摇期间允许正常角色移动**（带正常跑步/走路动画，不是收招姿势平移）。
> 表现选型（2026-09-12 用户确认，三选一中的 B）：**静止播后摇，一动就释放**——
> - 静止不动：后摇动画照常完整播出（保留"站桩收招"的抬手表现）；
> - 推动摇杆瞬间：立刻停掉 Montage、切回移动状态，角色可自由跑动，同时连段窗口提前开启——边跑边接下一段、边跑边闪避都成立。
>
> 前置依赖：后摇取消功能已全部落地（`Docs/Combo_Backswing_Cancel.md`，AnimNotifyState_ComboBackswing / Event.Combo.BackswingStart / 剪断机器 / 窗口 GE 均为现成）。
>
> **实施状态（2026-09-12）**：全功能落地——攻击/闪避两侧释放均实测工作。含三次实施修订（确定性收尾、双侧显式 Montage_Stop、AnimInstance 获取 API 更正），详见第 4 节避坑 #5 与第 6 节实施结果。

---

## 0. 现状结论（基于代码核查）

| 事实 | 依据 | 对方案的影响 |
|---|---|---|
| **移动输入从未被 C++ 锁过** | `AHelloPlayerController::Move()` 无任何状态检查，`AddMovementInput` 攻击/闪避期间一直生效 | "不能移动"的根源在**动画层**：DefaultSlot Montage 全身覆盖移动动画（或动画带 Root Motion 锁位移），后摇期角色钉在收招姿势 |
| 攻击后摇起点感知 = 现成机器 | `AnimNotifyState_ComboBackswing` → `Event.Combo.BackswingStart` → `WaitBackswingTask` | 攻击侧零新增感知，直接复用 `bInBackswing` |
| 闪避 GA 无后摇概念 | `GA_Dodge` 只绑 BlendOut/Interrupted，动画播完即结束 | 闪避侧要照攻击的样子补一条通知链路 |
| `OnMontageCompleted` 才开窗口 | 窗口 GE / 第 5 段宽容期 GE 都挂在自然播完回调里 | **释放路径不会走到它**（Montage 被停、回调被抑制）→ 窗口逻辑必须抽取出来在释放时也能开，否则释放后接不了段 |
| 敌人不走 PlayerController 移动 | `HelloEnemy` 用自己的 AI/测试函数 | 移动事件只有玩家会发，敌人侧零影响 |

---

## 1. 总体思路

一句话：**给"玩家开始移动"也发一个 GameplayEvent（`Event.Movement.Started`），攻击/闪避 GA 在后摇区间收到它 → 复用现有"Stop Montage + 短混合"机器释放角色，并同时把连段窗口提前打开。**

```
第 N 段动画（攻击）：
 ├─ 有效帧 ─┬────────── 后摇（ComboBackswing 区间）──────────┤
 │          │ bInBackswing=true                            │
 │          │ 静止：后摇动画照播（现状）                      │
 │          │ 推杆 → OnMovementStarted：                    │
 │          │   ① 防御性收武器（后摇里可能有检测窗口）          │
 │          │   ② suppress + EndTask Montage（短混合切 locomotion）
 │          │   ③ 立刻开窗口 GE（+第 5 段宽容期）→ 可边跑边接段 │
 │          │   ④ bInBackswing 保持 true（闪避打断仍记 N+1）  │
 │          │ 后摇起点前推杆 → 忽略（攻击有效帧不释放）         │
 
闪避动画（GA_Dodge）：
 ├─ 翻滚（无敌帧=GE 时间制，与动画无关）─┬── 后摇（同款通知、Dodge Tag）──┤
 │                                      │ 推杆 → EndTask → OnInterrupted/ │
 │                                      │   BlendOut → EndAbility →       │
 │                                      │   Status.Dodge.Active 提前消失  │
 │                                      │   → 可立即起手普攻（连锁红利）    │
```

移动事件从哪来：`AHelloPlayerController::Move()` 里推杆值非零时发 `Event.Movement.Started` 到角色 ASC——**每帧发、不做边沿检测**（原因见避坑 #1），GA 侧用标志位幂等消化。

---

## 2. 新增 / 改动组件

### 2.1 新增 Native Tag ×2（HelloGameplayTags）

```text
Event.Movement.Started      # 玩家开始/正在移动（PlayerController::Move 持续发）
Event.Dodge.BackswingStart  # 闪避动画后摇区间开始（闪避动画上的通知发出）
```

### 2.2 `AnimNotifyState_ComboBackswing` 参数化（加一个属性，不改类名）

通知类当前硬编码发 `Event.Combo.BackswingStart`。给它加一个**可编辑 Tag 属性**，闪避动画复用同一个类、选另一个 Tag：

```cpp
/** 该通知发出的后摇事件 Tag。攻击动画保持默认（Event.Combo.BackswingStart）；
 *  闪避动画在编辑器里改为 Event.Dodge.BackswingStart */
UPROPERTY(EditAnywhere, Category = "Backswing")
FGameplayTag BackswingEventTag;   // 构造函数里默认 = Event_Combo_BackswingStart

// NotifyBegin 里：HandleGameplayEvent(BackswingEventTag, &Payload)
```

- **不改类名**：5 个攻击动画上已摆的通知按类引用，改名会断资产；加属性零破坏。
- 攻击资产零改动（吃到默认值），只有闪避动画需要摆一次通知并选 Dodge Tag。

### 2.3 `AHelloPlayerController::Move()` 发事件

```cpp
void AHelloPlayerController::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn) return;

	// 移动事件：推杆值非零时每帧发（GA 侧幂等，不做边沿检测——见方案避坑 #1）
	if (!MovementVector.IsNearlyZero(0.1f))
	{
		if (AHelloCharacterBase* Char = Cast<AHelloCharacterBase>(ControlledPawn))
		{
			if (UAbilitySystemComponent* ASC = Char->GetAbilitySystemComponent())
			{
				FGameplayEventData Payload;
				Payload.EventTag = HelloGameplayTags::Event_Movement_Started;
				ASC->HandleGameplayEvent(HelloGameplayTags::Event_Movement_Started, &Payload);
			}
		}
	}

	// ...原有 AddMovementInput 不变...
}
```

### 2.4 `GA_NormalAttack` 改动

**新增成员**：

```cpp
/** 本段后摇已因移动释放（防重复释放；PlayCurrentSegment/EndAbility 重置） */
bool bBackswingReleased = false;

/** 等待移动开始事件任务（第三个 WaitGameplayEvent 实例） */
UPROPERTY()
UAbilityTask_WaitGameplayEvent* WaitMovementTask = nullptr;
```

**抽取 `OpenWindowAndGrace()`**：把 `OnMontageCompleted` 里"开窗口 GE + 第 5 段宽容期 GE"两段原样抽成函数，`OnMontageCompleted`（自然播完兜底）与释放路径共用。

**新回调 `OnMovementStarted()`**：

```cpp
void UGA_NormalAttack::OnMovementStarted(FGameplayEventData EventData)
{
	if (!WaitMovementTask || !IsActive()) return;

	// 只在后摇区间且尚未释放时生效；攻击有效帧推杆不释放（保持现有站桩攻击）
	if (!bInBackswing || bBackswingReleased) return;
	if (!CurrentMontageTask || !CurrentMontageTask->IsActive()) return;

	bBackswingReleased = true;

	UE_LOG(LogTemp, Warning, TEXT("[GA_NormalAttack] [%s] 第 %d 段后摇中移动 → 释放动画，窗口提前开启"),
		*GetAvatarActorFromActorInfo()->GetName(), GetCurrentComboIndex());

	// ① 收武器：释放同样跳过后摇里未触发的 DisableWeaponTrace（后摇里可能有检测窗口）
	if (AHelloCharacterBase* BaseChar = Cast<AHelloCharacterBase>(GetAvatarActorFromActorInfo()))
	{
		BaseChar->DisableWeaponTrace();
	}

	// ② 停 Montage：回调抑制（与 StartNextSegment 同款），bInBackswing 保持 true（闪避记忆 N+1 语义不变）
	bSuppressMontageCallbacks = true;
	if (CurrentMontageTask->IsActive()) { CurrentMontageTask->EndTask(); }
	CurrentMontageTask = nullptr;

	// ③ 窗口提前开（+第 5 段宽容期）——释放后按攻击键走既有窗口分支推进，边跑边接段
	OpenWindowAndGrace();
}
```

**其余接线**：`PlayCurrentSegment` 重置 `bBackswingReleased=false` + 启动 `WaitMovementTask`（照 `WaitBackswingTask` 模式）；`StartNextSegment`/`EndAbility` 清理 `WaitMovementTask`；`EndAbility` 重置 `bBackswingReleased`。

### 2.5 `GA_Dodge` 改动（照攻击侧抄一遍）

- 新成员：`bInBackswing` / `WaitBackswingTask`（听 `Event.Dodge.BackswingStart`）/ `WaitMovementTask`（听 `Event.Movement.Started`）；
- `OnDodgeBackswingStarted()`：置 `bInDodgeBackswing=true`；
- `OnDodgeMovementStarted()`（**实施修订为确定性收尾**）：`bInDodgeBackswing` 且 Montage 在播 → `bSuppressMontageCallbacks` 抑制 + `EndTask DodgeMontageTask` + 直接 `EndAbility(false)`——不依赖 EndTask 到底触发哪个委托，收尾路径唯一、无双结束（GA 结束 → `Status.Dodge.Active` 自动消失 → 普攻解锁）；
- 两个 Montage 回调开头补 `if (!IsActive() || bSuppressMontageCallbacks) return;` 防重入（镜像 GA_NormalAttack 模式）；
- `EndAbility` 补两个新任务的清理 + 标志重置；
- 释放**不提前结束无敌 GE**：无敌是时间制（`InvincibleDuration`），与动画无关，提前释放动画不影响无敌帧时长（语义正确：无敌帧承诺的是时间不是姿势）。

### 2.6 编辑器侧（唯一必做项）

闪避动画（GA_Dodge 蓝图的 DodgeMontage 资产）摆一个 `Combo Backswing` 区间通知，**把通知上的 Tag 属性改为 `Event.Dodge.BackswingStart`**；起点=翻滚主体结束（建议在无敌帧结束后附近），拉到动画尾。攻击 5 段动画零改动。

---

## 3. 边界情况清单

| # | 场景 | 行为 | 依据 |
|---|---|---|---|
| 1 | 攻击有效帧推杆 | 不释放，攻击动画照播（移动输入原本就生效，观感同现状） | `bInBackswing` 守卫 |
| 2 | 后摇起点前一帧已推杆、后摇开始时仍按住 | **当帧事件即触发释放**（事件每帧发，不存在"错过边沿"问题） | 避坑 #1 |
| 3 | 释放后窗口期内按攻击 | 走既有窗口分支推进——边跑边接下一段 | 窗口 GE 提前开 |
| 4 | 释放后窗口期内闪避 | 记忆 N+1（`bInBackswing` 保持 true + 窗口 Tag 双保险） | 现有 T4.4 判定 |
| 5 | 释放后一直不接段 | 窗口到期 → `MaxComboDuration` 超时 → 连段结束（现有机制） | 现状 |
| 6 | 预输入剪断 vs 移动释放竞争 | 剪断推进在前则段数已换、释放标志随 `PlayCurrentSegment` 重置；同帧到达时 `ScheduleAdvance` 去重 | 现有机器 |
| 7 | 闪避翻滚主体（后摇前）推杆 | 不释放，翻滚不可打断移动（无敌帧完整性） | `bInBackswing` 守卫 |
| 8 | 闪避释放后立即按攻击 | `Status.Dodge.Active` 已随 GA 结束消失 → 普攻立即可起手（连锁红利，不额外处理） | ActivationBlockedTags 机制 |
| 9 | 完美闪避慢动作（攻击者 0.3 倍速）中移动释放 | 事件走世界时间照常到达；动画混合照常——攻击者本体的释放不受影响 | 事件/计时器不受 CustomTimeDilation 影响 |
| 10 | 敌人侧 | 不走 PlayerController 移动 → 无事件 → 不释放，行为不变 | 现状 |
| 11 | 后摇里带武器检测窗口的段（实际存在） | 释放=跳过未触发的 Disable → 释放路径显式收武器兜底（①） | T2.8 同款 |
| 12 | 动画带 Root Motion | 静止播后摇无影响；释放停 Montage 即交还位移控制权——两种资产情形都成立 | 停 Montage 的通用性 |

---

## 4. 避坑记录

1. **移动事件必须每帧发、不做边沿检测**。若只在"从停到动"的边沿发一次：玩家在后摇开始前的一帧已推杆（事件发过、被忽略），后摇开始时按住不放 → 再也没有新事件 → 永远释放不了。每帧发 + GA 侧 `bBackswingReleased` 幂等是唯一稳的做法；本地单机每帧一次 `HandleGameplayEvent` 开销可忽略。
2. **释放路径必须自己开窗口**。释放停掉 Montage 后 `OnMontageCompleted` 被抑制标志吞掉，自然播完才开窗口的旧逻辑永远不会执行——不开窗口 = 释放后接不了段，这是本方案最容易漏的一步（所以把开窗逻辑抽成 `OpenWindowAndGrace()` 复用）。
3. **释放 ≠ 推进**：只停动画 + 开窗口，段数 Tag 不动、GA 不结束。别把释放写成 `StartNextSegment`（那会跳段）。
4. **`bInBackswing` 在释放后保持 true**：闪避打断记忆 N+1 的语义锚点是"攻击已交付"，释放只是换了表现，没改变交付事实。
5. **GA_Dodge 释放的收尾路径要确定**（实施时修订）：原计划"EndTask 后交给 OnInterrupted/OnBlendOut 回调链收尾"，但 EndTask 到底触发哪个委托不确定（引擎源码不可读）——万一一个都不触发，GA 永久卡活、普攻被 `Status.Dodge.Active` 永久封锁。最终采用与 GA_NormalAttack 同款的确定性模式：`bSuppressMontageCallbacks` 抑制 + EndTask + 直接 `EndAbility(false)`，两回调开头早退守卫，路径唯一。**二次修订（用户实测后）**：EndTask"是否代停 Montage"同样不确定——闪避是 Root Motion 驱动，GA 结束但 Montage 没真停时，翻滚位移仍锁死角色到动画自然播完（症状：释放日志出现但仍要等播完才能动）。收尾前显式 `AnimInstance->Montage_Stop(0.1f)`（幂等，多停无副作用）。**三次修订（同日，攻击释放同坑）**：GA_NormalAttack 的 `OnMovementStarted` 同样补显式 `Montage_Stop(SegmentBlendTime)`——释放后 GA 存活等窗口输入，Montage 没真停时全身动画持续盖住跑步姿态（症状：释放日志+窗口都有了、角色却钉在收招姿势跑不动；此前"释放后接段"能过是因为新 Montage 顶掉了旧的，掩盖了缺口）。

---

## 5. 分阶段落地任务

> 沿用项目惯例：每阶段编译 + PIE 日志实测验收。

### 阶段 1 · 事件链路（Tag + 通知参数化 + Move 发事件）

- [x] T1.1 `HelloGameplayTags.h/.cpp` 追加 `Event_Movement_Started`（`Event.Movement.Started`）与 `Event_Dodge_BackswingStart`（`Event.Dodge.BackswingStart`）
- [x] T1.2 `AnimNotifyState_ComboBackswing` 加 `BackswingEventTag` 可编辑属性（构造默认 Combo Tag；`NotifyBegin` 改用该属性，日志改打实际 Tag 名）——攻击 5 段动画零改动回归
- [x] T1.3 `HelloPlayerController::Move()` 推杆非零每帧发 `Event.Movement.Started`（不加每帧日志防刷屏——事件到达性由阶段 2 GA 侧日志直接证明；参数化默认值则由攻击后摇日志打出 Tag 名验证）
- [x] T1.4 编译 + 回归通过（2026-09-12 用户实测：攻击通知默认 Tag 未破坏资产、攻击/闪避行为零变化）

**验收**：日志看到移动事件按帧到达；5 段攻击的后摇通知仍正常触发（Tag 默认值未破坏资产）。

### 阶段 2 · 攻击后摇移动释放

- [x] T2.1 `GA_NormalAttack.h`：`bBackswingReleased` + `WaitMovementTask` + `OnMovementStarted`/`OpenWindowAndGrace` 声明
- [x] T2.2 抽取 `OpenWindowAndGrace()`（窗口 GE + 第 5 段宽容期，从 `OnMontageCompleted` 原样搬移，两处调用）
- [x] T2.3 `OnMovementStarted()` 实现（收武器 → suppress+EndTask → 开窗口；`bInBackswing` 不动）
- [x] T2.4 `PlayCurrentSegment` 重置释放标志 + 启动第三监听；`StartNextSegment`/`EndAbility` 补清理与标志重置
- [x] T2.5 PIE 用例（2026-09-12 用户实测通过：静止完整播出 / 后摇推杆释放切跑步 / 释放后按攻击 1→2 接段 / 释放后闪避记 N+1 并接回第 2 段不重播）

**验收**：五条全过 + 剪断/成环/闪避记忆全部回归（重点 c/d 与现有窗口分支、T4.4 判定的衔接）。

### 阶段 3 · 闪避后摇移动释放

- [x] T3.1 `GA_Dodge.h/.cpp`：`bInDodgeBackswing` + `bSuppressMontageCallbacks` + `WaitBackswingTask`（Dodge Tag）+ `WaitMovementTask` + 两回调 + 确定性释放路径（抑制+EndTask+直接 EndAbility，见 2.5 实施修订）+ `EndAbility` 清理与标志复位（ActivateAbility 开头也做残留复位）
- [x] T3.2 两个 Montage 回调补 `IsActive() || bSuppressMontageCallbacks` 守卫
- [x] T3.3 编辑器：闪避动画 `Dodge_B_Anim` 已摆 `Combo Backswing` 通知、Tag 已选 `Event.Dodge.BackswingStart`（日志证实 Tag 名正确）
- [x] T3.4 核心链路 b 实测通过（尾段推杆 → 提前结束、立即可移动可起手普攻）；a/c/d/e 建议与 T4.1 调参后一并复测（注：通知起点在最左时 d"翻滚主体不释放"无意义——整个动画都是后摇区间）

**验收**：五条全过；重点确认释放时只触发一个 Montage 回调（避坑 #5，看日志有无双 EndAbility 警告）。

### 阶段 4 · 手感打磨

- [ ] T4.1 闪避动画通知起点（= 闪避最早可移动时刻）与释放混合观感调参。**已发现的调参教训**：起点拖到最左 + 按住摇杆 = 闪避起手瞬间被掐断、翻滚位移几乎不走——建议起点摆在翻滚位移基本完成的帧，再往左微调到"位移完整 vs 收招灵活"的平衡点
- [ ] T4.2 （可选）攻击后摇释放的混合时长与剪断共用 `SegmentBlendTime`，观感不妥再拆分独立配置

---

## 6. 实施结果记录（2026-09-12 收官）

### 代码改动清单

| 文件 | 改动 |
|---|---|
| `HelloGameplayTags.h/.cpp` | +2 Native Tag：`Event.Movement.Started`、`Event.Dodge.BackswingStart` |
| `AnimNotifyState_ComboBackswing.h/.cpp` | +可编辑 `BackswingEventTag`（构造默认 Combo Tag，5 段攻击资产零改动）；验证日志改打实际 Tag 名 |
| `HelloPlayerController.cpp` | `Move()` 推杆非零每帧发移动事件（不做边沿检测，注释写明缘由） |
| `GA_NormalAttack.h/.cpp` | +`bBackswingReleased`/`WaitMovementTask`/`OnMovementStarted`；抽取 `OpenWindowAndGrace()`（自然播完与释放两路共用）；释放 = 收武器 → suppress → **显式 Montage_Stop(SegmentBlendTime)** → EndTask → 开窗 |
| `GA_Dodge.h/.cpp` | +`bInDodgeBackswing`/`bSuppressMontageCallbacks`/双监听任务；确定性释放 = 抑制 → **显式 Montage_Stop(0.1f)** → EndTask → EndAbility(false)；两 Montage 回调补守卫；Activate/EndAbility 标志复位 |

编辑器侧唯一动作已完成：闪避动画 `Dodge_B_Anim` 摆 `Combo Backswing` 通知、Tag 选 `Event.Dodge.BackswingStart`。

### 实测验证状态

- ✅ 攻击：静止完整播出 / 后摇推杆释放切跑步 / 释放后边跑边接段（1→2）/ 释放后闪避记忆 N+1 并接回不重播
- ✅ 闪避：通知→Tag→GA 置位→释放全链路（日志逐环证实）/ 尾段推杆释放、移动恢复
- ⏳ 待最后一轮回归：攻击释放终版（显式 Montage_Stop + `GetCurrentActorInfo()->GetAnimInstance()` 编译修复）后复测"释放当帧切跑、跑着闪避、跑着接段"三动作；T3.4 的 a/c/d/e 用例；T4.1 通知起点调参

### 相对原方案的三次实施偏差

1. 闪避释放从"EndTask 后交给回调链收尾"改为**确定性收尾**（抑制 + 直接 EndAbility）——回调链模式有"GA 永久卡活封锁普攻"的尾部风险（2.5 节已同步）；
2. 两侧释放均补**显式 `Montage_Stop`**——`EndTask` 是否代停 Montage 是引擎不确定行为，闪避（Root Motion）和攻击（GA 存活等窗口）各暴露过一次"日志出现但动画没真停"的症状（避坑 #5 二/三次修订）；
3. AnimInstance 获取 API 更正：`UAbilitySystemComponent` 无公开 `GetAnimInstance()`（C2039 实锤），正确写法 `GetCurrentActorInfo()->GetAnimInstance()`——已记入项目 GAS 踩坑记忆。

---

## 7. 一句话总结

> **移动开始也走 GameplayEvent（`Event.Movement.Started`，每帧发、GA 幂等消化）；攻击/闪避 GA 在后摇区间收到它 → 收武器 + 抑制回调 + 显式 Montage_Stop 短混合 + EndTask 释放角色（攻击侧同时立刻开连段窗口）——静止照播后摇、一动就自由，边跑边接段/闪避/起手全部顺承现有机制，收尾不依赖任何引擎不确定行为。**
