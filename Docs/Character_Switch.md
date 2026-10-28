# 换人系统（现状整理）

> 状态：**已实现，单机 PIE 可用**（整理日期 2026-10-27）
> 相关系统：GAS（PlayerState 级 ASC）、相机系统（`Camera_System.md` §四、§二十一、§二十二）、输入（Enhanced Input）
> 本文只记录现状与已知问题，不包含新的设计改造。

## 一、能力总览

| 能力 | 现状 | 落点 |
|---|---|---|
| 切换输入 | 循环切下一位 / 直接切 1 号位 | `OnSwitchNext` / `OnSwitchTo1`（`HelloPlayerController`） |
| 服务器权威 | 切换走 `Server, Reliable` RPC | `ServerSwitchToCharacter` |
| 位置交接 | 新旧角色**对调世界位置与朝向** | 同上 |
| 视角交接 | `Possess` 前后保存/还原 `ControlRotation` | 同上（见 §三·4） |
| 控制权交接 | `Possess(NewTarget)`，相机管理器不重建 | `AHelloPlayerController` / `AHelloPlayerCameraManager` |
| 小队列表 | 首位角色 Possess 时记录，并按配置生成队友 | `OnPossess` + `SpawnTeammatesFromConfig` |
| 换人演出 | 事件驱动 GA：出场 Cue → 延迟 → 入场 Cue + 无敌 GE | `UGA_SwitchCharacter`（挂 PlayerState ASC） |
| 镜头交接 | 换人时立即清除旧角色的技能镜头，默认镜头保持 | `HelloPlayerCameraManager::RemoveAllTransientModes` |

## 二、职责与数据

```
AHelloPlayerController
  ├─ TeamConfig (UTeamConfigDataAsset)      // 队伍配置：角色类 / 生成偏移 / 显示名 / 初始 Tag
  ├─ TeamMembers (Replicated TArray)        // 运行时小队成员
  ├─ CurrentMemberIndex                     // 当前成员下标（仅本地/服务端，未复制）
  ├─ PlayerCameraManagerClass = AHelloPlayerCameraManager
  └─ ServerSwitchToCharacter(NewTarget)     // 换人权威入口

AHelloPlayerState
  └─ ASC (Mixed 复制)
       └─ StartUpAbilities（含 UGA_SwitchCharacter）→ 以 Ability.Switch 标签激活并常驻

UGA_SwitchCharacter（事件触发式常驻 GA）
  ├─ 监听 Event.Switch.Request
  ├─ OutCueTag → SwitchDelay(0.15s) → InCueTag + InvincibilityGE
  └─ 清理 State.Switching，回到监听

AHelloPlayerCameraManager（Controller 拥有，跨 Possess 常驻）
  ├─ ViewTarget 变化 → RemoveAllTransientModes() + DetermineBaseCameraMode()
  └─ 每帧按镜头栈合成 POV
```

## 三、核心链路

### 1. 输入与请求

`SetupInputComponent` 把 `SwitchToNextAction` / `SwitchTo1Action` 绑定到 `OnSwitchNext` / `OnSwitchTo1`：

- `OnSwitchNext`：`(CurrentMemberIndex + 1) % TeamMembers.Num()`，成员数 ≤ 1 时直接返回。
- `OnSwitchTo1`：目标固定为 `TeamMembers[1]`，目标等于当前 Pawn 时返回。
- 两者都只调用 `RequestSwitchToCharacter` → `ServerSwitchToCharacter`（`Server, Reliable`）。

`CurrentMemberIndex` 只在执行输入的端更新（本地客户端），服务端收到 RPC 后不会同步更新它，且它不是复制属性；`TeamMembers` 是复制属性。

### 2. 服务端换人

`ServerSwitchToCharacter_Implementation` 的顺序：

1. 空目标、目标等于当前 Pawn → 直接返回。
2. 记录双方世界位置与朝向，然后**对调**：旧角色去新角色的位置，新角色去旧角色的位置，朝向一并交换。
3. 记录当前 `ControlRotation`。
4. `Possess(NewTarget)`。
5. 若切换成功（`GetPawn() == NewTarget`），还原 `ControlRotation`。
6. 取 `AHelloPlayerState` 的 ASC，发 `Event.Switch.Request`（`Instigator = PreviousChar`，`OptionalObject = NewTarget`）。
7. 打日志 `Switched from X to Y`（Warning 级）。

对调位置是为了让“玩家看到的世界”连续：新角色出现在旧角色刚才的位置，相机不需要长距离追赶。朝向对调则是为了让新角色的身体朝向接上旧角色的朝向。

### 3. 视角连续性（2026-10-27 新增）

引擎的 Possess 流程会把 `ControlRotation` 对齐到新 Pawn 的 `ActorRotation`。本项目角色是 `bUseControllerRotationYaw = false` + `bOrientRotationToMovement = true`，身体朝向跟移动方向走，和玩家视线 Yaw 经常不一致。不处理的话，切人瞬间镜头会绕到新角色“身体背后”，看起来像相机位置被重置。

