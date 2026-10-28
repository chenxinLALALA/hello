# 相机系统 · 面试 STAR 稿

> 用途：校招 / 初级 UE 客户端面试自述稿。
> 配套代码：`Source/Hello/Public|Private/Camera/`，设计细节见 `Docs/Camera_System.md`。
> 用法：先背 §一 的电梯版；面试展开时按 §二 的 S→T→A→R 讲；被追问细节时按 §三 的代码索引现场定位。

## 一、两版开场

**30 秒版**

> 我在一个 UE5.5 + GAS 的动作项目里做了整套第三人称相机系统。它没有用引擎模板的弹簧臂组件，而是仿照 Lyra 做了一层相机导演：相机栈挂在 PlayerController 上，底层是默认第三人称，技能演出时压入瞬态镜头，按权重混合。这样做解决了三个问题——换人时镜头无缝交接、技能特写可配置、贴墙不穿模。同时把连段命中特效做成了"命中确认才触发"，空挥不会晃镜头。

**2 分钟版**：按 S/T/A/R 四段讲，见下。

## 二、STAR

### S · Situation（背景与问题）

项目是一个第三人称动作游戏：UE 5.5 C++，战斗走 GAS（GameplayAbilitySystem），有连段、闪避、完美闪避慢动作，还有一个小队换人机制——玩家在多名角色之间 `Possess` 切换。

最初的相机是 UE 模板自带的做法：角色蓝图里挂 `SpringArmComponent + CameraComponent`。跑起来后暴露出四个问题：

1. **换人时镜头甩动**。相机组件挂在角色身上，`Possess` 新角色后相机跟着新角色的身体朝向重建；而项目角色是 `bOrientRotationToMovement`（朝移动方向转身），身体朝向和玩家视线经常不一致，切人瞬间镜头像被"重置"到角色背后。
2. **技能需要特写但没人管**。释放大招/处决时希望镜头切到固定机位或跟拍，弹簧臂做不了这种"整体接管 + 平滑回归"。
3. **贴墙穿模**。角色退到墙角时相机被墙挤进模型内部，看到角色内脏。
4. **打击感全靠动画**。命中时希望有 FOV 冲击/镜头偏移，但挥空不能有——需要把镜头效果和"真正造成伤害"绑定。

### T · Task（目标）

把相机从"角色身上的组件"升级成"项目级的相机系统"，具体目标：默认第三人称平滑跟手；技能镜头可配置、可叠加；贴墙自动拉近；换人镜头不断裂；所有过渡帧率无关、无跳变；镜头效果只在命中时触发；出错（GA 被打断、切人）不能把镜头卡死。

### A · Action（做了什么）

**A1. 相机导演放在 PlayerController 层**

自定义 `AHelloPlayerCameraManager : APlayerCameraManager`，在 `AHelloPlayerController` 构造函数里指定 `PlayerCameraManagerClass`。这是整个方案的地基：`APlayerCameraManager` 归 Controller 所有，跨 `Possess` 常驻。换人换的是 Pawn，导演不重建，栈底默认态和平滑历史都还在；旧角色的瞬态技能镜头由管理器检测到 ViewTarget 变化后主动清掉（见 A7）。

**A2. 镜头模式做成 UObject，用栈管理**

`UHelloCameraMode` 是纯计算单元（`UObject`，不是 Actor/Component），输入一个 `FHelloCameraContext`（ViewTarget、ControlRotation、锚点、DeltaTime），输出一个 `FHelloCameraModeView`（FOV + Location + Rotation）。管理器维护 `TArray<FHelloCameraModeStackEntry>`：

- 栈底永远是默认态（`UHelloCameraMode_ThirdPerson`），由角色的 `CameraModeTag` + `UCameraModeConfigDataAsset` 查表决定；
- 技能镜头 `UHelloCameraMode_Anchored` 压到栈顶，带 `bTransient` 标记和唯一 Handle；
- 每帧逐模式 `UpdateView`，再按权重从栈底往上合成：`Alpha = W / (AccumWeight + W)`，位置 Lerp、旋转 Slerp、FOV Lerp；
- `Pop` 只是标记淡出，权重归零后由 `CleanFinishedModes` 移出栈，所以技能结束自动回到默认态，不需要"记住上一个模式"。

**A3. 把"平滑"变成基类契约**

