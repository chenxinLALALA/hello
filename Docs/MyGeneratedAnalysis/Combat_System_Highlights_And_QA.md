# Hello 战斗系统：模块亮点、难点与关键问题解答

> 范围说明：工作区目前只有一个 Unreal Engine 5.5 项目 `Hello`（纯 C++ + GAS，蓝图只做资产配置）。本文按功能模块归纳“各个项目”的突出点与难点，并回答四个问题：
>
> 1. 这个系统中的前后摇是怎么设计的？
> 2. 推进到下一个连段动作会突变吗，怎么解决？
> 3. 输入缓存是怎么做的？
> 4. 这些功能为什么不考虑联机模式，如果需要联机应该怎么做？

## 一、总体架构

项目定位是一套“动作游戏战斗框架”：以 Gameplay Ability System（GAS）为状态中枢，以 GameplayTag / GameplayEffect 表达状态，以 DataAsset 承载数值，以 AnimSequence + AnimNotify 承载表现。

- 输入层：`AHelloPlayerController` 用 Enhanced Input 把按键翻译成 GameplayEvent 发给 ASC，输入与玩法解耦。
- 状态层：`UHelloASC`（角色级）挂战斗 GA；`AHelloPlayerState` 的小队级 ASC 挂换人 GA。两类 ASC 职责分离。
- 能力层：`UGA_NormalAttack` 单 GA 管整条 5 段连段，`UGA_Dodge` 管闪避，`UGA_SwitchCharacter` 管换人。
- 数据层：`UComboConfigDataAsset` 配置每段动画、伤害 GE、窗口时长、宽容期；`UTeamConfigDataAsset` 配置小队。
- 表现层：`AnimNotify_Enable/DisableWeaponTrace` 开武器判定，`AnimNotifyState_ComboBackswing` 标记后摇区间。
- 属性层：`UHelloAttributeSet` 用 `Damage` 元属性中转伤害，`PostGameplayEffectExecute` 事后消费并广播给 UI。

所有 GameplayTag 均为 C++ Native Tag（`HelloGameplayTags.h/.cpp`），不依赖编辑器手动建 Tag。

## 二、各模块的突出点与难点

| 模块 | 突出点 | 难点 / 踩坑 | 主要位置 |
|---|---|---|---|
| 连段系统 | 单 GA 状态机管整条连段，段数用 `Status.Combo.Count.1~5` Tag 挂在 ASC 上，窗口期/重启期/连段成环（第 5 段回第 1 段） | 窗口 GE 用 `SetDuration` 做运行时覆盖会被资产 Def 顶替，最终改为 Loose Tag + 世界定时器（Plan B）；推进时必须“即关窗”，否则窗口 Tag 漏到下一段会造成连点加速 | `GA_NormalAttack`、`ComboConfigDataAsset`、`HelloGameplayTags` |
| 后摇取消 | 后摇用 `AnimNotifyState_ComboBackswing` 区间通知标在动画上，`NotifyBegin` 发 `Event.Combo.BackswingStart`，GA 据此剪断推进 | 剪断会跳过剪断点后的 `DisableWeaponTrace`，必须显式收武器；`EndTask→StopMontage` 可能同步触发 Montage 回调，用 `bSuppressMontageCallbacks` + 下一帧调度 + 去重三层防重入 | `AnimNotifyState_ComboBackswing`、`GA_NormalAttack` |
| 后摇移动释放 | 静止播后摇、一动就释放；移动事件每帧发、GA 幂等消化；释放后提前开窗，边跑边接段 | `EndTask` 不保证真正停 Montage，必须显式 `Montage_Stop`；释放不等于推进（段数不动、GA 不结束）；释放路径必须自己开窗口，否则接不了段 | `GA_NormalAttack`、`GA_Dodge`、`HelloPlayerController::Move` |
| 闪避系统 | 高优先级取消普攻、无敌帧 GE、完美闪避减速攻击者、闪避打断后连段记忆、方向闪避只用“前冲+后闪”两份动画 | 双 ASC 陷阱（GA 挂在错误的 ASC 上会激活失败）；Loose Tag 与 GE 授予同名 Tag 的计数减穿；减速恢复必须挂世界计时器；`LastMoveInput` 电平语义经过三轮迭代才稳定 | `GA_Dodge`、`HelloCharacterBase`、`HelloPlayerController` |
| 伤害与属性 | `Damage` 元属性中转 + SetByCaller 传值 + `PostGameplayEffectExecute` 事后消费；武器碰撞由动画通知开关，`TSet` 同一挥砍去重 | `PostGameplayEffectExecute` 里若再叠 Magnitude 会双倍结算；手动写属性必须 Base/Current 成对写；GA 外构造 Spec 要用三参构造防 Def 为空崩溃 | `HelloAttributeSet`、`HelloCharacterBase::ApplyDamageToActor` |
| 换人小队 | PlayerState 小队级 ASC 挂事件触发式换人 GA，Possess 换角色并保留小队状态 | 跨角色持久 GA 与角色级战斗 GA 的归属必须分清；换人 GA 若给错 ASC 会永远不触发 | `GA_SwitchCharacter`、`HelloPlayerState`、`HelloPlayerController` |
| 敌人血条 | 纯 C++ UMG（`RebuildWidget` 构建进度条），属性广播驱动刷新，死亡隐藏 | 旧蓝图实例组件脱挂到世界原点；非标准 `CreateWidget+SetWidget` 时序会导致血条脱离组件 | `HelloEnemy`、`EnemyHealthBarWidget` |
| 相机系统（方案） | 已产出完整方案：相机导演放 PlayerController、镜头模式 UObject + 栈模型、默认态 Tag 驱动、技能镜头 Push/Pop | 尚未编码。预判坑：旋转混合必须 Slerp/短路径、技能 GA 被打断需 Pop 防镜头泄漏、Possess 首帧 ViewTarget 为空 | `Docs/Camera_System.md` |

