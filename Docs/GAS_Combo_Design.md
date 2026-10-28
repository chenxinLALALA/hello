# GAS 普通攻击连段系统 · 实现方案

> 目标：在现有 Hello 项目中，基于 **UE GAS 插件** 实现普通攻击连段（Combo）。
> 设计前提（已与需求确认）：
> - 推进方式：**窗口内点击推进**（输入窗口内再次按下 → 进入下一段；错过窗口 → 结束/回到第一段）
> - 状态持有：**ASC 上，用 GameplayTag / GameplayEffect 驱动**（不依赖 Character 上的裸 int）
>
> 本文只给出实现思路与接口设计，不修改现有代码。

---

## 0. 现状与约束（基于现有代码梳理）

| 现有结构 | 说明 | 对连段的影响 |
|---|---|---|
| `UHelloASC`（继承 `UAbilitySystemComponent`）| 挂在 `AHelloCharacter` 上，`CreateDefaultSubobject` 创建，`SetIsReplicated(true)` | 连段状态 Tag / GE 天然挂在这里，天然支持网络复制 |
| `AHelloCharacter::InitialStartUpAbilities()` | `BeginPlay` 时把 `CharacterActiveAbilities` 里的 GA 用 `GiveAbility` 授予 | 连段 GA 直接加进这个数组即可，无需改授予逻辑 |
| 输入走 **Enhanced Input**（`HelloPlayerController`）| 有 `MoveAction` / `JumpAction` 等，新增 `AttackAction` 即可 | 连段按键在 `AHelloPlayerController::SetupInputComponent()` 绑定 |
| `GA_SwitchCharacter` | 采用 **GameplayEvent + Tag** 触发模式，`ListenForSwitchEvent` | 项目已有「事件触发 GA」的先例，连段可参考 |
| 尚未看到 `BlockAbilitiesWithTag` / `ActivateAbilitiesWithTag` 配置 | 需补充 | 打断、受击、状态互斥靠这些 Tag 实现 |

> 注意：当前项目是单机/本地测试为主（`HelloCharacter` 的 ASC 直接挂角色，未走 PlayerState 那套权属），
> 网络复制部分按「主机模拟」即可，本文不展开复杂跨端同步。

---

## 1. 总体思路

把「连段」拆成两个正交问题：

1. **状态**（我连到第几段了？）→ 用 GameplayTag 表达，挂在 ASC 上。
2. **输入缓冲（缓存队列）**（我按了，但还没到下一段的窗口，这个输入要不要记？）→ 用「窗口 GE + 缓冲 Tag」表达，挂在 ASC 上。

核心循环：

```
按攻击键
  │
  ├─ 连段 GA 未激活 → 用 Tag 直接激活 GA（Ability.Combo.Attack），GA 读段数=1，播第 1 段
  │
  └─ 连段 GA 已激活
       ├─ 当前在输入窗口内 → 段数 Tag +1（1→2→3），播下一段
       └─ 当前不在输入窗口 → 丢弃（或按第 4 节「宽容窗口」记入缓冲）
```

---

## 2. GameplayTag 设计（全部通过 GameplayTag 表定义）

建议在项目 Tag 里统一加一组：

```text
# 能力标签（用 ActivateAbilitiesWithTag 定位连段 GA）
Ability.Combo.Attack

# 连段状态标签（挂在 ASC 上，表示"正在第 N 段"）
Status.Combo.Count.1
Status.Combo.Count.2
Status.Combo.Count.3

# 通用状态标签（挂在 ASC 上）
Status.Combo.Attacking      # 正在连段中（可用于打断/伤害判定/移动限制）
Status.Combo.Window         # 当前处于"可衔接输入窗口"内
Status.Combo.ResetPending   # 最终段结束后，等待"是否重新开始连段"的宽容期

# 输入缓冲标签（挂在 ASC 上，表示"存了一次攻击按键"）
Input.Buffer.Combo

# 游戏事件标签（走 SendGameplayEventToActor，参考 GA_SwitchCharacter）
Event.Combo.AttackPressed
Event.Combo.ChainCombo
Event.Combo.ResetCombo
```

**为什么段数用 Tag 而不是 int？**
- 伤害倍率、AnimNotify、音效、特效都可以挂到 Tag 上，配数据资产时直观；
- Tag 天然进 ASC 的 `HasMatchingGameplayTag` / `BlockAbilitiesWithTag` 查询体系；
- 未来做「第 N 段附带特殊效果」时，直接按 Tag 查数据表即可。
- 代价：段数上限=你手动列多少 Tag。连段一般 3~5 段，列 Tag 完全够。