所有模式共用一套平滑：`SmoothLocation` 用 `Alpha = 1 - exp(-Speed * Dt)` 做指数衰减，`SmoothRotation` 转四元数 Slerp 再钳 Pitch（±89.9），避免三轴分别插值跨 ±180 翻转。管理器在每帧 `UpdateView` 之后调用 `CommitView` 提交"本模式自己的输出"给下一帧做起点；用显式的 `bHasLastView` 表示"有没有历史"，而不是拿零向量当哨兵；含 NaN 的输出不写入历史，防止一帧异常污染之后所有插值。

**A4. 第三人称的阻挡规避**

`UHelloCameraMode_ThirdPerson::ResolvePenetration`：从关注点（Mesh 位置 + 90cm 高度）朝理想机位做球扫（`SweepSingleByChannel`，`ECC_Camera`，半径 10），命中后沿视线拉近到 `Hit.ImpactPoint` 再留 30cm 安全距离。这里有两个细节：忽略自身 Actor，否则每帧被自己的胶囊挡住贴脸；用 `ImpactPoint` 而不是 `Hit.Location`，因为球扫的球心会提前一个半径停下，用它算会把相机推到墙后。拉近量用独立的 `PenetrationAlpha` 做指数逼近，进墙快（0.1s）、回位慢（0.25s），并且首帧直接对齐目标值，避免模式重建时出现一次假回位。

**A5. 技能镜头：配置化 + 两种触发**

`UHelloCameraMode_Anchored` 支持 Socket 锚点 + 相对偏移（`FTransform` 乘法组合，不是世界空间加减）、可选看向角色、FOV 覆盖、平滑速度覆盖。为了演出稳定，默认开启 `bFreezeAnchorOnStart`：进镜头的第一帧锁存锚点，之后不再逐帧跟随骨骼，否则呼吸/挥刀会让固定机位抖。存活时长可以走 `HoldDuration` 自动到期，也可以由动画区间/GA 控制。

触发方式做了两层：

- GA 直推：`UHelloGA` 封装 `PushSkillCamera / PopSkillCamera`，`EndAbility` 里双保险清理（句柄 Pop + 兜底清瞬态），被打断也不会把镜头卡在技能机位；
- 动画通知：`UAnimNotifyState_HelloSkillCamera` 在区间内推/弹镜头。后来又加了 `bRequireHit`：区间开始只"上膛"，真正命中才推镜头，空挥整段不产生任何镜头变化。

**A6. 命中确认的镜头效果**

FOV 冲击走引擎原生 `UCameraModifier` 子类 `UHelloCameraModifier_Punch`，内部维护冲击列表，多次命中各自衰减、互不覆盖。关键点是执行顺序：栈合成必须写在 `UpdateViewTargetInternal` 覆盖里，因为基类 `UpdateViewTarget` 会在它之后调用 `ApplyCameraModifiers`，这样冲击才叠在合成结果上；写在 `UpdateViewTarget` 末尾就会被栈覆盖掉。

"只在命中时触发"用上膛/消费模式：动画通知到打击帧时只把参数记在角色身上（上膛，带 0.2s 确认窗口），`ApplyDamageToActor` 真正结算伤害后才消费并触发；挥空、打无敌帧都不触发。状态记在角色而不是 AnimNotify 上，因为 Notify 是共享 CDO，不能存运行时状态。

**A7. 换人连续性**

`ServerSwitchToCharacter` 里在 `Possess` 前后保存/恢复 `ControlRotation`。根因是引擎 `Possess` 会把 ControlRotation 对齐新 Pawn 的 ActorRotation，而本项目身体朝向跟随移动，和视线不一致，不还原就会"甩到背后"。另外管理器检测到 ViewTarget 变化时立刻 `RemoveAllTransientModes`，避免旧角色的技能镜头按新角色重新解析 Socket；角色贴上相机时的淡出也只在纯第三人称生效（技能特写需要角色出镜），并用 90/130cm 双阈值迟滞防止闪烁。

### R · Result（结果与反思）

可验证的结果：

- 相机导演跨 `Possess` 常驻，换人只切 ViewTarget，技能镜头不会串到新角色；
- 技能镜头 Push 后栈大小为 2，结束后日志 `Mode finished`、栈回到 1，打断路径同样回位；
- 贴墙时相机拉近到障碍表面外 30cm，离开后平滑回位，不穿模；
- 挥空时无任何 FOV/镜头变化，命中才触发，且一次上膛只消费一次（多目标不连震）；
- 所有过渡使用帧率无关的指数曲线，低帧率下不会变慢/变陡；
- 单人项目、客户端本地运行；AI 与远端角色通过 `GetForActor` 过滤，不会抢玩家镜头。

踩过的坑（面试时挑一两个讲，最能体现真实度）：

