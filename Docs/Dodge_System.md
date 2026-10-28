# 闪避系统（已实现）

> 状态：**已实现并验证通过**（2026-09-09：激活、打断普攻、无敌免伤、完美闪避减速、连段记忆全部过日志实测；冷却 GE 仅剩编辑器配置）
> 依赖：GAS、EnhancedInput；前置系统见 `Docs/Combat_Damage_HealthBar.md`（伤害链路）

## 一、功能总览

| 功能 | 说明 | 状态 |
|---|---|---|
| 闪避激活与输入 | IA_Dodge → Controller → 按 Tag 激活 GA_Dodge | 已验证 |
| 高优先级打断普攻 | 激活瞬间取消普攻；闪避中普攻被 Block | 已验证（激活日志） |
| 无敌帧 | GE_Dodge_Invincible 挂 `Status.Dodge.Invincible`（默认 0.3s，GA 上可调） | 已验证（Tag 生效） |
| 免疫伤害（双保险） | GE_Damage 蓝图 Application Ignore + ApplyDamageToActor 显式检查 | 代码链路已验证 |
| 完美闪避 | 命中无敌帧中的目标 → 免伤 + 攻击者时间减速 | **已验证**（0.3 倍速动画实测拉长） |
| 独立冷却 | 引擎原生机制：冷却 GE 配在 GA 蓝图 Cooldowns 分类 | 代码就绪，**GE 待配置待测** |
| 连段记忆 | 闪避打断普攻后限时接回被打断的段 | **已验证**（2/3/4 段打断→接回→继续推进，日志实测） |
| 方向闪避 | 方向输入转向冲刺 / 无输入按战斗状态前冲或后闪 | **已验证**（2026-09-29，详见 `Dodge_Directional.md`） |

## 二、核心链路

闪避激活：

```
按键 IA_Dodge（Started）
  → HelloPlayerController::OnDodgeStarted
    → TryActivateAbilitiesByTag(Ability.Dodge.Dodge)   ← 查的是角色级 ASC
      → GA_Dodge::ActivateAbility
          ① CommitAbility（冷却 GE 由引擎在此自动检测+应用，C++ 零冷却代码）
          ② InvincibleGE：SetDuration(InvincibleDuration) 运行时覆盖资产时长 → 挂无敌 Tag
          ③ PlayMontageAndWait 播 DodgeMontage（BlendOut→正常结束 / Interrupted→取消结束）
          ④ WaitGameplayEvent 监听 Event.Dodge.PerfectHit（GA 存续期间）
```

优先级与互斥（全部 GAS 原生，构造函数配置）：

```
GA_Dodge 激活 → CancelAbilitiesWithTag(Ability.Combo.NormalAttack) → 进行中的普攻被取消
GA_Dodge 存续 → ActivationOwnedTags 自动持有 Status.Dodge.Active
             → GA_NormalAttack 的 ActivationBlockedTags 据此阻断闪避中起手普攻
GA_Dodge 结束 → Tag 自动移除，普攻恢复可用
```

完美闪避（攻击者侧判定 → 目标侧响应）：

```
攻击者武器 Overlap
  → HelloCharacterBase::ApplyDamageToActor
      查目标 ASC 有 Status.Dodge.Invincible？
        有 → 免伤 + 记 HitSet（同挥砍不重复触发）
           + HandleGameplayEvent(Event.Dodge.PerfectHit, Instigator=攻击者) 发到【目标】ASC
             → GA_Dodge::OnPerfectDodge
               → 攻击者 SlowTime(PerfectSlowScale=0.3, PerfectSlowDuration=1.5)
                   CustomTimeDilation=0.3（动画/移动整体变慢）
                   世界计时器 1.5s 后恢复 1.0（世界时间不受膨胀影响，准时恢复）
        无 → 正常伤害链路（见 Combat_Damage_HealthBar.md）
```

免疫为什么配两层：GE_Damage 蓝图的 **Application Tag Requirements → Application Ignore Tags** 加 `Status.Dodge.Invincible` 是数据层兜底（任何代码路径应用 GE 都会被 GAS 拦下，且未来可按攻击做例外——做不可闪避攻击只需复制一个不带此条件的 GE 变体）；`ApplyDamageToActor` 的显式检查是为了拿"完美"语义（记 HitSet + 发事件通知闪避 GA）。缺省后者仍免疫但无减速，缺省前者依赖调用方自觉。