> 如果将来段数很多（10+），可以用一个 `SetByCaller` 的浮点属性（如 `ComboIndex`）替代。
> 但按你的选择「Tag/GE 驱动」，先按 Tag 走。

---

## 3. 状态如何流转（段数推进）

### 3.1 输入入口：`AHelloPlayerController`

新增 `UInputAction* AttackAction`，`SetupInputComponent()` 里绑定：

```cpp
void AHelloPlayerController::OnAttack()
{
    UHelloASC* ASC = /* 从 Pawn 上拿 */;

    // 1. 连段 GA 是否已激活？
    if (ASC->IsAbilityActiveWithTag(FGameplayTag::RequestGameplayTag("Ability.Combo.Attack")))
    {
        // 已在连段中：把"这次按键"告诉当前 GA
        ASC->AddLooseGameplayTag(Input.Buffer.Combo);        // 记一次缓冲
        FGameplayEventData Data;
        ASC->HandleGameplayEvent(Event.Combo.AttackPressed, Data); // 通知 GA
    }
    else
    {
        // 未连段：直接激活（GAS 会自己检查是否被 Block）
        ASC->TryActivateAbilitiesByTag(Ability.Combo.Attack);
    }
}
```

> 关键点：**这里不关心段数**。段数由 GA 自己读 Tag 决定。输入层只负责「按了 / 没按」。

### 3.2 连段 GA：`UGA_ComboAttack`（建议单 GA 管理整条连段）

设计成一个 GA 实例管理整条连段（内部状态机），而不是每段一个 GA。好处：
- 连段间数据不落地、不断流；
- 中断清理只要管一个 GA 的 `EndAbility`；
- 与 Tag 状态完全对应，好查错。

```cpp
UCLASS()
class UGA_ComboAttack : public UHelloGA
{
    GENERATED_BODY()
public:
    // 配置
    UPROPERTY(EditDefaultsOnly)
    TArray<UAnimMontage*> ComboMontages;          // 第 1/2/3 段动画

    // 窗口 GE（蓝图配置）：挂 Status.Combo.Window 标签
    UPROPERTY(EditDefaultsOnly)
    TSubclassOf<UGameplayEffect> ComboWindowGE;

    virtual void ActivateAbility(...) override;
    virtual void EndAbility(...) override;

protected:
    // 根据当前段数 Tag 播对应动画
    void PlayCurrentSegment();
    int32 GetCurrentComboIndex() const;            // 扫 Status.Combo.Count.N 得出 1/2/3

    UFUNCTION()
    void OnAttackPressed(FGameplayEventData EventData);  // 监听 Event.Combo.AttackPressed

    void StartNextSegment();                        // 段数+1，播下一段
    void EndCombo();                                // 结束/重置
};
```

#### 激活流程

```
ActivateAbility
  └─ ① 提交（CommitAbility：查 cost / cooldown / Block 标签）
  └─ ② 读段数 Tag → GetCurrentComboIndex()
  └─ ③ PlayCurrentSegment()
        ├─ PlayMontage(第 N 段动画)
        ├─ AddLooseGameplayTag(Status.Combo.Attacking)
        └─ 等待两个信号（详见下节）
```

#### 窗口与推进

动画播到「可衔接点」（用 AnimNotify 或延时）时，GA：

1. **开窗口**：把 `ComboWindowGE` 应用到自己（ASC）上
   - 该 GE 在 ASC 上 **添加 `Status.Combo.Window` Tag**；
   - 该 GE 的 **Duration 就是窗口长度**（例如 0.8s），到期自动移除 Tag → 窗口自动关闭；
   - **这就是"缓存队列"的时间上限**。

2. **等输入**：监听 `Event.Combo.AttackPressed`

```
收到 Event.Combo.AttackPressed（说明玩家在窗口内按了）
  ├─ 窗口 Tag 存在（窗口开着）→ 消费：移除 Input.Buffer.Combo
  │      └─ StartNextSegment()：段数 Tag N → N+1，回到 ③ 播下一段
  └─ 窗口 Tag 不存在（窗口关了）→ 不消费（本次输入作废）
```

> **窗口内点击推进** 正是由「窗口 Tag 存在 → 才消费缓冲」保证的。

#### 连段结束 / 重置

- 第 3 段（最终段）播完窗口关闭后：
  - 挂 `Status.Combo.ResetPending`（宽容期，例如 0.5s）；
  - 若宽容期内玩家再按 → **从头开新连段**（清段数 Tag → 回到第 1 段）；
  - 若宽容期结束没按 → `EndCombo()`：清所有连段 Tag、移除 `Status.Combo.Attacking`，`EndAbility`。
