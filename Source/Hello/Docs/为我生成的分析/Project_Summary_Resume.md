# Hello 项目 · 战斗系统总结（简历技能点归纳）

> 项目定位（一句话）：基于 UE 5.5 Gameplay Ability System（GAS）从零构建的**第三人称动作游戏战斗框架**——纯 C++ 实现连击、伤害、闪避、后摇取消等六大战斗系统，以 GameplayTag / GameplayEffect 为状态中枢，全程"方案文档 → 分阶段编码 → PIE 日志实测"驱动开发。
>
> 本文档归纳每个已实现功能的流程逻辑与对应技能点，供简历/面试使用。功能细节见 Docs/ 下四篇设计文档。

---

## 一、技术栈与工程概况

| 项 | 内容 |
|---|---|
| 引擎/语言 | Unreal Engine 5.5，纯 C++（单模块 `Hello`，无蓝图逻辑、蓝图仅做资产配置载体） |
| 核心框架 | GAS（ASC / GA / GE / GameplayTag / AbilityTask）、Enhanced Input、UMG（纯 C++ 构建控件） |
| 架构分层 | PlayerController（输入→GameplayEvent 分发）→ ASC（状态 Tag/GE 中枢）→ GA（玩法状态机）→ DataAsset（数值配置）→ AttributeSet（属性结算） |
| 配置哲学 | 数值逻辑全部留在资产层（GE 蓝图 / ComboConfig DataAsset），C++ 只做 Spec 工厂与应用；状态全部走 GAS 体系，天然支持 Block/打断/复制 |
| 规模 | 20+ C++ Native GameplayTag、6 个战斗系统 + 换人/队伍系统、4 篇设计文档、约 30 个源文件 |

---

## 二、功能流程逻辑归纳

### 1. 连击系统（5 段平 A 连段，单 GA 状态机）

**功能**：轻攻击五段连击，窗口内点击推进、错过窗口重置，连段成环（第 5 段后回到第 1 段）。

**核心链路**：

```
按攻击键（PlayerController）
  → ASC 上无连段 Tag → TryActivateAbilitiesByTag 激活 GA_NormalAttack
  → PlayCurrentSegment：读段数 Tag（Status.Combo.Count.N）→ ComboConfig 取该段动画/参数
  → 动态 Montage（AnimSequence 运行时 CreateSlotAnimationAsDynamicMontage）+ PlayMontageAndWait
  → 动画播完（OnBlendOut）→ 开窗口 GE（Status.Combo.Window，Duration=该段 WindowDuration）
  → 窗口内再按 → Event.Combo.AttackPressed → 段数 Tag N→N+1（第 5 段环回 1）→ 播下一段
  → 窗口过期/超时（MaxComboDuration 每段重置）→ 连段结束清 Tag
```

**技术要点**：

- **段数即 Tag**：`Status.Combo.Count.1~5` 挂 ASC，段数状态天然进入 `HasMatchingGameplayTag` 查询体系，供伤害/打断系统消费；
- **输入缓冲 = Loose Tag + 窗口 GE**：`Input.Buffer.Combo` 记输入意图、窗口 GE 的 Duration 即缓冲寿命，窗口外按键存预输入等播完自动连——把"队列"问题降维成"单槽位 + 时间窗"；
- 单 GA（InstancedPerActor）管整条连段而非每段一个 GA：段间数据不断流、打断清理只管一个 EndAbility；
- 宽容期 GE（ResetPending）：第 5 段播完后窗口外的最后机会，期内按键重启新一圈。

### 2. 伤害系统（元属性 + 事件驱动的武器碰撞）

**功能**：每段攻击独立的伤害结算，武器挥砍碰撞检测，伤害值由段配置决定。

**核心链路**：

```
动画帧 → AnimNotify_Enable/DisableWeaponTrace → 角色 Mesh Overlap 开/关
  → OnWeaponOverlap（TSet 同挥去重）→ ApplyDamageToActor
  → 构造 GE Spec（三参构造绑 Def + SetByCaller 传 Data.Damage）→ ApplyGameplayEffectSpecToTarget
  → GE Additive 写元属性 Damage → 目标 AttributeSet::PostGameplayEffectExecute
  → 读现值→清零→扣 Health（Base/Current 成对写）→ 广播 OnHealthChanged
```

**技术要点**：

- **Damage 元属性模式**：GE 不直写 Health 而写元属性，目标侧 `PostGameplayEffectExecute` 里读现值消费——正确处理"事后通知"语义，避免双倍结算；
- **GE 完全配置化**：伤害数值经 SetByCaller 注入，C++ 只做"取 GE 类 → 造 Spec → 应用"，新增伤害段零代码改动；
- 段号同步到角色基类 `CurrentComboSegment`，`ApplyDamageToActor` 据此查 ComboConfig 取该段 DamageGE（敌人/玩家同路径）。

