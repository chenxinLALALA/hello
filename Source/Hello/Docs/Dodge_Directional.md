# 方向闪避（已实现）

> 状态：**已实现并验证通过**（2026-09-29：方向输入冲刺、无输入战斗后闪、无输入非战斗前冲、后摇释放后松开方向再闪避、后摇移动释放，全部 PIE 日志实测通过）
> 依赖：闪避系统（`Docs/Dodge_System.md`）、EnhancedInput；改动覆盖 Controller / PlayerState / GA_Dodge / 角色基类四处

## 一、功能总览

| 输入场景 | 行为 | 状态 |
|---|---|---|
| 有方向输入（任意方向含斜向） | 角色转向输入方向 → 播前冲动画 | 已验证 |
| 无方向输入 + 战斗中（bInCombat=true） | 角色转向镜头方向，播后闪动画（向镜头内部/屏幕深处后退） | 已验证 |
| 无方向输入 + 非战斗中 | 角色转向镜头方向，播前冲动画（向镜头内部冲刺） | 已验证 |
| 后摇释放后松开方向再闪避 | 正确识别为无输入（走 bInCombat 分支） | 已验证 |
| 后摇期间推杆 | 触发 `Event.Dodge.BackswingStart` → 提前结束闪避动画，交还移动控制 | 已验证 |

八向闪避由"转向 + 前冲动画"天然覆盖：输入向量 → `Atan2` 连续角度 → 角色旋转到任意朝向，只需两份动画资产（前冲 + 后闪）。

## 二、核心链路

```
IA_Move（Started/Triggered/Completed 三事件绑定）
  → HelloPlayerController
      Triggered（Move）        : LastMoveInput = 当前输入值（每帧，电平语义）
      Completed（MoveCompleted): LastMoveInput = 0（松开即清零）
  → IA_Dodge（Started）
      → OnDodgeStarted → TryActivateAbilitiesByTag(Ability.Dodge.Dodge)
      → GA_Dodge::ActivateAbility
          → PlayDodgeMontageByDirection()
              ① Cast AHelloCharacterBase 取角色身上的闪避动画
              ② 读 Controller->LastMoveInput
              ③ 从 PlayerController 读 `CameraYaw = GetControlRotation().Yaw`
              ④ SelectDodgeAnimationAndRotation：
                   有输入   → 前冲动画，TargetYaw = CameraYaw + Atan2(X=左右, Y=前后)
                   无输入   → 查 PlayerState->bInCombat
                              战斗   → 后闪动画，TargetYaw = CameraYaw（角色面向镜头，动画向屏幕里后退）
                              非战斗 → 前冲动画，TargetYaw = CameraYaw（角色面向镜头，动画向屏幕里冲刺）
              ⑤ SetActorRotation(TargetYaw)
              ⑥ CreateSlotAnimationAsDynamicMontage → PlayMontageAndWait
              ⑦ 若动画上配置了 `Combo Backswing` 通知（BackswingEventTag = Event.Dodge.BackswingStart），
                 进入后摇区间后推杆 → `OnDodgeMovementStarted` 显式 `Montage_Stop(0.1f)` + EndTask + EndAbility
      → EndAbility → ClearLastMoveInput()（兜底清零）
```

输入约定：`FVector2D` **X=左右、Y=前后**（与 `Move()` 里 `AddMovementInput(Right, X)` / `AddMovementInput(Forward, Y)` 一致）。方向判定基准是**摄像机 ControlRotation.Yaw**，不是角色自身 Rotation：W=镜头前向、D=镜头右向、A=镜头左向、S=镜头后向、斜向连续。无输入时统一面向镜头（向屏幕里运动）。

## 三、关键设计决策