- 任意时刻被打断（受击、移动/技能打断）：`EndCombo()` + 清缓冲。

---

## 4. 缓存队列（核心问题）怎么实现

### 4.1 为什么这里不太需要"队列"

连段输入是 **单个攻击键的连打**，不是一串离散的指令序列。同一帧最多按一次，本质是「最后一次按键的意图」。
因此缓存只需 **1 个槽位**，而不是一个 FIFO 数组。

### 4.2 用「缓冲 Tag + 窗口 GE」模拟队列

| 机制 | 用什么表达 | 作用 |
|---|---|---|
| **缓冲槽** | `Input.Buffer.Combo`（Loose Tag）| 记录"我按过攻击键" |
| **窗口** | `Status.Combo.Window`（挂在窗口 GE 上）| 记录"当前可衔接" |
| **缓存时长上限** | 窗口 GE 的 Duration | 超出窗口还没被消费 → 缓冲作废 |
| **消费** | GA 移除 `Input.Buffer.Combo` | 把缓冲翻译成"推进一段" |

工作状态机：

```
玩家按攻击键
  │
  ├─ 连段 GA 未激活          → 直接激活 GA（= 第 1 段）
  ├─ 连段 GA 已激活 & 窗口开着 → GA 消费缓冲，段数+1
  └─ 连段 GA 已激活 & 窗口关了 → 写缓冲 Input.Buffer.Combo（宽容窗口下进入待消费）
                                  （若缓冲已满，仅刷新时间戳——只保留最近一次意图）
```

> 段数推进 = **状态 Tag 变化**，缓存 = **输入意图暂存**。两者解耦，是这套方案的核心。

### 4.3 关键细节

1. **窗口长度 = 缓存寿命**
   窗口 GE 的 Duration 就是「这次输入能被接受多久」。想做得宽容（对新手友好），就把 Duration 拉长（如 1.2s）；想硬核，就缩到 0.3~0.5s。

2. **宽容窗口（可选进阶）**
   很多动作游戏允许"窗口刚过、但动画刚收招时按键仍有效"。
   做法：在窗口 GE 关闭后，额外留一个 `Status.Combo.Grace` 小窗口（如 0.15s），此期间按键同样写缓冲并推进。
   - 方案 A（简单）：把窗口 GE Duration 本身拉长（窗口=宽容期，二者合一）；
   - 方案 B（精细）：两段式——`Window` GE 结束时触发第二个 `Grace` GE，GA 在 Grace 期间也消费缓冲。
   建议先做 A，手感不够再上 B。

3. **缓冲只留最近一次**
   如果玩家狂点，`Input.Buffer.Combo` 只会被反复添加（Loose Tag 去重）。GA 消费一次就移除。
   想要更精确的"连击数"，可以把缓冲做成 Instant GE + 1 层 Stack，GA 消费时移除 1 层——但连段场景一般用不到，Loose Tag 足够。

4. **连段被打断（受击 / 其他技能）**
   - 受击时挂 `Status.HitStun`（或项目已有的受击 Tag），并用 `BlockAbilitiesWithTag` 屏蔽 `Ability.Combo.Attack`；
   - 打断时 GA `EndCombo()`：清段数 Tag + 清 `Input.Buffer.Combo`，保证不会"断连后还残留一次按键"。

5. **网络复制（如果后续要联机）**
   - 段数 Tag 走 ASC 复制（默认就复制）；
   - `Input.Buffer.Combo` 是本地输入意图，可只加在客户端 ASC 上（`AddLooseGameplayTag` 不复制）。
   - 服务器端以窗口 GE 的判定为准，天然防"按得太快导致服务器不同步"。

---

## 5. 与 GAS 的贴合点（建议直接使用原生能力）

| 需求 | 用 GAS 哪个机制 |
|---|---|
| 激活连段 GA | `TryActivateAbilitiesByTag(Ability.Combo.Attack)` |
| 段数切换动画 | GA 内 `PlayMontage` + `UAbilityTask_PlayMontageAndWait` |
| 等待窗口内输入 | `UAbilityTask_WaitGameplayEvent` 监听 `Event.Combo.AttackPressed` |
| 开窗口 / 关窗口 | `ApplyGameplayEffectToSelf(ComboWindowGE)`，靠 Duration 自动关 |
| 记录缓冲 | ASC `AddLooseGameplayTag(Input.Buffer.Combo)` |
| 消费缓冲 | ASC `RemoveLooseGameplayTag(Input.Buffer.Combo)` |
| 连段被打断 | ASC `BlockAbilitiesWithTag` + GA `CancelAbility` |
| 段数 Tag | ASC `HasMatchingGameplayTag` / 增删 `Status.Combo.Count.N` |