现在的做法是在换人 RPC 内包围 `Possess`：`Possess` 前存 `GetControlRotation()`，`Possess` 后 `SetControlRotation` 回去。

- 为什么不在 `Possess` 覆盖里：本引擎版本的 `AController::Possess` 不可作为可覆盖虚函数（编译报无该成员）。
- 为什么不在 `OnPossess` 里：`Possess` 内部会先松开旧 Pawn，`OnPossess` 执行时旧视角与“是否换人”的判断都拿不到。
- 代价：只覆盖换人路径。复活/重生成等其它 `Possess` 路径若需要同样效果，需要在对应位置补一次存取。
- 联机注意：`SetControlRotation` 在服务器上不复制给客户端；单机/Listen PIE 无影响。

详细记录见 `Camera_System.md` §二十二。

### 4. 相机层交接

`AHelloPlayerCameraManager::UpdateViewTargetInternal` 检测到 `ViewTarget` 变化时：

1. `RemoveAllTransientModes()`：立即移除技能镜头（不淡出），避免旧角色的 Socket 镜头按新角色重新解析。
2. `DetermineBaseCameraMode()`：按新角色的 `CameraModeTag` 对齐栈底默认镜头。同镜头类时复用原实例，滞后历史连续；不同镜头类时会重建（目前未做视图种子，见 `Camera_System.md` §二十二·6）。

相机管理器本身归 PlayerController，跨 Possess 不重建，因此没有黑屏或重建抖动。

### 5. GAS 侧：小队友军 GA

- `AHelloPlayerState` 构造时创建 ASC（`SetIsReplicated(true)`，Mixed 模式）。
- `BeginPlay` 在服务端授予 `StartUpAbilities`，然后用 `Ability.Switch` 标签 `TryActivateAbilitiesByTag` 激活 `UGA_SwitchCharacter`，只激活一次（`bSwitchAbilityActive`）。
- GA 常驻监听 `Event.Switch.Request`：
  - 收到事件 → 从 `OptionalObject` 取目标角色 → 播 `OutCueTag`（出场，代码已接，当前未配置/未验证）→ `WaitDelay(SwitchDelay)`。
  - 延迟结束 → 在目标 ASC 上播 `InCueTag`（入场）、应用 `InvincibilityGE`、移除 `State.Switching`；然后清理自身 `State.Switching`、清空目标、重新监听。

换人 GA 挂 PlayerState ASC 而不是角色 ASC：角色会被换下/销毁重建，小队级能力需要跨角色常驻。

### 6. 角色 Possess 侧的 GAS 处理

- `AHelloCharacterBase::UnPossessed`：`CancelAllAbilities()` 后 `Super::UnPossessed()`。
- `AHelloCharacterBase::PossessedBy`：重新 `InitAbilityActorInfo(this, this)`；有权威时补发 `InitialStartUpAbilities`（角色主动/被动技能，重复授予会跳过）。

## 四、配置方式

1. **TeamConfig 资产**（`UTeamConfigDataAsset`）：在 PlayerController 蓝图的 `Team` 分类指定。`TeamMembers` 每项包含 `CharacterClass`、`DisplayName`、`SpawnOffset`、`InitialTags`。第 0 项对应当前出生的主控角色（不会重复生成），第 1 项起在首位 Possess 时按 `SpawnOffset` 生成。
2. **PlayerState GA**：`StartUpAbilities` 里加入 `GA_SwitchCharacter`，GA 必须带 `Ability.Switch` 标签。
3. **GA 参数**：`OutCueTag`、`InCueTag`、`InvincibilityGE`、`SwitchDelay`（默认 0.15s）。
4. **输入**：`SwitchToNextAction` / `SwitchTo1Action` 在 PlayerController 蓝图指定并绑定。
5. **相机**：各角色 `CameraModeTag`（默认 `Camera.Mode.ThirdPerson`）。

## 五、验收检查清单（PIE）

| # | 操作 | 期望 |
|---|---|---|
| 1 | 按循环切换键 | 在 `TeamMembers` 间循环，被控角色与位置对调，无黑屏、相机不重建 |
| 2 | 静止且视角与角色朝向不同时切人 | 画面不位移、不转向（§三·3） |
| 3 | 奔跑中切人 | 镜头方向保持；新角色可立即响应输入 |
| 4 | 切人后按 W / 闪避 | 移动、闪避方向仍以镜头为基准 |
| 5 | 技能镜头播放中切人 | 技能镜头立即清除，栈大小回到 1，画面回到新角色第三人称 |
| 6 | 换人特效 | 配好 Cue/GE 时能看/感受到出场→延迟→入场与无敌 |
| 7 | 切回原角色 | 恢复可控，技能与属性仍在该角色自己身上 |
| 8 | 成员数 ≤ 1 | 切换输入无效果、不报错 |