1. 修饰器不生效——栈合成放在 `UpdateViewTarget` 末尾，被基类的 `ApplyCameraModifiers` 之前覆盖掉，后来拆到 `UpdateViewTargetInternal` 才正确。
2. 相机贴脸——球扫没忽略自身 Actor，每帧被自己的胶囊挡住。
3. 回位时相机穿墙——用球心落点 `Hit.Location` 计算，少算一个球半径；改用 `ImpactPoint`。
4. 平滑状态用"零值=未初始化"的哨兵，第一帧会从原点插值，出现一次飞入；改成显式 `bHasLastView`，并对 NaN 做拦截。
5. 技能镜头泄漏——GA 被打断时 `NotifyEnd` 不一定触发，`EndAbility` 兜底 Pop 才彻底解决。
6. 命中早于上膛——动画通知必须不早于 `EnableWeaponTrace`，否则同帧命中会漏；技能镜头区间同样要提前 1~2 帧。

## 三、代码索引（按讲述顺序读）

建议按 ①→⑥ 的顺序读，每块 10~15 分钟，读完能对应上面的 A1~A7。

**① 系统骨架（先看这个，理解"谁拥有谁"）**

| 文件 | 位置 | 看什么 |
|---|---|---|
| `Private/PlayerController/HelloPlayerController.cpp` | L12-16 | 构造函数指定 `PlayerCameraManagerClass`，相机导演归 Controller |
| `Public/Camera/HelloPlayerCameraManager.h` | L19-31、L109-159 | 栈条目结构（句柄 + `bTransient`）、栈成员、Punch/淡出状态 |
| `Public/Camera/HelloCameraMode.h` | L13-54、L60-61 | `FHelloCameraModeView` / `FHelloCameraContext`、模式基类为什么是 UObject |

**② 每帧链路（面试最常被问"相机怎么更新"）**

| 文件 | 位置 | 看什么 |
|---|---|---|
| `Private/Camera/HelloPlayerCameraManager.cpp` | L150-164 | `UpdateViewTarget`：调 Super、关闭空转的 Punch、角色淡出 |
| 同上 | L166-191 | `UpdateViewTargetInternal`：换目标检测 → 清瞬态 → 定栈底 → 清淡出 → Super → 栈合成 |
| 同上 | L260-340 | `EvaluateCameraStack`：组装 Context、逐模式 Tick/Blend/UpdateView/CommitView、加权合成、写 POV |
| 同上 | L342-364 | `CleanFinishedModes`：淡出完成才出栈，栈底至少保留一个 |

**③ 平滑与混合（体现数学细节）**

| 文件 | 位置 | 看什么 |
|---|---|---|
| `Private/Camera/HelloCameraMode.cpp` | L10-29 | BlendAlpha 推进 + `Pow(Alpha, BlendExponent)` 权重曲线 |
| 同上 | L40-52 | `CommitView`：NaN 拦截、历史视图提交 |
| 同上 | L54-86 | `SmoothLocation` / `SmoothRotation`：`1-exp(-Speed*Dt)`、Slerp、Pitch 钳制 |

**④ 第三人称与阻挡规避**

| 文件 | 位置 | 看什么 |
|---|---|---|
| `Private/Camera/HelloCameraMode_ThirdPerson.cpp` | L17-53 | 关注点 → 理想机位 → 探测 → 位置滞后 → 旋转滞后 → 输出 |
| 同上 | L55-119 | `ResolvePenetration`：忽略自身、球扫 `ECC_Camera`、`ImpactPoint`、安全距离、穿透系数首帧对齐 |

**⑤ 技能镜头与命中确认**

| 文件 | 位置 | 看什么 |
|---|---|---|
| `Public/Camera/HelloCameraMode_Anchored.h` | L13-57 | 可配置项：Socket、偏移、看向、FOV、锁存锚点、平滑覆盖、Hold |
| `Private/Camera/HelloCameraMode_Anchored.cpp` | L34-50 | Hold 到期自动淡出（幂等，与手动 Pop 不冲突） |
| 同上 | L52-107 | 锚点/看向的解析回退链（Socket → 默认 → Mesh → Context） |
| 同上 | L109-145 | 锁存锚点、`BaseAnchor * AnchorOffset`、看向覆盖、基类平滑 |
| `Private/Camera/HelloPlayerCameraManager.cpp` | L59-110 | Push/Pop：句柄分配、`bTransient`、Pop 只标记淡出 |
| `Private/Animation/AnimNotifyState_HelloSkillCamera.cpp` | L8-60 | 区间通知：普通模式直接 Push/Pop；`bRequireHit` 分支只上膛 |
| `Public/Character/HelloCharacterBase.h` | L126-140 | `ArmSkillCameraForHit` / `TryConsumeSkillCameraOnHit` 等接口 |
| `Private/Character/HelloCharacterBase.cpp` | L324-432 | 上膛、命中推入、区间结束回位/延迟回位、只清等待态 |
| `Private/AbilitySystem/GA/HelloGA.cpp` | L27-63、L65-90 | GA 侧封装与 `EndAbility` 兜底清理 |