连段记忆（闪避打断普攻 → 限时接回被打断的段）：

```
普攻第 N 段进行中按闪避
  → GA_Dodge 激活取消普攻（或 Dodge Montage 抢占 → OnMontageCancelled）
  → 两条路径都汇到 SaveComboMemory()（bComboMemorySaved 防重复挂 GE）：
      ① 段号 N 从 Count.N Tag 读出（必须在清 Tag 前）
      ② 挂 ComboMemoryGE：SetDuration=记忆窗口，DynamicGrantedTags 动态授予 Count.N
         （引擎原生 Cooldown 授予 Tag 同款机制；GE 到期 Tag 自动回收）
  → 窗口内玩家按攻击 → GA_NormalAttack 重激活
      → PlayCurrentSegment 读到 Count.N（GE 来源）
      → RemoveActiveGameplayEffect(记忆GE) + 改挂 Loose Tag → 从第 N 段接回，后续推进走既有流程
  → 窗口过期不攻击 → GE 到期 Tag 消失 → 下次攻击从第 1 段重开
```

实现要点：OnMontageCancelled 识别闪避打断（查 `Status.Dodge.Active`）后以 `bWasCancelled=true` 结束——走原来的 EndCombo 会以 false 结束导致记忆丢失；EndAbility 侧不查 Dodge.Active（取消链执行时该 Tag 的挂载时序不保证），`bWasCancelled` 本身就是可靠信号。

## 三、测试状态与待办

**已验证（2026-09-09，日志实测）**：

- 闪避激活正常（`[GA_Dodge] 闪避激活，无敌帧 0.30 秒`）
- 完美闪避事件全链路四条日志按序出现：
  `命中无敌帧中的[玩家] → 完美闪避 → 时间减速至 0.30 倍 → 到时时间恢复正常`
- **减速效果**：0.3 倍速实测生效，敌人攻击动画时长明显拉长（GA_NormalAttack 的动画起止日志可量化对比：持续时长 ÷ 动画资产时长 = 1 ÷ 流速）
- **连段记忆全链路**（第 2/3/4 段各打断一次，连续接回）：
  `被【闪避】打断于第 N 段 → 连段记忆 5.0 秒（GE=1，Tag即时校验=1）`
  `连段记忆命中 → 接回第 N 段` → 播的确实是第 N 段 → 后续 N→N+1 正常推进
  （期间经历并修复了 Loose/GE Tag 同名计数减穿问题，见避坑节）

**待测试（剩余项）**：

1. **无敌免伤双保险**：临时清空 GA 蓝图的 InvincibleGE 再被命中 → 应仍不掉血但**无**完美闪避日志（GE 层静默拦截）；填回后完整链路恢复。测完务必填回
2. **闪避记忆窗口过期**：闪避打断后等 5 秒（当前窗口值）再攻击 → 应从第 1 段重开（GE 到期 Tag 回收）
3. **打断/互斥行为**：闪避中按攻击 → 无反应；闪避结束后攻击 → 正常起手
4. **冷却**：配好 GE_Dodge_Cooldown 后，闪避后立刻再按 → 无反应且不刷激活日志；到期后恢复可用

**敌人循环攻击测试函数**（正式 AI 接入后删除）：`AHelloEnemy::StartLoopAttack()` / `StopLoopAttack()`，间隔 `TestAttackInterval`（默认 2s，蓝图可调），复用普攻 GA 全套逻辑（含 AnimNotify 武器碰撞）。

## 四、文件清单