## 三、问题 1：前后摇是怎么设计的？

结论：前摇、有效判定帧、后摇不是靠代码里的秒数/定时器切分的，而是直接写在每段 `AnimSequence` 的动画时间轴上，再通过动画通知把“关键边界”发给 GAS。代码只消费事件，不硬编码时长。

具体划分：

1. 前摇：动画开头到 `EnableWeaponTrace` 之间，属于起手/蓄势阶段。系统没有给它发单独事件，它只是正常播放的动画前段；此阶段按攻击键不会推进，只会被存成预输入，等后摇起点或窗口期再消费。只有闪避这类高优先级能力可以直接打断它。
2. 有效判定帧：由 `AnimNotify_EnableWeaponTrace` 和 `AnimNotify_DisableWeaponTrace` 标记。开通知后角色 Mesh 对 Pawn 开 Overlap 并清空 `HitEnemiesThisAttack`，关通知后关碰撞。
3. 后摇：由 `AnimNotifyState_ComboBackswing` 标记。`NotifyBegin` 时向角色 ASC 发 `Event.Combo.BackswingStart`，`GA_NormalAttack` 收到后置 `bInBackswing = true`。

后摇区间里可发生三件事：

- 有预输入：`OnBackswingStarted` 立即 `ScheduleAdvance`，剪掉剩余后摇接下一段。
- 后摇中按攻击键：`OnAttackPressed` 看到 `bInBackswing`，立即剪断推进。
- 后摇中推移动杆：`OnMovementStarted` 显式停 Montage 并提前开连段窗口，角色切回跑步，段数不动（释放不等于推进）。

这样设计的好处是：后摇标记跟随动画时间轴，完美闪避把攻击者降到 0.3 倍速时，后摇事件仍然和动画同步触发；如果改成“配置秒数 + 世界定时器”，定时器不吃 `CustomTimeDilation`，慢放时会提前触发。漏摆 `ComboBackswing` 通知的动画会自动退化为“整段播完才推进”，不崩溃、可渐进迁移。

闪避的“后摇”复用同一个通知类，只是把 `BackswingEventTag` 从 `Event.Combo.BackswingStart` 改成 `Event.Dodge.BackswingStart`，`GA_Dodge` 用来做“后摇中推杆提前交还移动控制”。

## 四、问题 2：推进到下一个连段动作会突变吗，怎么解决？

会存在“硬切”的风险，但系统用了四层手段把它压到可接受的观感。