**⑥ 叠加冲击、换人与边界**

| 文件 | 位置 | 看什么 |
|---|---|---|
| `Public/Camera/HelloCameraModifier_Punch.h` | L10-59 | 冲击实例、`Trigger`、`IsActive` |
| `Private/Camera/HelloCameraModifier_Punch.cpp` | L22-51 | 倒序遍历、指数衰减、FOV + Yaw/Pitch 偏移 |
| `Private/Camera/HelloPlayerCameraManager.cpp` | L130-148 | 复用同一 modifier、结束后 `DisableModifier` |
| `Private/Character/HelloCharacterBase.cpp` | L459-467、L554-627 | `DisableWeaponTrace` 清膛；`ApplyDamageToActor` 命中后消费 Punch / 技能镜头 |
| `Private/PlayerController/HelloPlayerController.cpp` | L204-239 | 换人：位置对调 + `ControlRotation` 保存恢复 |
| `Private/Camera/HelloPlayerCameraManager.cpp` | L193-258、L366-378、L380-466 | 栈底切换、清瞬态、角色贴近淡出（双阈值迟滞） |
| `Public/DataAsset/CameraModeConfigDataAsset.h` | L15-47 | Tag → 模式类 的配置资产 |

## 四、预判追问

**Q1：为什么不用引擎自带的 SpringArm + Camera？**
SpringArm 是"挂在角色身上的一个组件"，能力边界就是跟随 + 拉近；本项目需要镜头被技能整体接管、需要换人跨 Possess 常驻、需要按权重混合多层镜头，这些都不是组件模型擅长的。自己实现镜头模式 + 栈，本质是把 Lyra 的做法迁移到我们项目结构上，同时保留 SpringArm 里好用的思想（弹簧臂长度、滞后、穿透探测）。

**Q2：为什么相机管理器挂 Controller 而不是角色？**
`Possess` 会重建角色侧组件并打断角色逻辑，而 `APlayerCameraManager` 归 Controller，跨 Possess 常驻。换人时只换 ViewTarget，导演和栈底默认态保留，旧角色的瞬态镜头主动清理后重新开始，这是"无缝交接"的前提。这也是我们和 Lyra 最大的偏离点——Lyra 是单角色，相机组件放 Pawn 没问题。

**Q3：为什么镜头模式是 UObject 而不是 Component/Actor？**
模式是纯计算单元，不需要场景位置、Tick 组件、复制，一个技能镜头实例化一次、PoP 后销毁。UObject 足够轻，也方便 `NewObject` 动态创建和 GC 管理。做成 Actor 会引入场景开销和生命周期问题。

**Q4：多个镜头怎么混合？**
每帧先逐模式算 View，再按权重增量合成：`Alpha = W / (AccumWeight + W)`，位置 Lerp、旋转 Slerp、FOV Lerp，从栈底往上累加。权重是 `Pow(BlendAlpha, BlendExponent)`，所以淡入淡出不是线性的，后半程更明显。

**Q5：怎么保证不同帧率手感一致？**
所有平滑都用 `1 - exp(-Speed * DeltaTime)`，等价于指数衰减的时间常数形式，而不是固定步长 Lerp；穿透系数的逼近、角色淡入淡出的时间推进也都用 DeltaTime 归一。这样 30/60/120 帧下的收敛速度一致。

**Q6：相机陷入墙体怎么办？**
从关注点向理想机位做球形 sweep（`ECC_Camera`，半径 10），取 `ImpactPoint`（不是球心落点），沿视线回退 30cm 安全距离；拉近/回位分别用独立时间常数平滑，且首帧直接对齐，避免重建时假回位。

**Q7：为什么修饰器要放在 `UpdateViewTargetInternal`？**
引擎顺序是 `UpdateViewTarget → UpdateViewTargetInternal → ApplyCameraModifiers`。栈合成如果写在 `UpdateViewTarget` 末尾，会先合成、再被基类内部覆盖成默认 POV，最后修饰器叠加在默认 POV 上；写进 `UpdateViewTargetInternal`，修饰器就叠在合成结果上，FOV 冲击才看得见。