### 3. 敌人血条（纯 C++ UMG + 空间 Widget）

**功能**：敌人头顶血条，随伤害实时刷新，死亡隐藏。

**核心链路**：

```
PostGameplayEffectExecute 扣血 → OnHealthChanged 多播 → 敌人 RegisterHealthChanged 回调
  → WidgetComponent（Screen 空间，显式 SetupAttachment 挂根）→ UEnemyHealthBarWidget
  → 纯 C++ RebuildWidget 构建进度条 → SetPercent 刷新；死亡隐藏
```

**技术要点**：不依赖 WBP 资产的纯 C++ 控件构建（`RebuildWidget`）；属性变更走 AttributeSet 多播委托而非轮询。

### 4. 闪避系统（打断 / 无敌 / 完美闪避 / 连段记忆）

**功能**：闪避打断普攻、无敌帧免伤、完美闪避减速攻击者、闪避后连段记忆接回。

**核心链路**：

```
按闪避键 → GA_Dodge 激活
  → CancelAbilitiesWithTag(Ability.Combo.NormalAttack) 瞬间取消普攻（走 GA EndAbility(bWasCancelled) 清理链）
  → CommitAbility（引擎原生冷却检测）→ InvincibleGE（SetDuration 覆盖无敌时长，时间制）
  → 播翻滚 Montage（Root Motion）+ WaitGameplayEvent(Event.Dodge.PerfectHit)
  [被攻击时] 攻击者 ApplyDamageToActor 查无敌 Tag → 免伤 + 发 PerfectHit 事件（Instigator=攻击者）
  → GA_Dodge 收事件 → 攻击者 SlowTime（CustomTimeDilation=0.3，世界计时器准时恢复）
  [打断普攻时] 普攻 EndAbility(cancelled) → SaveComboMemory：
     "读段号 → 清 Loose Tag → 挂记忆 GE（DynamicGrantedTags 动态授予 Count.N）"
  [闪避后按攻击] PlayCurrentSegment 读到记忆 GE 的段数 → 移除 GE 转 Loose → 从第 N 段接回
```

**技术要点**：

- **Tag 驱动的能力互斥**：`ActivationOwnedTags`（GA 存续自动持 Tag）+ `ActivationBlockedTags`（闪避中禁起手普攻），零手写状态检查；
- **DynamicGrantedTags 运行时授 Tag**：空壳 Duration GE 在运行期动态授予段数 Tag，到期自动回收——复用引擎冷却授 Tag 的原生机制；
- **Loose Tag 与 GE 同名 Tag 的计数坑**：清理顺序必须"先清 Loose 再挂 GE"，否则 `RemoveLooseGameplayTag` 计数减穿会把 GE 授予的同名 Tag 一并抹掉（实测踩坑并修复）；
- 完美闪避的减速恢复挂世界计时器——世界时间不受 CustomTimeDilation 影响，保证准时恢复。

### 5. 后摇取消（Backswing Cancel：剪断推进 + 成环 + 记忆 N+1）

**功能**：攻击确认接段时不播后摇直接剪断；后摇中按键即时剪断；闪避打断后摇按"下一段"记忆。

**核心链路**：

```
[标记] AnimSequence 资产上摆 AnimNotifyState_ComboBackswing（区间通知）
[感知] NotifyBegin → ASC->HandleGameplayEvent(Event.Combo.BackswingStart) → GA WaitBackswingTask
[剪断·路径A] 攻击有效帧按过键（bHasPendingInput）→ 后摇起点事件到达 → ScheduleAdvance（下一帧）
           → StartNextSegment：防御性收武器 → 抑制回调 → EndTask 停 Montage（短混合）→ 播下一段
[剪断·路径B] 后摇区间内按键 → OnAttackPressed 后摇分支 → 同上即时推进
[成环]     StartNextSegment：NextIndex = (N>=5) ? 1 : N+1 —— 第 5 段剪断/窗口推进都接下一圈
[记忆N+1]  闪避打断 → EndAbility："视作完成"判定（后摇标志 ∨ 窗口 Tag ∨ 宽容期 Tag）
           → 记忆段号 = 环回意义上的下一段；有效帧打断仍记 N
```

**技术要点**：

- **选型论证**：后摇标记用动画区间通知而非"配置秒数 + 世界定时器"——通知跟随动画时间轴，角色 CustomTimeDilation 变速（完美闪避减速）时自动同步，定时器则不会；
- **三层防重入**：回调内不直接 EndTask（统一下一帧调度）+ `bSuppressMontageCallbacks` 早退守卫（EndTask→StopMontage 可能同步触发 OnBlendOut/OnInterrupted）+ `bAdvancePending` 去重；
- **防御性收武器**：剪断会跳过剪断点后未触发的 DisableWeaponTrace 通知 → 剪断路径与 EndAbility 显式幂等收武器，杜绝"幻影命中/武器热残留"；
- **渐进迁移**：动画漏摆通知自动退化为旧行为（整段播完推进），资产可逐段迁移零风险；
- **语义一致性**："视作完成"判定覆盖后摇/窗口/宽容期三种打断时点，消除"等得越久打断反而亏"的语义倒挂。