1. **双动画 + 转向，而非四/八向动画**：曾评估四向动画（美术成本 4~8 份）与纯旋转硬掰（侧向动画视觉滑移）。最终方案只需前冲 + 后闪两份：有输入时"旋转角色 + 前冲动画"表达任意方向。
2. **方向判定基准 = 摄像机 ControlRotation.Yaw，不是角色自身 Rotation**：玩家按 W 时期望朝"屏幕里"冲刺，而不是朝角色当前面朝冲刺。后闪同理——战斗中无输入时朝"屏幕里"后退（角色面向镜头）。
3. **后闪动画资产规范 = "角色面向镜头、向屏幕里后退"**：原设计"角色背对镜头 + 后闪动画"导致视觉上是往屏幕外跑；改为角色面向 CameraYaw 播放后闪动画，才符合"向镜头内部"的手感。
3. **动画配置在角色基类而非 GA**：`AHelloCharacterBase` 上三个 `UAnimSequence*`（DodgeForwardMontage / DodgeBackwardMontage / DodgeMontage 兜底）。GA 是行为逻辑，动画是角色资产——将来每个角色（含换人小队、敌人）各配各的闪避动画，GA 只读不配置。
4. **bInCombat 放 PlayerState（小队级）**：`AHelloPlayerState` 上 `UPROPERTY(EditDefaultsOnly)`，全队共享的战斗节奏状态。当前手动在蓝图 Class Defaults 勾选测试；将来接"锁定目标/受击进入战斗、脱战计时器"系统时由代码/蓝图驱动。
5. **LastMoveInput 电平语义**（三轮迭代定稿，见第五节）：Triggered 每帧更新真实值 + Completed 清零。不依赖 Started 边沿。

## 四、文件清单

| 文件 | 改动 |
|---|---|
| `Public/Private/PlayerController/HelloPlayerController` | 新增 `LastMoveInput`（BlueprintReadOnly）；`MoveStarted`（占位）/ `MoveCompleted`（清零）两个事件处理；`Move()` 开头无条件更新 LastMoveInput；`ClearLastMoveInput()` 供 GA 调用；SetupInputComponent 补 Started/Completed 绑定 |
| `Public/Private/PlayerState/HelloPlayerState` | 新增 `bInCombat`（EditDefaultsOnly，Category="Combat"） |
| `Public/Private/Character/HelloCharacterBase` | 新增 `DodgeForwardMontage` / `DodgeBackwardMontage` / `DodgeMontage`（兜底）三个动画属性（EditDefaultsOnly，Category="Dodge"） |
| `Public/Private/AbilitySystem/GA/GA_Dodge` | 删除 GA 侧动画属性；新增 `PlayDodgeMontageByDirection()`（读输入 → 选动画 → 按需旋转 → 建动态 Montage）与 `SelectDodgeAnimationAndRotation()`（三分支判定）；EndAbility 里 `ClearLastMoveInput()` 兜底 |

## 五、LastMoveInput 三轮迭代记录（避坑核心）

输入记录方案经历三次返工，每轮修一个问题又暴露下一个——最终电平语义一次解决全部：

| 轮次 | 方案 | 暴露的问题 |
|---|---|---|
| 1 | Triggered 里更新 + 死区过滤 | 后摇释放推杆后残留值污染下一次闪避（第二次闪避变前冲） |
| 2 | Started 记录 / Completed 清零（方案 B） | **Started 瞬间传入值是零**（Enhanced Input 时序）：日志实锤 `MoveStarted: X=0.000 Y=0.000` |
| 3 | Started 置意图 flag，Triggered 有 flag 才更新 | **边沿丢失**两处致命：① PIE 启动/焦点切换瞬间按住 W，Started 不触发 → flag 永假；② `ClearLastMoveInput` 顺手清了 flag，而按住不放的键不会再触发 Started → flag 永久失效（日志实锤：闪避前无 MoveStarted、MoveInput 恒 0） |
| 定稿 | **电平语义**：Triggered 无条件更新 + Completed 清零 | 无。松开后 Triggered 停止、惯性滑行不产生 Triggered，值自然保持零 |

关键认识：

- **Enhanced Input 的 Started 是"从零到非零"的边沿事件**，传值不可靠（可能为零）、可能丢失（启动前按住）。做"当前方向读取"要用 Triggered（电平，按住期间每帧真值）+ Completed（释放边沿，唯一可靠的"结束"信号）这对组合。
- **清除状态类标志前先想清楚谁能把它置回来**：ClearLastMoveInput 清 flag 后，"按住中"的键没有新的边沿来重置 flag——电平状态被边沿逻辑破坏后无法自愈。
- **GA 结束时的输入清零（ClearLastMoveInput）保留为兜底**：电平语义下即使清了值，下一帧按住中的键会立刻被 Triggered 刷回真值，无副作用。