| 文件 | 职责 |
|---|---|
| `Public/Private/AbilitySystem/GA/GA_Dodge` | 闪避本体：激活/无敌 GE/Montage/完美闪避监听与减速/任务清理 |
| `Public/Private/GameplayTags/HelloGameplayTags` | 新增 5 个原生 Tag：Ability.Dodge.Dodge / Status.Dodge.Active / Status.Dodge.Invincible / Event.Dodge.PerfectHit / Cooldown.Dodge |
| `Public/Private/PlayerController/HelloPlayerController` | DodgeAction 输入属性 + OnDodgeStarted 按 Tag 激活 |
| `Public/Private/Character/HelloCharacterBase` | 完美闪避判定（ApplyDamageToActor 查无敌 Tag）+ SlowTime/RestoreTimeDilation |
| `Public/Private/Character/HelloEnemy` | StartLoopAttack/StopLoopAttack 测试函数 |
| `Public/Private/AbilitySystem/GA/GA_NormalAttack` | 闪避互斥（ActivationBlockedTags）+ 连段记忆（SaveComboMemory 挂 GE / PlayCurrentSegment 接回 / OnMontageCancelled 识别闪避打断分流）；GetSegmentData 改 Cast 基类（修敌人取不到 ComboConfig） |

## 五、编辑器侧配置

**已完成**：GE_Dodge_Invincible（Duration + GrantedTags=Status.Dodge.Invincible）、GE_Damage 配 Application Ignore Tags、闪避动画填入 GA_Dodge、IA_Dodge 绑键并填 Controller、角色蓝图授予 GA_Dodge、敌人 ComboConfig 配单段攻击动画、GE_ComboMemory 填入 GA_NormalAttack（记忆窗口实测 5 秒）

**待完成**：

1. GE_Dodge_Cooldown 蓝图：Parent = GameplayEffect，Duration Policy = Has Duration（时长填真实 CD 值），GrantedTags 加 `Cooldown.Dodge`
2. GA_Dodge 蓝图 → Class Defaults → **Cooldowns 分类 → Cooldown Gameplay Effect** 填入上面 GE
3. 可调参数：InvincibleDuration（GA_Dodge）、PerfectSlowScale / PerfectSlowDuration（GA_Dodge）、ComboMemoryWindow（GA_NormalAttack）

## 六、关键实现认识（避坑）

- **双 ASC 陷阱**：项目有角色级 ASC 与 PlayerState 小队级 ASC，两者授予列表和 Tag 容器完全独立。GA 授予在哪个 ASC，TryActivateAbilitiesByTag 就必须在哪个 ASC 上调用——GA_Dodge 曾配在 PlayerState 的 StartUpAbilities 导致按键无反应
- `CancelAbilitiesWithTag` / `ActivationBlockedTags` 只对**同一个 ASC** 上的能力和 Tag 生效，跨 ASC 无原生联动
- WaitGameplayEvent 监听的是发到**本 ASC** 的事件，HandleGameplayEvent 的目标 ASC 要选对（完美闪避事件发给闪避者，不是攻击者）
- 减速恢复计时器必须挂**世界计时器**（`World->GetTimerManager()`）：挂 Actor 自身计时器会被 CustomTimeDilation 拖慢，实际恢复时间 = Duration ÷ Scale
- 冷却走引擎原生（GA 类默认 Cooldown GE，CommitAbility 自动处理），C++ 不写任何检测/应用代码
- PlayMontageAndWait 只绑 OnBlendOut + OnInterrupted：再绑 OnCompleted/OnCancelled 会双触发 EndAbility
- 运行时给 GE 动态授予 Tag 用 `Spec.Data->DynamicGrantedTags`（引擎原生 Cooldown 同款），GE 资产侧不用预配 GrantedTags；配合 `SetDuration` 覆盖时长，一个空壳 GE 资产适配所有段号/时长
- 被打断路径的 `bWasCancelled` 信号：Montage 被抢占走 OnInterrupted → 若回调里走 EndCombo 会以 false 结束，需要记忆/取消语义时必须在回调里显式以 `bWasCancelled=true` 调 EndAbility，并用 `IsActive()` 防与取消链重入
- **Loose Tag 与 GE 授予 Tag 同名混用的计数陷阱**（实测丢记忆根因）：取消链会把 EndAbility 调两次，`RemoveLooseGameplayTag(Count.N)` 第二次执行时 Loose 计数已为 0，减穿后**连 GE 授予的同名 Tag 也会从 ASC 上消失**（日志表现：挂 GE 成功且短暂可读，下次激活却读不到）。正确顺序：清 Loose Tag → 再挂记忆 GE；已挂记忆后跳过同名 Tag 的 Loose 清理