**Q8：技能镜头会不会卡住？**
三条兜底：`Pop` 幂等（已淡出就不再处理）；`EndAbility` 统一清（GA 句柄 + 角色记录 + 等待命中的窗口）；换 ViewTarget 时直接 `RemoveAllTransientModes`，防止旧镜头按新角色解析 Socket。`NotifyEnd` 在打断时可能不触发，所以不能只靠它。

**Q9：为什么镜头要"锁存锚点"？**
固定机位如果逐帧跟随骨骼，呼吸、挥刀、受击都会让画面轻微抖。`bFreezeAnchorOnStart` 在进入镜头的第一帧锁存 Socket 变换和看向目标，之后只吃配置的偏移，画面稳定；需要手持跟拍时关掉即可。

**Q10：命中才触发是怎么做的？**
Arm/Consume 模式：打击帧通知只上膛（参数记在角色上，确认窗口 0.2s），`ApplyDamageToActor` 真正结算伤害后消费并触发。状态不能放 AnimNotify，因为 Notify 是共享 CDO。挥空、完美闪避（无敌帧提前 return）都不会消费。

**Q11：换人镜头为什么会甩？怎么修的？**
引擎 `Possess` 会把 ControlRotation 对齐新 Pawn 的 ActorRotation；本项目角色朝移动方向转身，身体朝向和视线不一致，于是镜头绕到背后。修法是在换人 RPC 里 `Possess` 前后保存/恢复 ControlRotation。没有覆盖 `Possess`，因为这个引擎版本里它不是可覆盖虚函数，且 `OnPossess` 时旧 Pawn 已经被松开。

**Q12：AI 敌人放技能会抢玩家镜头吗？**
不会。`GetForActor` 只认本地 PlayerController 控制的 Pawn，AI/远端返回 nullptr；GA 侧拿不到管理器就静默失败。

**Q13：这套系统在联机下成立吗？**
当前是单人项目，镜头只在本地跑；命中判定和镜头触发都在客户端本地，若做多人需要把"命中确认"改成服务器权威或 GameplayCue 驱动，否则各端表现会不一致。这是明确的边界。

**Q14：性能开销如何？**
每帧模式数通常 1~2，主要是 1 次球扫 + 若干向量运算，开销可忽略；球扫只在第三人称且开启穿透规避时执行，且 `AddIgnoredActor` 避免自挡。

**Q15：如果重做，会改什么？**
把"命中确认"的状态机和镜头模式解耦成通用事件；相机栈支持按 Tag 查询/替换；增加单元测试式的固定步进模拟（给定输入序列断言输出）；多人时改服务端权威。

## 五、PIE 演示脚本（3 分钟）

1. 第三人称跟手：转视角、跑动，看位置/旋转滞后；贴墙后退，看相机拉近与回位（日志 `SetupPlayerInputComponent` 不必讲）。
2. 技能镜头：放一次带镜头通知的技能，看栈大小 2 → 1，`Mode finished` 后镜头平滑回位。
3. 命中确认：空挥一次——无镜头变化；打中敌人——镜头推入/FOV 冲击。
4. 打断：技能镜头生效中按闪避，确认镜头回位、栈回到 1。
5. 换人：在侧对镜头时切角色，视角方向保持，不甩到身体背后。

## 六、背诵卡片

- 架构：`PlayerCameraManager`（Controller 层）+ `UHelloCameraMode` 栈 + Tag 配置资产。
- 混合：`Pow(BlendAlpha, BlendExponent)`；合成 `Alpha = W / (AccumWeight + W)`。
- 平滑：`1 - exp(-Speed * Dt)`；Slerp + Pitch ±89.9。
- 第三人称：ArmLength 400、Offset 90、Lag 8/12、球扫半径 10、安全距离 30、穿透 0.1/0.25。
- 技能镜头：Blend 0.25/0.35、锚点锁存、Socket 回退链、Hold 幂等淡出。
- 叠加：`UCameraModifier`，在 `ApplyCameraModifiers` 阶段生效，必须在 `UpdateViewTargetInternal` 之后。
- 命中确认：arm/consume，确认窗口 0.2s，一次上膛只消费一次。
- 换人：保存/恢复 ControlRotation + `RemoveAllTransientModes`。

## 七、诚实边界

单人本地项目；没有做网络同步与回滚；相机不做物理碰撞体，只做探测；动画资源、Socket 命名依赖美术配置；Punch 效果最终没有上动画（保留接口），实际使用的是技能镜头的"命中才生效"。面试时主动说明边界，比被问出来更好。