### 6. 后摇移动释放（静止播后摇、一动就释放）

**功能**：攻击/闪避后摇期间推杆即切回正常移动（跑步动画），攻击侧同时窗口提前开——边跑边接段/闪避/起手。

**核心链路**：

```
[事件源] PlayerController::Move() 推杆非零每帧发 Event.Movement.Started（故意不做边沿检测）
[攻击侧] GA OnMovementStarted（bInBackswing 且未释放才生效）
        → 收武器 → 抑制回调 → 显式 Montage_Stop(SegmentBlendTime) → EndTask
        → OpenWindowAndGrace()（从 OnMontageCompleted 抽取共用：窗口 GE + 第 5 段宽容期）
        → bInBackswing 保持 true（闪避打断记忆 N+1 语义不变）→ 边跑边接段
[闪避侧] 通知类参数化 BackswingEventTag（闪避动画选 Event.Dodge.BackswingStart）
        → OnDodgeMovementStarted → 抑制 → 显式 Montage_Stop → EndTask → EndAbility(false)
        → Status.Dodge.Active 自动消失 → 可立即起手普攻（连锁红利）；无敌 GE 时间制不受影响
```

**技术要点**：

- **移动事件每帧发 + GA 幂等消化**：边沿检测会漏"后摇开始前已按住摇杆"的场景，每帧发是唯一稳解；
- **释放 ≠ 推进**：只停动画 + 提前开窗，段数 Tag 不动、GA 不结束——与剪断路径严格区分；
- **通知类参数化复用**：同一个 AnimNotifyState 类通过可编辑 Tag 属性服务攻击/闪避两套动画，已摆资产零改动（构造默认值兼容）；
- **显式 Montage_Stop 兜底**：`EndTask` 不保证代停 Montage（引擎不确定行为，两侧各实测暴露一次"日志出现但动画没真停"）——释放路径一律显式停一次（幂等），收尾不依赖引擎内部行为；
- 闪避释放的确定性收尾：抑制回调 + 直接 EndAbility，规避"EndTask 触发哪个回调不确定 → GA 永久卡活封锁普攻"的尾部风险。

### 7. 换人/队伍系统（数据驱动）

**功能**：配置化队伍生成与角色切换（出入场特效 + 切换无敌 + 延迟换人）。

**核心链路**：

```
TeamConfigDataAsset（角色类/显示名/生成偏移/初始 Tag）→ PlayerController 生成队友
按切换键 → Event.Switch.Request → GA_SwitchCharacter（挂 PlayerState ASC，事件触发式 GA）
  → 出场/入场 GameplayCue 特效 + 切换无敌 GE + SwitchDelay 延迟 → Possess 新角色
```

**技术要点**：PlayerState 级 ASC 与角色级 ASC 的权属划分（跨角色持久的 GA 挂 PlayerState）；GameplayCue 承担表现层；DataAsset 驱动队伍扩展零代码。

### 8. 基础设施层

- **C++ Native GameplayTag 体系**：`FNativeGameplayTag` 宏定义 20+ Tag（能力/状态/段数/输入缓冲/事件/数据/冷却七组），配蓝图函数库导出——不依赖编辑器手动建 Tag，多模块可引用；
- **输入 = 事件源**：PlayerController 把攻击/切换/移动等输入统一翻译成 GameplayEvent 发给 ASC，GA 侧 WaitGameplayEvent 消费——输入层与玩法层完全解耦，敌人 AI 发同款事件即可复用全部战斗逻辑；
- **动态 Montage 工作流**：动画资产统一用 AnimSequence 配置，运行时 `CreateSlotAnimationAsDynamicMontage` 转 Montage 播放——资产管线最轻，通知（武器检测/后摇标记）直接摆在 AnimSequence 上随之生效。

---

## 三、简历技能点（可直接引用的表述）

### 项目条目写法（单段版，放项目经历栏）

> **基于 UE5.5 GAS 的动作游戏战斗框架**（个人项目，纯 C++）
> 以 GameplayTag/GameplayEffect 为状态中枢，从零实现连击、伤害、闪避、后摇取消、后摇移动释放等 6 个战斗系统与换人/队伍系统；设计"预输入缓冲 + 取消窗口 + 连段成环"的连击手感体系与"闪避打断连段记忆"机制；全程方案文档驱动、PIE 日志实测验收，沉淀 4 篇设计文档与 10+ 条引擎级踩坑记录。

### 技能点 bullet（按主题挑用）