## 六、调试方法

- 正常换人：Output Log 出现 `Switched from X to Y`（Warning 级）。
- 视角保持：出现 `[HelloCamera] Possess preserved ControlRotation: Pitch=... Yaw=... Roll=... (Pawn=...)`。
- 技能镜头清理：切人时出现 `[HelloCamera] RemoveAllTransientModes N->M`。
- 镜头默认态切换：两人 `CameraModeTag` 映射不同镜头类时出现 `[HelloCamera] Base mode switched to ...`；同类时不出现（正常）。
- 按了没反应：
  - `TeamMembers.Num() <= 1`（`OnSwitchNext` 直接 return；检查 TeamConfig 是否配置、首位 Possess 是否在服务端执行）。
  - 目标等于当前 Pawn（`OnSwitchTo1` 直接 return）。
  - 服务端没收到 RPC（联机下检查 owner/authority；单机不涉及）。
- 换人后 GAS 异常：确认 `PossessedBy` 里的 `InitAbilityActorInfo` 执行、角色技能是否随角色（角色级 ASC）而非 PlayerState（小队级 GA）。

## 七、已知问题与限制

### 1. 【未处理，先记录】奔跑 / 下落状态下换人，动画不停止

**现象**：换人前角色处于奔跑或下落状态时，换人后该角色仍保持奔跑/下落动画，不会自动停止，直到下次切回该角色。

**影响**：观感问题（换下场的角色原地跑/悬空动画），不影响当前被控角色的输入与战斗。当前无 AI 驱动，换下场的角色没有任何逻辑把它带回待机。

**初步判断（未验证根因）**：换人链路目前只做位置/朝向对调和 `Possess`；`UnPossessed` 只取消 ASC 能力，没有显式处理移动状态、速度、动画状态，也没有 AI 接管。动画停留在切换前状态是合理结果。

**决策**：**暂不修改**。计划在后续加入 AI 逻辑动画后，由 AI 接管换下场的角色并驱动回待机/locomotion，届时一并验证；如果 AI 接管后问题仍在，再从换人/UnPossess 路径补显式清理。

**后续修复入口（给将来的自己）**：
- 在 `AHelloCharacterBase::UnPossessed` 或换人链路里，对旧角色显式 `StopMovementImmediately()`、清空移动输入意图、按需停止动画（或交给 AnimBP 的状态机）。
- 若走 AI 方案：确认换下场的角色确实被 AIController 接管，并且 AI 状态机能从“奔跑/下落”回到待机；否则问题会原样保留。

### 2. 其它现状限制（未列入本次目标）

- **位置对调没有碰撞检查**：旧角色的位置可能靠墙、在窄道或半空，直接把新角色放过去可能穿插/卡住。
- **`CurrentMemberIndex` 未复制**：只在执行输入的端更新，服务端不会随 RPC 同步，联机下两边“下一位”可能不一致。
- **小队只在服务端生成**：`SpawnTeammatesFromConfig` 仅在权威端执行，依赖 `TeamMembers` 复制与 `ServerSwitchToCharacter` 参数解析；联机前需要完整验证。
- **换人没有冷却/状态检查**：输入侧、RPC 侧都没有“切人中”或“不可切人”的拦截；`State.Switching` 在 GA 里只被移除，代码里没看到添加点（若蓝图也未添加，该标签实际不生效）。
- **换人时技能镜头硬切**：`RemoveAllTransientModes` 是立即移除（有意为之），不是淡出。
- **`SetControlRotation` 不复制**：见 §三·3 联机注意。
- **不同镜头类角色之间切换**：栈底会重建，可能有一次回正跳变（方案 B 未做，见 `Camera_System.md` §二十二·6）。

## 八、文件清单

| 文件 | 职责 |
|---|---|
| `Public/Private/PlayerController/HelloPlayerController.h/.cpp` | 输入绑定、换人 RPC、位置对调、视角保持、小队生成 |
| `Public/Private/DataAsset/TeamConfigDataAsset.h/.cpp` | 队伍配置资产（成员类/偏移/显示名/初始 Tag） |
| `Public/Private/PlayerState/HelloPlayerState.h/.cpp` | 小队级 ASC、授予并激活换人 GA |
| `Public/Private/AbilitySystem/GA/GA_SwitchCharacter.h/.cpp` | 事件驱动换人演出：出场/入场 Cue、无敌 GE、State.Switching |
| `Public/Private/Character/HelloCharacterBase.h/.cpp` | Possess/UnPossess 的 ASC 处理 |
| `Public/Private/Camera/HelloPlayerCameraManager.h/.cpp` | ViewTarget 变化时清理瞬态镜头、对齐默认镜头 |
| `HelloGameMode.cpp` | 指定 PlayerControllerClass（默认 Pawn 在蓝图 GameMode 配置） |