1. 剪断点由美术放在动画后摇区，而不是任意帧。方案明确建议把 `ComboBackswing` 起点放在 `DisableWeaponTrace` 之后，也就是剪断只丢弃“收招尾巴”，有效攻击帧和伤害窗口不会中途被切掉。这是从设计上减少突变的关键。
2. 段间短混合。动态 Montage 的 BlendIn/BlendOut 从原来的 0（硬切）改为 `SegmentBlendTime`（默认 0.1 秒），剪断推进、自然换段、移动释放共用这个短混合，动作之间有一个快速淡入淡出。
3. 下一帧再执行，不在回调栈里切。`ScheduleAdvance` 用 `SetTimerForNextTick` 延迟到下一帧执行，并配合 `bAdvancePending` 去重，避免在动画通知/任务回调里立刻 `EndTask+PlayMontage` 造成重入和视觉抖动。
4. 切段前防御性收武器。剪断会跳过剪断点之后未触发的 `DisableWeaponTrace`，`StartNextSegment` 显式调用 `DisableWeaponTrace()`，保证下一段从“武器冷状态”起手，不会残留上一段的幻影命中。

同时 `bSuppressMontageCallbacks` 会在剪断/换段期间抑制旧 Montage 的 `OnBlendOut/OnInterrupted` 回调，防止“停旧 Montage 时同步触发回调”导致双重推进或双重结束。

如果追求更柔顺，可以继续把 `SegmentBlendTime` 调大，或者进一步做成手动 Montage 多 Section + `JumpToSection` / 动画蓝图内做姿态融合；当前项目选择的是“短混合 + 美术摆好剪断点”的轻量方案。

## 五、问题 3：输入缓存是怎么做的？

实现采用的是“单槽位 + 生命周期定时器”的预输入缓存，而不是 FIFO 队列。

输入入口：

- `IA_NormalAttack` 绑定 Started / Triggered / Completed。点按立即处理一次，长按由 `AttackHeldTimerCallback` 按间隔重复触发。
- `HandleAttackInput()` 先查角色 ASC 上是否有 `Status.Combo.Attacking`：在连段中就发 `Event.Combo.AttackPressed` 给 GA；不在连段中就 `TryActivateAbilitiesByTag(Ability.Combo.NormalAttack)` 走完整激活检查。

缓存本体是 GA 里的 `bool bHasPendingInput`：

- 动画播放期间按攻击键，`OnAttackPressed` 先置 `bHasPendingInput = true`，再按当前状态分流。
- 窗口期 `Status.Combo.Window` 存在：消费预输入，下一帧推进到 N+1（第 5 段回第 1 段）。
- 后摇期 `bInBackswing`：消费预输入，立即剪断剩余后摇推进。
- 重启期 `Status.Combo.ResetPending` 存在：消费预输入，清段数 Tag、挂 `Count.1`、重播第 1 段。
- 三个分支都不命中（动画播放中）：预输入保留，等 `OnMontageCompleted` 自然播完时消费推进。

为什么单槽位就够了：连段输入本质是“同一个攻击键的最后一次按下意图”，不是一串离散指令，同帧最多按一次，所以只需要保存最近一次意图。重复狂点只是刷新这个意图，不会被排成一个会越积越多的队列。

缓存的“寿命”由三个时间量控制，全部来自 `ComboConfigDataAsset`：

- `WindowDuration`（每段，默认 0.8 秒）：动画自然播完后允许接段的输入窗口。到期 `OnWindowExpired` 摘窗口 Tag、挂重启 Tag。
- `GraceDuration`（每段，默认 0.15 秒）：窗口过期后的重启宽容期。窗口 + 宽容期都过完由 `OnComboExpired` 结束连段。
- `MaxComboDuration`（默认 3.0 秒）：动画播放期间的兜底上限，防止超长动画导致连段一直吊着。

一个历史细节：Tag `Input.Buffer.Combo` 在 Native Tag 里定义了，但最终实现里真正的缓存职责由 `bHasPendingInput` 承担，Tag 没有参与 Add/Remove。文档里保留它主要是为了将来蓝图/AI 侧可查询“玩家是否按过攻击键”。`EndAbility` 会显式清 `bHasPendingInput`，避免实例复用（InstancedPerActor）时上一轮死区里留下的预输入跨连段存活，造成“一次点击出两段”的 bug。