**GAS 深度应用**

- 熟练掌握 GAS 核心机制并在实战项目落地：Tag 状态驱动（段数/互斥/缓冲全部 Tag 化）、GE 生命周期（Duration/DynamicGrantedTags/SetByCaller/元属性结算）、ActivationOwnedTags 与 CancelAbilitiesWithTag 构成的能力优先级与打断体系；
- 掌握 AbilityTask 异步编程与回调重入治理：PlayMontageAndWait / WaitGameplayEvent 组合驱动玩法状态机，设计"下一帧调度 + 回调抑制标志 + 调度去重"三层防重入方案处理 Montage 停止时的同步回调风暴。

**战斗系统设计**

- 设计并实现动作游戏核心手感三件套：**输入缓冲**（Loose Tag + 时间窗 GE，把输入队列降维成单槽位+寿命）、**后摇取消**（AnimNotifyState 区间通知 → GameplayEvent → 剪断推进，通知跟随动画时间轴天然免疫变速）、**取消链语义**（闪避打断按打断时点映射连段记忆段号，消除语义倒挂）；
- 实现伤害闭环：元属性 Damage → PostGameplayEffectExecute 事后结算 → 多播驱动 UI，武器碰撞由动画通知开关 + 同挥去重；完美闪避经 CustomTimeDilation 减速攻击者（世界计时器恢复，规避 Actor 时间膨胀不同步问题）。

**工程素养**

- 坚持数值配置资产化（GE/DataAsset）与 C++ 状态机的分层：新增伤害段/新角色零代码改动；敌人 AI 复用同一条事件驱动战斗链路；
- 防御式编程习惯：幂等清理（武器/Tag/任务）、显式 Montage_Stop 兜底引擎不确定行为、漏配资产自动退化旧行为的渐进迁移设计；
- 文档驱动开发：每个功能先出方案（选型论证 + 边界清单 + 分阶段任务 + 验收标准）再编码，4 篇设计文档全程沉淀。

---

## 四、面试素材：可深挖的技术故事（坑位记录）

以下每条都是"现象 → 定位 → 根因 → 修复"完整闭环的真实踩坑，适合面试展开：

| # | 坑 | 根因与修复 | 考察点 |
|---|---|---|---|
| 1 | 连段记忆 Tag 神秘消失 | Loose Tag 与 GE 授予同名 Tag 混用：取消链双路径 EndAbility，第二次 `RemoveLooseGameplayTag` 在 Loose 计数已 0 时减穿，把 GE 授予的 Tag 一并抹掉。修复：固定"先清 Loose 再挂 GE"顺序 + 已挂标记跳过清理 | GAS Tag 计数模型理解深度 |
| 2 | 伤害双倍结算（每跳 40 而非 20） | 误把 `PostGameplayEffectExecute` 当"修改前通知"，在分支内叠加 Magnitude。修复：读 `GetCurrentValue()` 现值消费，元属性读现值→清零→扣属性 | GE 执行时序理解 |
| 3 | 剪断后偶发多余 Montage 回调 | EndTask→StopMontage 同步触发 OnBlendOut/OnInterrupted，与推进逻辑重入。修复：抑制标志 + 回调早退 + 下一帧调度三层守卫 | 异步回调重入治理 |
| 4 | 释放日志出现但角色仍被动画锁死（两次） | EndTask 不保证代停 Montage：闪避 Root Motion 下 GA 已结束但翻滚继续；攻击释放后 GA 存活等窗口、全身动画盖住跑步姿态。修复：两侧释放路径显式 `Montage_Stop`（幂等） | 引擎 API 行为边界意识 |
| 5 | 完美闪避减速与后摇剪断的时序选择 | 世界 TimerManager 不吃角色 CustomTimeDilation——配置秒数+定时器的后摇方案在减速期间会提前触发。修复：改用动画通知（随时间轴自动同步） | 系统时间模型理解 |
| 6 | GA 放 PlayerState 的 ASC 导致激活失败 | PlayerState 与 Character 双 ASC 并存，GA 授予错侧。修复：跨角色持久的 GA（换人）挂 PlayerState，战斗 GA 挂 Character | GAS 网络权属架构 |

---

## 五、一句话电梯陈述（面试开场用）

> "我用纯 C++ 在 UE5.5 上基于 GAS 搭了一套动作游戏战斗框架：状态全部 Tag 化、数值全部资产化，核心是围绕'后摇'做的手感体系——预输入缓冲、取消窗口、连段成环、闪避打断的连段记忆、移动释放，每个机制都是先用方案文档论证选型和边界再分阶段落地，靠 PIE 日志逐链路验收；过程中踩通并记录了十多条 GAS/引擎级的坑，比如 Loose Tag 和 GE 的计数减穿、EndTask 不保证停 Montage 这类不看源码发现不了的问题。"