---

## 6. 可选演进（本期可不做）

- **段数上限与循环**：第 3 段后接 `ResetPending` 实现"最后一段 → 重新第 1 段"。
- **派生攻击**：窗口内按不同键（轻/重/特殊）走不同派生分支 → 本质是多个 `Event.Combo.*` 事件 + 多个分支 GA，结构同上。
- **伤害结算**：每段动画里用 AnimNotify 发 `Damage` GE，段数 Tag 决定伤害倍率（数据资产里按 Tag 查倍率）。
- **移动/朝向限制**：连段中挂 `Status.Combo.Attacking`，`SetMovementMode` 或 MaxWalkSpeed 变化由该 Tag 驱动。

---

## 7. 落地清单（按顺序）

1. GameplayTag 表里添加 第 2 节 所有 Tag。
2. 新建 `UGA_ComboAttack`（单 GA 管整条连段），配置 `ComboMontages` 与 `ComboWindowGE`。
3. 新建 `UGameplayEffect`（ComboWindowGE），Duration = 窗口长度，挂 `Status.Combo.Window` Tag。
4. 配置动画 Montage，在"可衔接点"加 AnimNotify（或延时节点）触发开窗口。
5. `AHelloPlayerController` 加 `AttackAction` + `OnAttack()`，按 3.1 逻辑实现。
6. `AHelloCharacter` 的 `CharacterActiveAbilities` 数组加上 `UGA_ComboAttack`。
7. （可选）受击 Tag + `BlockAbilitiesWithTag` 实现打断。
8. 测试手感：调整窗口 GE Duration、宽容期、段数 Tag 数量。

---

## 8. 一句话总结

> **段数用 GameplayTag 挂在 ASC 上，输入缓冲用「Loose Tag（Input.Buffer.Combo）+ 窗口 GE（Status.Combo.Window）」表达，窗口 GE 的 Duration 就是缓存寿命，GA 在窗口内消费缓冲推进段数，窗口外/宽容期外丢弃——这就是一套完全基于 GAS 的普通攻击连段 + 缓存队列方案。**

---

## 9. C++ Native GameplayTag 实现（已落地）

第 2 节的所有 Tag 已用 **C++ Native 形式**（`FNativeGameplayTag`）实现，不需要在编辑器手动创建。

### 9.1 已创建的文件

| 文件 | 内容 |
|---|---|
| `Source/Hello/Public/GameplayTags/HelloGameplayTags.h` | `namespace HelloGameplayTags` 内全部 Tag 的 extern 声明 + `UHelloGameplayTags` 蓝图辅助库 |
| `Source/Hello/Private/GameplayTags/HelloGameplayTags.cpp` | `UE_DEFINE_GAMEPLAY_TAG` 定义 + 段数 Tag 辅助函数 |

Build.cs 已包含 `GameplayTags` 依赖，**无需改动**。

### 9.2 C++ 使用示例

```cpp
#include "Hello/Public/GameplayTags/HelloGameplayTags.h"

// 判断段数：从 ASC 当前持有的段数 Tag 推导段号
FGameplayTag CurrentTag = /* ASC 上的 Status.Combo.Count.N */;
int32 ComboIndex = HelloGameplayTags::GetComboIndexFromTag(CurrentTag);

// 添加 / 移除 Loose Tag（输入缓冲、状态）
ASC->AddLooseGameplayTag(HelloGameplayTags::Input_Buffer_Combo);
ASC->RemoveLooseGameplayTag(HelloGameplayTags::Input_Buffer_Combo);

// 发送连段事件（参考 GA_SwitchCharacter 的用法）
FGameplayEventData Payload;
ASC->HandleGameplayEvent(HelloGameplayTags::Event_Combo_AttackPressed, Payload);
```

### 9.3 注意事项

1. **要求 UE 5.1+**：`FNativeGameplayTag` / `UE_DECLARE_GAMEPLAY_TAG_EXTERN` / `UE_DEFINE_GAMEPLAY_TAG` 是 5.1 引入的原生 Tag 宏。
2. Native Tag 在编辑器 GameplayTag Picker 的 **"Native Tags" 分组**可见，可读、不可编辑删除。
3. 若项目还有用 DataTable / 手动创建的 Tag（如换人相关），**二者可混用**，不影响。
4. **段数上限**由 `Status.Combo.Count.1~3` 决定；要加段数：在 h/cpp 各加一个 `UE_DECLARE/UE_DEFINE`，并同步 `GetComboCountTag` / `GetComboIndexFromTag` 里的 switch。
5. Tag 变量已带 `HELLO_API` 导出，将来拆多模块也能跨模块引用。