## 六、问题 4：为什么不考虑联机模式，如果需要联机应该怎么做？

### 为什么不考虑联机

1. 定位是单机动作手感验证框架。核心目标是快速迭代后摇取消、输入缓存、连段记忆、完美闪避这些手感机制，这些机制高度依赖本地输入和本地时间，单机实现最直接。
2. 当前连段的窗口期/重启期/段数状态用 `AddLooseGameplayTag` 挂在 ASC 上，而 Loose Tag 默认不复制，这套实现天然是本地语义，不适合直接拿来当服务器权威状态。
3. 伤害由“谁触发 Overlap 谁结算”的 `ApplyDamageToActor` 完成，没有服务器权威校验和延迟补偿；减速用 `CustomTimeDilation`，也是 Actor 本地时间，不复制。
4. 虽然项目里已经有一些网络脚手架（ASC `SetIsReplicated(true)`、属性 OnRep、`ServerSwitchToCharacter`、PlayerState ASC Mixed 模式），但它们是局部准备，不是完整联机方案。设计上把联机作为“后续迁移项”而不是当前目标。

### 如果要联机，建议的改造路径

1. 把连段权威状态从 Loose Tag 迁移到可复制的载体。段数、窗口、重启、无敌、记忆这些状态改用带 GrantedTags 的 Duration GE，或者明确设置可复制的 Tag 容器；本地输入意图（`bHasPendingInput` 类）只留在拥有者的客户端。
2. 明确服务器权威边界。服务器负责激活能力、推进段数、开窗/关窗、伤害结算；客户端做本地预测提升手感，预测失败时回滚。`GA_NormalAttack` 目前是 `LocalPredicted`，需要按能力逐个评估改成 `Server` 或保留预测并补 PredictionKey 处理。
3. 伤害与命中检测收归服务器。客户端不应直接决定伤害结果；可让服务器做武器 Overlap/校验，或用“客户端预测命中 + 服务器验证目标、距离、无敌帧”的 RPC，再由服务器应用伤害 GE。完美闪避事件也要由服务器判定后广播。
4. 时间相关逻辑要重新设计。`CustomTimeDilation` 默认不复制，完美闪避减速不应直接改对方 Actor 的时间；多人下建议改成只影响受击方的表现层/攻击硬直，或做成服务器复制的时间缩放状态，避免不同客户端观感不同步。
5. 输入缓冲要保持“客户端缓冲、服务器裁决”。客户端可以在窗口开始前就缓存输入，但推进以服务器判定为准；可以给缓冲一个略宽的服务端窗口，或把输入时间戳随 RPC 上传，让服务器判断是否落在窗口内。
6. 统一 ASC 归属与复制模式。战斗 GA 挂角色级 ASC，换人等跨角色 GA 挂 PlayerState ASC；GameplayEffect 用合适的 ReplicationMode（Full/Minimal/Mixed），并确认所有 Native Tag 在 `DefaultGameplayTags.ini` 里可见、可复制。
7. 换人链路已经具备 `ServerSwitchToCharacter` + `TeamMembers` 复制，联机时让 Possess 在服务器执行并自然复制，换人 GA 的效果（入场/出场/无敌）按表现层与权威层拆开。
8. 增加网络延迟下的容错。例如把接段窗口在服务器端做小幅放宽，或使用服务器时间作为唯一裁决基准，避免高延迟玩家“按了但不进下一段”的挫败感。

一句话总结：现状是“手感优先的本地实现”，联机需要把本地 Loose Tag 状态和本地伤害/减速逻辑替换成“可复制的服务器权威状态 + 客户端预测 + 服务器裁决”。

## 七、遗留项与改进建议

- 闪避冷却 GE（`GE_Dodge_Cooldown`）尚未在编辑器里完成配置，代码已走引擎原生冷却机制。
- 敌人正式 AI 未接入，目前用 `StartLoopAttack/StopLoopAttack` 做完美闪避测试。
- `bInCombat` 仍靠蓝图 Class Defaults 手动勾选，未接入“进入/脱战”驱动。
- Q/Z 技能输入已绑定但能力未实现。
- 相机系统只有方案文档，未编码。
- 联机改造未做，见上文第六节。