## 六、其他避坑

- **方向基准从"角色面向"改为"摄像机 ControlRotation.Yaw"**：玩家操作以镜头为参照，按 W 就应朝屏幕里跑。代码里取 `PlayerController->GetControlRotation().Yaw` 作为基准，而不是 `Avatar->GetActorRotation().Yaw`。
- **后闪动画资产规范 = "角色面向镜头、向屏幕里后退"**：因为无输入时角色转到 `CameraYaw`（面向镜头），后闪动画的根运动必须朝角色背后（即屏幕里）。如果美术做成"背对镜头向后跑"，会看见角色往屏幕外冲——此时把后闪分支的 `CameraYaw` 改回 `CameraYaw + 180.f` 即可。
- **前冲动画资产规范**：角色面向镜头/输入方向，根运动沿角色 X+（即屏幕里或输入方向）。
- **旋转与 Root Motion 的时序耦合**：动态 Montage 创建后 PlayMontageAndWait 立即播放，SetActorRotation 必须在创建/播放之前完成；当前代码已满足。若仍出现时序问题，在 SetActorRotation 后调 `Mesh->RefreshBoneTransforms()` 强制同步。
- **后摇移动释放通知配置**：闪避 AnimSequence 上摆 `Combo Backswing` 通知状态，细节面板 **Backswing Event Tag** 必须显式改为 `Event.Dodge.BackswingStart`（默认是 `Event.Combo.BackswingStart`，GA_Dodge 不消费）。后摇区间内推杆 → `OnDodgeMovementStarted` → 显式 `Montage_Stop(0.1f)` + EndTask + EndAbility。
- **bInCombat 用 EditDefaultsOnly 才在蓝图可见**：纯 BlueprintReadWrite 实测蓝图 Class Defaults 不显示该字段（同现象见讨论记录）；如需运行时读写改回 BlueprintReadWrite 并由代码驱动。
- **GA 判定死区 `IsNearlyZero(0.1f)`**：兜住松开过程最后几帧 Triggered 传的小残留值（Completed 清零前的短暂窗口）。
- **换人小队**：动画在角色基类，每个队员蓝图各自配置；PlayerState 的 bInCombat 全队共享，切人不影响闪避行为一致性。

## 七、编辑器配置

**已完成**：角色蓝图（BP_PlayerCharacter）Class Defaults → Dodge 分类：DodgeForwardMontage = Dodge_F_Anim、DodgeBackwardMontage = 后闪动画、DodgeMontage = 兜底；前冲/后闪动画上均配置 `Combo Backswing` 通知，`Backswing Event Tag` 改为 `Event.Dodge.BackswingStart`，实现后摇期间推杆释放。PlayerState 蓝图 Class Defaults → Combat 分类：bInCombat 手动勾选用于测试。

**待完成（后续系统接入，非本功能阻塞）**：

1. bInCombat 的真实驱动：进入战斗（受击/锁定/攻击命中）置 true，脱战计时器置 false
2. 敌人侧闪避（当前敌人无 GA_Dodge，接入时需给敌人蓝图配 Dodge 动画三件套）

## 八、验收日志样例（2026-09-29 PIE 实测）

```
[GA_Dodge] 闪避激活，无敌帧 0.30 秒
[GA_Dodge] [BP_PlayerCharacter0_C_0] 闪避方向：前冲 | MoveInput=X=0.000 Y=0.000 | CameraYaw=120.0 | 旋转=0 | bInCombat=0
[ComboBackswing] BP_PlayerCharacter0_C_0 的动画 [Dodge_F_Anim] 进入后摇区间（后摇时长=0.51 秒）→ Event.Dodge.BackswingStart 已发出
[GA_Dodge] 闪避后摇中移动 → 提前结束闪避动画，交还移动控制
```

排查要点：
- `MoveInput` 非零必有 `旋转=1`；`旋转=0` 但位移方向不对 → 查动画资产方向/摄像机基准。
- 后摇释放不生效：先看 `[ComboBackswing]` 日志有没有 `Event.Dodge.BackswingStart`；没有 → 动画通知 Tag 配错或未覆盖到当前播放的动画。
- `MoveInput` 与按键状态不符 → 查 Controller 三事件绑定。
