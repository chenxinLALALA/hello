# 相机系统（Lyra 类）（方案）

> 状态：**阶段 1/2 已完成并通过 PIE 验收（2026-10-15）**；**阶段 3 阻挡规避已实现（2026-10-25），待 PIE 验收**；阶段 4 未开始。
> 依赖：GAS 事件驱动、Native GameplayTag、换人小队（`ServerSwitchToCharacter` / `Possess`）
> 选型（用户已确认）：相机导演挂 **PlayerController** 层；技能镜头 **覆盖式为主 + 叠加接口**；默认态 **Tag 驱动 + 技能直推**。

## 一、功能总览

| 需求 | 目标行为 | 落点 |
|---|---|---|
| 默认第三人称跟随 | 角色居中偏上、弹簧臂、Look 输入旋转、FOV 可配 | `UHelloCameraMode_ThirdPerson` |
| 阻挡自适应 | 相机与角色之间有墙/柱时，相机沿视线方向自动拉近，障碍移除后平滑回位 | ThirdPerson 的穿透探测（§七） |
| 变化平稳 | 镜头切换（默认↔技能）、拉近回位、旋转滞后全部平滑插值，无跳变/翻转 | 栈混合（§八）+ 模式内滞后 |
| 技能镜头 | 技能激活时整体接管镜头（特写/固定机位/拉近），结束平滑回默认 | `UHelloCameraMode_Anchored` + Push/Pop（§九） |
| 叠加效果 | 轻技能/连段命中用 FOV 冲击 + 偏移，不整体接管 | `UHelloCameraModifier_Punch`（§十） |
| 换人交接 | 切人后相机导演不重建，读新角色默认镜头 Tag 自动过渡 | 相机管理器归 Controller（§四·1） |

## 二、核心架构

```
AHelloPlayerController
  ├─ PlayerCameraManagerClass = AHelloPlayerCameraManager   (构造时指定)
  │
AHelloPlayerCameraManager  (APlayerCameraManager，相机导演，Controller 拥有 → 换人不重建)
  ├─ UCameraModeConfigDataAsset* CameraModeConfig   (Tag → 镜头类 查找表)
  ├─ TArray<FHelloCameraModeStackEntry> CameraModeStack   (镜头栈：底=默认态，顶=技能态)
  │     ├─ [底] UHelloCameraMode_ThirdPerson    ← 由 DetermineCameraMode 按 Tag 维护
  │     └─ [顶] UHelloCameraMode_Anchored       ← 由 GA Push 进来
  ├─ UpdateViewTarget(FTViewTarget& OutVT) 每帧：确定模式 → 逐模式 UpdateView → EvaluateStack 合成 → 覆盖 OutVT.POV
  └─ 叠加：UCameraModifier 子类（Punch/Shake），引擎 ApplyCameraModifiers 自动叠加

AHelloCharacterBase
  └─ FGameplayTag CameraModeTag   (每个角色声明自己的默认镜头态，EditDefaultsOnly)
```

职责切分（照项目惯例：行为逻辑在代码、数值/映射在资产）：

- **相机管理器**：持有栈、合成、暴露 Push/Pop/叠加 API、按 Tag 决定默认态。**只做导演，不做数值**。
- **镜头模式（UObject，非 Component）**：纯计算，输入 Context（目标角色、控制旋转、锚点），输出 View（FOV/位置/朝向）。可配置、可组合、可继承扩展。
- **配置资产**：`Tag → TSubclassOf<UHelloCameraMode>` 映射，默认态由它查表。

## 三、核心链路

### 3.1 每帧相机更新链路

```
PlayerController::Tick
  → UpdateCameraManager(DeltaSeconds)      (DeltaSeconds 为真实帧时间，不受 CustomTimeDilation 影响)
  → APlayerCameraManager::DoUpdateCamera
      ① 初始化 OutVT 为当前 ViewTarget / CameraCache.POV
      ② AHelloPlayerCameraManager::UpdateViewTarget(OutVT, DeltaTime)
          - ViewTarget 变化检测 → DetermineBaseCameraMode() 保证栈底默认态与角色 CameraModeTag 一致
          - CleanFinishedModes() 移除已完全淡出的瞬态模式
          - Super::UpdateViewTarget(OutVT, DeltaTime) 做引擎默认处理（含 modifiers 之前的基准 POV）
          - EvaluateCameraStack(OutVT, DeltaTime) 逐 mode UpdateView → 加权混合 → 覆盖 OutVT.POV
      ③ CameraCache.POV = OutVT.POV
  → ApplyCameraModifiers（Punch/Shake 叠加）→ 输出最终画面
```

### 3.2 技能镜头 Push/Pop 链路

```
GA::ActivateAbility
  → CameraManager->PushCameraMode(UHelloCameraMode_Anchored, Config{锚点/偏移/FOV}, BlendIn, BlendOut)
      返回 FHelloCameraModeHandle
  → 动画播放期间相机从默认第三人称平滑混合到技能机位

GA::EndAbility（含被打断路径，统一兜底）
  → CameraManager->PopCameraMode(Handle)   (或 PopAllTransientModes)
  → 平滑混合回默认第三人称
```

## 四、关键设计决策

1. **相机导演放 PlayerController（APlayerCameraManager 子类），不放角色**：项目有换人小队，`Possess` 会重建角色的组件、打断角色侧逻辑；而 `APlayerCameraManager` 归 Controller 所有、跨 Possess 常驻，天然满足"换人无缝交接"。这是与 Lyra（相机组件放 Pawn）最大的偏离点，也是本项目的必要改造。
2. **镜头模式做成 UObject（非 Component、非 Actor）**：模式是"纯计算单元"，一个模式实例不占场景、可任意实例化/销毁（技能镜头每激活一次就一个实例）。`UHelloCameraMode` 基类 + 栈管理，与 Lyra 同构。
3. **栈模型（默认态在底、技能态在顶）**：默认第三人称永远垫底（weight 恒定），技能镜头 push 到顶并淡入/淡出。这样技能结束只需 Pop，天然回到默认态，无需额外"记住上一个模式"。
4. **默认态 Tag 驱动、技能直推（混合触发）**：默认态属于"角色身份/战斗状态"，用 `CameraModeTag` + 配置表表达（换人自动切）；技能镜头属于"瞬态演出"，用 GA 显式 Push（带 BlendIn/Out），生命周期与 GA 严格绑定。
5. **穿透规避用 `ECC_Camera` 碰撞通道做探测，而非 Unity 式射线重算弹簧臂**：保持"理想机位 → 探测 → 拉近"三层结构，拉近量用独立 BlendIn/Out 平滑（§七），避免逐帧硬跳。
6. **叠加效果走引擎原生 `UCameraModifier`**：`ApplyCameraModifiers` 在 `DoUpdateCamera` 之后自动执行，Punch/Shake 不用侵入合成逻辑；多个叠加可并存（`bExclusive=false`）。

## 五、文件清单

| 文件 | 职责 |
|---|---|
| `Public/Camera/HelloCameraMode.h/.cpp` | `UHelloCameraMode` 抽象基类：View 结构、混合参数、`UpdateView`/`UpdateBlending` 虚方法 |
| `Public/Camera/HelloCameraMode_ThirdPerson.h/.cpp` | 默认第三人称：弹簧臂、位置/旋转滞后、**穿透规避** |
| `Public/Camera/HelloCameraMode_Anchored.h/.cpp` | 技能镜头：锚定到 Socket/相对偏移、可选看向角色、可选 Hold 时长 |
| `Public/Camera/HelloPlayerCameraManager.h/.cpp` | `AHelloPlayerCameraManager`：栈管理、Push/Pop/PopAllTransient、DetermineCameraMode、合成、叠加 API |
| `Public/DataAsset/CameraModeConfigDataAsset.h/.cpp` | `Tag → TSubclassOf<UHelloCameraMode>` 查找表（照 ComboConfigDataAsset 风格） |
| `Public/Camera/HelloCameraModifier_Punch.h/.cpp` | `UCameraModifier` 子类：FOV 冲击 + 偏移 + 衰减 |
| `HelloPlayerController.h/.cpp` | 构造函数指定 `PlayerCameraManagerClass` |
| `HelloCharacterBase.h/.cpp` | 新增 `CameraModeTag`（EditDefaultsOnly） |
| `HelloCharacter.h/.cpp` | 退役 `SpringArm`/`FollowCamera`（移除或停用，画面改由相机管理器出） |
| `HelloGameplayTags.h/.cpp` | 新增 `Camera.Mode.ThirdPerson` 等 Native Tag |

Build.cs 无需改动：`UCameraComponent`/`APlayerCameraManager`/`UCameraModifier`/`ECC_Camera` 均在 `Engine` 模块。

## 六、镜头模式设计

### 6.1 基类 `UHelloCameraMode`（UObject，Abstract）

```cpp
USTRUCT(BlueprintType)
struct FHelloCameraModeView
{
    float FieldOfView = 80.f;
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
};

struct FHelloCameraContext
{
    AActor*  ViewTarget = nullptr;     // 当前 Possess 角色
    FRotator ControlRotation;          // 控制器 Look 输入后的控制旋转
    FVector  TargetLocation;           // 角色位置（胶囊中心）
    FVector  AnchorLocation;           // 相机锚点（Mesh 根或头顶 Socket）世界位置
    FRotator AnchorRotation;
};

UCLASS(Abstract)
class UHelloCameraMode : public UObject
{
    // 混合：淡入/淡出
    UPROPERTY(EditDefaultsOnly) float BlendTime     = 0.0f;  // 淡入时长
    UPROPERTY(EditDefaultsOnly) float BlendOutTime  = 0.0f;  // 淡出时长（Pop 后继续淡出再移除）
    UPROPERTY(EditDefaultsOnly) float BlendExponent = 4.0f;  // 混合曲线指数（越大后程越陡）

    // 平滑（模式内）
    UPROPERTY(EditDefaultsOnly) float ViewSmoothSpeed     = 6.0f;
    UPROPERTY(EditDefaultsOnly) float RotationSmoothSpeed = 10.0f;

public:
    virtual void UpdateView(float DeltaTime, const FHelloCameraContext& Ctx, FHelloCameraModeView& OutView) {}
    void UpdateBlending(float DeltaTime);   // 推进 BlendAlpha（淡入 0→1 / 淡出 1→0）
    float GetBlendWeight() const;           // = Pow(Clamp(Alpha,0,1), BlendExponent)
};
```

### 6.2 默认第三人称 `UHelloCameraMode_ThirdPerson`

| 参数 | 含义 | 默认 |
|---|---|---|
| `TargetOffset` | 锚点相对角色胶囊中心的偏移 | (0, 0, 90) |
| `ArmLength` | 弹簧臂长（替代原 SpringArm 的 400） | 400 |
| `bEnableCameraLag` / `CameraLagSpeed` | 位置滞后开关/速度 | true / 8 |
| `bEnableRotationLag` / `CameraRotationLagSpeed` | 旋转滞后开关/速度 | true / 12 |
| `FieldOfView` | 基础 FOV | 80 |

`UpdateView` 逻辑：

```
DesiredCam = AnchorLocation + TargetOffset - ArmLength * CamForward(ControlRotation)
ActualCam  = FInterpTo(上一帧 ActualCam, DesiredCam, DeltaTime, CameraLagSpeed)
// 穿透规避（见 §七）会再对 DesiredCam 做拉近，得到 PenetratedCam 后再做滞后
Rotation   = FInterpTo(上一帧 Rotation, ControlRotation, DeltaTime, RotationLagSpeed)
```

### 6.3 技能镜头 `UHelloCameraMode_Anchored`

运行时由 GA 通过 Config 传入（非 EditDefaultsOnly 资产）：

```cpp
struct FHelloCameraModeConfig
{
    FName AnchorSocket;        // 角色 Mesh 上的 Socket（动画驱动，如 "Camera_Skill"）；空 = 用角色根
    FTransform AnchorOffset;   // 相对 Socket/根的偏移
    bool bLookAtCharacter;     // 是否强制看向角色
    float FieldOfView;         // 覆盖 FOV（0 = 用基类默认）
    float HoldDuration;        // 镜头保持时长（>0 到期自动 Pop；0 = 由 GA 控制结束）
};
```

`UpdateView`：`Camera = SocketTransform * AnchorOffset`，`bLookAtCharacter` 时朝向角色；可加位置/旋转平滑。

## 七、阻挡规避（自适应阻挡）

在 `UHelloCameraMode_ThirdPerson::UpdateView` 内，用独立于位置滞后的"穿透系数"处理：

```
1. 理想点 Ideal = Anchor + TargetOffset - ArmLength * CamForward
2. 探测：从 Anchor（+防夹安全区 SafeZoneRadius）朝 Ideal 方向做 SphereTrace/LineTrace
     - TraceChannel = ECC_Camera
     - IgnoreSelf（忽略 ViewTarget 角色及其 Mesh/Capsule）
     - 命中 Hit → 拉近点 Pulled = Hit.Location（沿视线方向留 SafeZone 余量）
     - 未命中   → Pulled = Ideal
3. 穿透系数 Penetration = FInterpTo(Penetration, 是否被挡 ? 1 : 0, DeltaTime,
                                   被挡 ? 1/PenetrationBlendInTime : 1/PenetrationBlendOutTime)
4. 实际目标 = Lerp(Ideal, Pulled, Penetration)
5. 再叠加位置滞后（§6.2）
```

关键参数：

| 参数 | 含义 | 默认 |
|---|---|---|
| `PenetrationBlendInTime` | 进入阻挡时拉近的平滑时长（小→反应快） | 0.1 |
| `PenetrationBlendOutTime` | 障碍移除时回位的平滑时长（大→回位柔和） | 0.25 |
| `SafeZoneRadius` | 相机距障碍表面的保留距离 | 30 |
| `TraceRadius` | 探测半径（比线更抗穿缝） | 10 |

## 八、平滑混合（变化平稳）

栈合成 `EvaluateStack`：

```
1. 逐 mode 更新 BlendAlpha（淡入/淡出）后取 Weight_i = Pow(Alpha_i, BlendExponent_i)
2. 归一化 W_i = Weight_i / ΣWeight
3. FOV      = Σ W_i * FOV_i                          (线性插值)
4. Location = Σ W_i * Location_i                     (线性插值)
5. Rotation = 从栈底到栈顶做 FQuat::Slerp 链（短路径），Pitch 钳制 [-89, 89]
```

要点：

- **旋转必须用 Slerp/短路径**，禁止对 `FRotator` 三轴分别 `FMath::Lerp`——跨 0°/360° 或 ±180° 边界会疯狂翻转（本类系统第一大坑）。
- **栈底默认态权重恒为 1**：淡入/淡出只发生在"默认态切换"或"技能 Push/Pop"期间；技能 Pop 后其 alpha 从 1 淡出到 0 才从栈移除，保证回默认态也是平滑过渡。
- 双平滑叠加：栈级（模式间切换）+ 模式级（ThirdPerson 内位置/旋转滞后 + 穿透 BlendIn/Out）。前者负责"换镜头"，后者负责"跟随手感"。
- **模式级平滑的提交约定**：`UHelloCameraMode::CommitView` 由相机管理器在每帧 `UpdateView` 之后、栈合成之前调用，写入 `LastView`/`CurrentView`。平滑是否可用只看显式标志 `bHasLastView`，**禁止用 `IsZero`/`IsNearlyZero` 判断"未初始化"**——零是合法位置/朝向，恰好位于原点或 yaw=0 的相机会被误判而硬切。提交的必须是模式自身输出，不能是合成后的 POV，否则低权重模式会吃进其他模式的画面，形成反馈式拖拽。平滑系数统一用 `1 - e^(-Speed·Dt)`（Frame-Rate Independent），保证不同帧率下手感一致。

## 九、技能镜头接入（GA 侧）

给 `UHelloGA`（或直接在各 GA 内）封装两个辅助接口，所有技能 GA 照抄：

```cpp
// ActivateAbility 内（读完 Config 后）
CameraHandle = CameraMgr->PushCameraMode(
    UHelloCameraMode_Anchored::StaticClass(),
    FHelloCameraModeConfig{ Socket, Offset, bLookAt, FOV, HoldDuration },
    BlendIn, BlendOut);

// EndAbility 内（无论正常结束还是被打断，统一兜底）
CameraMgr->PopCameraMode(CameraHandle);   // 幂等，双 Pop 安全
// 保险：EndAbility 末尾 CameraMgr->PopAllTransientModes() 清理任何泄漏的技能镜头
```

取相机管理器：`GetCurrentActorInfo()->AvatarActor->GetInstigatorController()` 或 `GetActorInfo().PlayerController` → `PlayerCameraManager`。封装成 `AHelloPlayerCameraManager::Get(WorldContext)` 静态助手（照 `GetComboCountTag` 的辅助函数风格）。

生命周期约定：

- 技能镜头是**瞬态（Transient）**模式，`PopAllTransientModes()` 只清瞬态、不动栈底默认态。
- GA 被打断（受击/更高优先级能力/闪避）也必须走 EndAbility → Pop，否则镜头卡死在技能机位。**防重入**：`PopCameraMode(Handle)` 内部校验 handle 的 Serial/有效性，双调无害（对照连段记忆 GE 的防重入经验）。

## 十、叠加接口（Punch/Shake）

`UHelloCameraModifier_Punch : UCameraModifier`：`Trigger(FOVDelta, FVector2D Offset, Duration)` 追加一次冲击；modifier 内部维护一个冲击列表，多次触发各自独立衰减、互不覆盖。`ModifyCamera` 里把各冲击的 FOV 与屏幕偏移按指数衰减累加到 `OutPOV`，列表清空后由管理器 `DisableModifier()`。`bExclusive=false`，可与其他修饰器并存。

**关键前提**：引擎的 `APlayerCameraManager::UpdateViewTarget` 是在 `UpdateViewTargetInternal` 之后才调用 `ApplyCameraModifiers` 的。相机栈的合成写在 `UpdateViewTargetInternal` 覆盖里（而不是 `UpdateViewTarget` 末尾），修饰器才会叠加在合成结果之上；否则冲击会被栈覆盖掉。

相机管理器上暴露 `AddCameraPunch(...)`（内部复用同一个 modifier + 追加冲击），GA 一行调用；`UHelloGA` 再包一层 `TriggerCameraPunch`。

## 十一、编辑器配置

1. `UCameraModeConfigDataAsset`（照 `ComboConfigDataAsset`）：新增 `DA_CameraModeConfig` 资产，配一条 `Camera.Mode.ThirdPerson → UHelloCameraMode_ThirdPerson`。
2. 角色蓝图 `BP_PlayerCharacter` Class Defaults → Camera 分类：`CameraModeTag = Camera.Mode.ThirdPerson`（各角色/敌人可配不同默认态）。
3. `AHelloPlayerController` 构造函数指定 `PlayerCameraManagerClass`，无需蓝图操作。
4. 技能镜头锚点：在角色 Mesh 上加 `Camera_Skill` 等 Socket（编辑器或 DCC），或直接用品化后绝对偏移。
5. 退役 `AHelloCharacter` 的 `CameraBoom`/`FollowCamera`：删除或 `bIsActive=false`。若保留做编辑器预览，运行时相机管理器接管后二者不应再参与 `CalcCamera`。

## 十二、避坑（提前声明）

1. **旋转混合翻转**：栈合成旋转必须 Slerp/短路径，Pitch 钳制；三轴分别 Lerp 会跨边界翻转（§八）。
2. **Possess 首帧 ViewTarget 为空**：`DetermineCameraMode`/`UpdateView` 前置 null 守卫，读不到角色就用上一个默认态撑过第一帧，下一帧再对齐。
3. **相机探测误命中自身**：探测必须 `IgnoreSelf`/忽略 ViewTarget 的 Capsule+Mesh，否则每帧必"被挡"、相机永远贴脸。
4. **技能镜头泄漏**：GA 被打断未 Pop → 镜头卡死；EndAbility 双保险（Pop 句柄 + PopAllTransientModes）。
5. **相机管理器归属**：必须在 Controller 创建、跨 Possess 常驻；误放到角色会被换人销毁重建，丢失过渡连续性。
6. **CustomTimeDilation 交互**：相机管理器 Tick 用真实 DeltaTime，完美闪避慢放时角色慢、相机平滑不慢——若视觉脱节，调小 ThirdPerson 的 `CameraLagSpeed`/`PenetrationBlendInTime`。
7. **FOV 跳变**：模式切换的 FOV 也走栈混合，模式内再做 `FInterpTo`，避免切技能瞬间 FOV 突跳。
8. **探测通道**：统一 `ECC_Camera`；项目现有武器碰撞会把 Mesh 改成 Pawn/Overlap，注意相机探测与武器碰撞各自独立设置，勿复用同一通道配置。

## 十三、验收标准（PIE 日志/目测）

1. 第三人称跟随正常，Look 输入旋转正常，FOV 配置生效。
2. 角色靠墙/柱子，相机沿视线平滑拉近；离开后平滑回位，无跳变。
3. `CameraModeTag` 切换（换人）时镜头平滑过渡，不瞬移、不翻转。
4. 技能激活 → 镜头平滑推到特写机位；技能结束/被打断 → 平滑回默认，无残留。
5. 连段命中触发 FOV Punch，叠加不互相覆盖、可衰减归零。
6. 反复 Push/Pop（快速连放技能、闪避打断技能）无镜头卡死、无模式泄漏（栈大小回归 1）。

## 十四、分阶段实施任务

- [x] **阶段 1 骨架**：`UHelloCameraMode` 基类 + `UHelloCameraMode_ThirdPerson`（无穿透）+ 栈 + `AHelloPlayerCameraManager` + `CameraModeConfigDataAsset` + 接入 PlayerController + 退役 SpringArm。验收：跟随/旋转/FOV 正常。
- [x] **阶段 2 平滑混合（核心已实现）**：栈混合（Slerp 旋转、FOV/位置加权）+ ThirdPerson 位置/旋转滞后 + 默认态切换过渡。已随阶段 1 一并验证：移动/转向滞后、换人/Tag 切换平滑。
- [x] **阶段 3 阻挡规避**：`ECC_Camera` 探测 + 穿透系数 + BlendIn/Out（2026-10-25 实现，见 §十八）。验收：靠墙拉近回位平滑。
- [ ] **阶段 4 技能镜头 + 叠加**：`UHelloCameraMode_Anchored` + Push/Pop API + GA 接入 + `UHelloCameraModifier_Punch`。验收：技能镜头 + 叠加全链路。
  - [x] 第一刀（2026-10-25）：`PushCameraMode` 支持传入 BlendIn/BlendOut；Anchored 实现 Socket 锚定、LookAt、默认 FOV、HoldDuration 到期自动淡出（见 §十九）。
  - [x] 第二刀（2026-10-25）：`UHelloGA` 封装 Push/Pop/Punch；Punch 修饰器改为多冲击列表；修饰器改为在栈合成之后叠加（见 §二十）。
  - [x] 第三刀（2026-10-25）：镜头冲击与技能镜头改为**动画通知**配置（`AnimNotify_HelloCameraPunch` / `AnimNotifyState_HelloSkillCamera`）；撤销 ComboConfig 的按段 `CameraPunch`；换人时立即清理瞬态镜头（见 §二十一）。

每阶段编译 + PIE 日志验收（照连段/闪避系统的既有流程）。

## 十五、阶段 1 改动清单

### 新增文件

| 文件 | 职责 |
|---|---|
| `Public/Camera/HelloCameraMode.h` / `Private/Camera/HelloCameraMode.cpp` | 镜头模式 UObject 基类：`FHelloCameraModeView`、`FHelloCameraContext`、淡入淡出、Slerp/短路径平滑辅助 |
| `Public/Camera/HelloCameraMode_ThirdPerson.h` / `Private/Camera/HelloCameraMode_ThirdPerson.cpp` | 默认第三人称：弹簧臂 + 位置/旋转滞后（阶段 1 无穿透规避） |
| `Public/Camera/HelloCameraMode_Anchored.h` / `Private/Camera/HelloCameraMode_Anchored.cpp` | 技能镜头占位：运行时配置 + 锚定逻辑（阶段 1 简化实现） |
| `Public/Camera/HelloPlayerCameraManager.h` / `Private/Camera/HelloPlayerCameraManager.cpp` | 相机导演：`APlayerCameraManager` 子类，栈管理、Push/Pop、DetermineBaseCameraMode、EvaluateStack；覆写 `UpdateViewTarget` 并写入 `OutVT.POV` |
| `Public/Camera/HelloCameraModifier_Punch.h` / `Private/Camera/HelloCameraModifier_Punch.cpp` | FOV 冲击/屏幕偏移叠加占位（阶段 4 完善） |
| `Public/DataAsset/CameraModeConfigDataAsset.h` / `Private/DataAsset/CameraModeConfigDataAsset.cpp` | Tag → 镜头模式类 映射资产 |

### 修改文件

| 文件 | 改动 |
|---|---|
| `Public/PlayerController/HelloPlayerController.h` | 声明 `AHelloPlayerController()` 构造函数 |
| `Private/PlayerController/HelloPlayerController.cpp` | 构造函数里 `PlayerCameraManagerClass = AHelloPlayerCameraManager::StaticClass()` |
| `Public/Character/HelloCharacterBase.h` | 加 `#include "GameplayTagContainer.h"`、新增 `CameraModeTag` 与 `GetCameraModeTag()` |
| `HelloCharacter.h` / `HelloCharacter.cpp` | 删除 `CameraBoom`/`FollowCamera` 成员与构造逻辑，角色不再自持相机 |
| `Public/GameplayTags/HelloGameplayTags.h` | 新增 `Camera_Mode_ThirdPerson` Native Tag 声明 |
| `Private/GameplayTags/HelloGameplayTags.cpp` | 定义 `Camera.Mode.ThirdPerson` Tag |

### 未改动

- `Hello.Build.cs`：相机相关类全在 `Engine` 模块，无需加依赖。

## 十六、阶段 1 调试方法

### 1. 编译

直接在 UE 编辑器或 Rider/VS 里编译 `Hello` 模块。常见编译错误点：

- `FGameplayTag` 未找到 → 检查 `HelloCharacterBase.h` 是否 include 了 `GameplayTagContainer.h`。
- `AHelloPlayerCameraManager` 未找到 → 检查 `HelloPlayerController.cpp` 是否 include 了 `Camera/HelloPlayerCameraManager.h`。
- `UHelloCameraMode` 抽象类无法实例化 → `UCLASS(Abstract)` 正确，相机管理器用 `NewObject(this, ModeClass)` 按具体子类创建。
- 蓝图编译报错（SpringArm/Camera 引用丢失）→ `AHelloCharacter` 已删除这两个组件，需同步打开 `BP_PlayerCharacter` 清理相关引用，或重新生成 C++ 父类。

### 2. 编辑器配置（必须做）

1. **创建配置资产**：Content Browser → 右键 → `Miscellaneous` → `Data Asset` → 选 `CameraModeConfigDataAsset`，命名为 `DA_CameraModeConfig`。
2. **填映射表**：在 `DA_CameraModeConfig` 的 `Camera Mode Entries` 数组里加一条：
   - `Camera Tag` = `Camera.Mode.ThirdPerson`
   - `Camera Mode Class` = `UHelloCameraMode_ThirdPerson`
3. **给相机管理器配资产**：
   - 找到项目自动生成的 `BP_HelloPlayerCameraManager`（或基于 `AHelloPlayerCameraManager` 新建一个 Blueprint 子类，命名为 `BP_HelloPlayerCameraManager`）。
   - 在 Class Defaults 的 `Camera` 分类里，`Camera Mode Config` = `DA_CameraModeConfig`。
   - **关键**：World Settings → `PlayerCameraManagerClass`（或在 GameMode 里）改为 `BP_HelloPlayerCameraManager`，确保实际生效的是带配置资产的子类。
   - 更简单的做法：在 `AHelloPlayerCameraManager` 构造里直接硬编码 `CameraModeConfig` 的引用路径，但阶段 1 先走资产配置。
4. **给角色配 Tag**：打开 `BP_PlayerCharacter` → Class Defaults → `Camera` 分类 → `Camera Mode Tag` = `Camera.Mode.ThirdPerson`。
5. **清理旧 SpringArm/Camera 引用**：如果 `BP_PlayerCharacter` 之前依赖 `CameraBoom`/`FollowCamera` 做蓝图绑定，打开 Component 面板删除或忽略这些引用。

### 3. PIE 运行验证

- 进入 PIE，确认画面是从角色后上方跟随（类似原 SpringArm 400 距离）。
- 移动鼠标（Look 输入），画面应绕角色旋转。
- 打开控制台：`log LogTemp log`（或更高 verbosity），看 Output Log 是否有 `[HelloCamera] Base mode initialized: UHelloCameraMode_ThirdPerson` 和每帧 `[HelloCamera] Eval ...`。
- 若画面黑屏/卡在角色模型中/看不到滞后：
  - 检查 `BP_HelloPlayerCameraManager` 是否真正被使用（World Settings / GameMode）。
  - 检查 `DA_CameraModeConfig` 里 Tag 与角色 `CameraModeTag` 是否一致；若 `CameraModeConfig` 未配置，栈为空，会回退到引擎默认 POV（可能位于胶囊中心）。
  - 检查 `AHelloPlayerCameraManager::UpdateViewTarget` 是否调用了 `Super::UpdateViewTarget` 后再调用 `EvaluateCameraStack(OutVT, DeltaTime)`，并且 `EvaluateCameraStack` 写入了 `OutVT.POV`（不是 `ViewTarget.POV`，也不是 `CameraCache.POV`）。
  - 快速晃鼠标：若 `[HelloCamera] Eval` 日志中的 `Rot` 平滑变化而不是瞬间跳到控制旋转，说明旋转滞后生效。

### 4. 换人验证（可选但推荐）

- 按换人键切到队友，观察画面是否直接跟随新角色、无黑屏/重建。
- Output Log 应出现 `[HelloCamera] Base mode switched to ...`（如果两人 Tag 相同则不会触发，属正常）。

## 十七、阶段 1/2 已验证功能记录

以下功能已在 PIE 中验证通过（2026-10-15）：

1. **第三人称跟随**：相机位于角色后上方，距离由 `ArmLength` 控制（默认 400），`Look` 输入可绕角色旋转，`FieldOfView` 配置生效。
2. **位置滞后**：角色奔跑或急停时，相机不会瞬间贴住角色，而是平滑追赶并轻微过冲回位；`CameraLagSpeed` 越小滞后越明显。
3. **旋转滞后**：快速甩鼠标后，画面旋转平滑过渡，无跳变/翻转；由 `CameraRotationLagSpeed` 控制。
4. **栈合成**：`EvaluateCameraStack` 按混合权重对 FOV、位置做 Lerp，对旋转做 `FQuat::Slerp` 短路径混合，Pitch 钳制在 `[-89.9, 89.9]`。
5. **Tag 驱动默认态**：`AHelloCharacterBase::CameraModeTag` 通过 `DA_CameraModeConfig` 解析为 `UHelloCameraMode_ThirdPerson`。
6. **换人交接**：切人后 `AHelloPlayerCameraManager` 不重建，按新角色 `CameraModeTag` 重建/切换栈底默认态，画面直接跟随新角色。
7. **角色不再自持相机**：`AHelloCharacter` 已移除 `CameraBoom`/`FollowCamera`，避免与 Controller 层相机冲突。

**已实现但未验证/未开始的功能**

- `UHelloCameraMode_Anchored` 已实现 Socket 锚定、LookAt、HoldDuration 到期自动淡出（见 §十九），尚未接真实 GA 验证。
- `UHelloCameraModifier_Punch` 已实现多冲击叠加，并在连段命中处触发（见 §二十）。
- 阻挡规避已在阶段 3 补齐（见 §十八），待 PIE 验收。

### 5. 后续阶段入口

阶段 3 已实现（见 §十八），PIE 验收通过后进入阶段 4：

- 阶段 4：完善 `UHelloCameraMode_Anchored`（Socket 解析、LookAt）、给 GA 加 Push/Pop、给 `AHelloPlayerCameraManager` 加 `AddCameraPunch` 接口。

## 十八、阶段 3 改动记录（阻挡规避，2026-10-25）

### 1. 改了什么

只改 `HelloCameraMode_ThirdPerson.h/.cpp`，相机管理器与栈合成零改动。

- 新增私有方法 `ResolvePenetration(Ctx, FocusLocation, IdealLocation, DeltaTime)`：探测 + 拉近 + 穿透系数平滑，返回"已含穿透系数、但尚未做位置滞后"的目标点。
- 新增参数（`Camera|ThirdPerson|Penetration` 分类）：`bEnablePenetrationAvoidance`、`PenetrationBlendInTime=0.1`、`PenetrationBlendOutTime=0.25`、`SafeZoneRadius=30`、`TraceRadius=10`。
- 新增状态 `PenetrationAlpha`（0=未受阻，1=完全拉近）。
- `UpdateView` 顺序变为：关注点 → 理想机位 → **阻挡规避** → 位置滞后 → 旋转滞后 → 输出。
- `AHelloPlayerCameraManager` 新增 `UpdateCharacterFade()`：相机距角色中心小于 `HideCharacterDistance`（默认 90）时把目标淡出系数设为 0，退回 `ShowCharacterDistance`（默认 130）以上再设回 1；两个阈值形成迟滞防闪烁，系数按 `FadeOutDuration`（0.15s）/`FadeInDuration`（0.25s）线性推进，换人时先恢复上一位角色。只在纯第三人称时生效（栈底是 ThirdPerson 且没有技能镜头在混合），技能特写镜头不受影响。
- `AHelloCharacterBase::SetCameraProximityFade(Alpha)` 负责把系数落到材质：按需为每个材质槽创建/缓存 `UMaterialInstanceDynamic`，写入标量参数 `CameraFadeParameterName`（默认 `Fade`）；系数到 0 才 `SetVisibility(false)` 省渲染，恢复时先开可见性再写参数。

实现要点（与 §七 的偏差与原因）：

1. **探测起点用"关注点"（`AnchorLocation + TargetOffset`），不是原始 `AnchorLocation`**。`Ctx.AnchorLocation` 取的是 Mesh 组件原点（通常在脚底），从脚底往相机位做球扫会撞地面，导致相机永远贴脸。§七 里写的 "Anchor" 按此理解。关注点位于角色胶囊内部，依赖 `AddIgnoredActor(ViewTarget)` 才不会自挡。
2. **探测用 `SweepSingleByChannel` + `FCollisionShape::MakeSphere(TraceRadius)`，通道 `ECC_Camera`**，`FCollisionQueryParams` 只忽略当前 ViewTarget。命中距离取**表面接触点 `Hit.ImpactPoint`** 在视线上的投影，再**减 `SafeZoneRadius`** 把相机拉回角色侧（球扫的 `Hit.Location` 是球心、会在表面前 `TraceRadius` 处停下，直接拿它并加余量会把相机推到墙后），最终 `Clamp` 到 `[0, 弹簧臂长]`。
3. **穿透系数用帧率无关指数曲线** `Alpha = 1 - e^(-Dt/BlendTime)` 逼近 0/1，`BlendIn/OutTime` 作为时间常数；不再用 `FInterpTo` 的 "Speed = 1/时间" 拼法。首次帧 / 模式重建时（`!HasValidLastView()`）直接对齐当前状态，避免从 0 起步产生一次假回位。
4. **拉近结果喂给 `DesiredLocation` 后仍走基类 `SmoothLocation`**，没有引入自有滞后状态（保持 2026-10-23 的平滑契约）。代价是"穿透 Blend + 位置滞后"两层串联，被墙顶住时可能穿模 2~3 帧。
5. 关闭 `bEnablePenetrationAvoidance` 即回到阶段 1/2 行为，便于 A/B 对比。
6. 通道现状：`DefaultEngine.ini` 没有自定义 Camera 通道，走引擎默认 `ECC_Camera`；角色的 Capsule 是 `Pawn` profile（默认忽略 Camera），所以不会被队友/敌人胶囊误挡。静态网格、道具、地形按各自 Profile 正常阻挡（默认 BlockAll 类会 Block Camera）。若出现"队友站身后就把相机拉近"的观感问题，在 `Params` 里追加忽略其他 `APawn` 即可。

### 2. 淡出所需的材质配置（一次性）

淡出参数是通过材质生效的，角色材质需要做一次配置，否则看不到渐变：

1. 材质 Blend Mode 设为 **Masked**。
2. 勾选材质属性里的 **Dither Opacity Mask**（或使用 `DitherTemporalAA` 节点输出到 Opacity Mask），让半透明通过抖动实现，避免 Masked 的硬阈值。
3. 新建 Scalar Parameter，名字与 `AHelloCharacterBase::CameraFadeParameterName` 一致（默认 `Fade`），**默认值 1**，接到 Opacity Mask。
4. 若角色有多个材质槽，每个槽各自的材质都要加这个参数；没加的槽不会淡出。
5. **`Opacity Mask Clip Value` 保持默认 0.333**。实测（2026-10-25，MatLayerBlend 材质）：把它设成 0 会让淡出只在 0.45 以下才出现、0.5 与 1 无差别；恢复默认值后 0.5 有正常半透明且全程有效。原因是 UE 的 dithered opacity mask 阈值与 Clip Value 相关，Clip=0 会压缩抖动窗口，不要按"Clip 归零就能线性映射"的直觉去改。

未配置材质不会报错也不会崩，只是写参数无效，表现为"到系数 0 时瞬间隐藏"——即回到加淡出之前的行为，可据此判断材质是否配对。

### 3. 预期效果

- 角色背靠墙 / 柱子 / 树时，相机沿视线方向快速拉近（约 0.1s 时间常数），画面保持能看清角色；离开障碍后约 0.25s 柔和回位到 `ArmLength`。
- 探测是球体而非射线，能盖住单根柱子边缘的"穿缝"，相机不会从柱子和墙之间的缝隙里穿出去。
- 拉近/回位全程无硬跳；换人、Tag 切换时新建的模式实例直接对齐，不产生回位抖动。
- 探测器不作用于旋转，`Look` 输入手感与阶段 2 完全一致。
- 背贴墙时相机被压缩到角色附近（几何上无法既不进角色又不进墙），此时角色在 0.15s 内平滑淡出，画面接近第一人称；离开墙面超过 `ShowCharacterDistance` 后角色在 0.25s 内淡入恢复。

### 4. 验收检查清单（PIE）

| # | 操作 | 期望 |
|---|---|---|
| 1 | 把角色贴到墙边并背对墙（相机在墙里） | 相机平滑拉近，角色在 0.15s 内淡出到不可见，不出现看到模型内部/穿墙；离开后 0.25s 淡入恢复 |
| 2 | 侧向跑过一根柱子，让柱子在相机与角色之间扫过 | 相机拉近，柱子移开后柔和回位，无一次拉近/回位抖动 |
| 3 | 面对开阔场地绕圈 | 相机距离恒为 `ArmLength`（400），行为与阶段 2 无差别 |
| 4 | 角色原地快速转身 360° | 相机绕角色旋转，无异常拉近/回位抽动（探测起点在胶囊内，不应自挡） |
| 5 | 在墙角卡住并快速甩鼠标 | 无反复横跳/穿模闪烁；若出现，调大 `PenetrationBlendOutTime` 或 `SafeZoneRadius` |
| 6 | 蹲伏/上楼/下坡（如果有关卡） | 相机不因地面被误判为障碍而贴脸 |
| 7 | 换人（两名角色贴近墙） | 切换后相机正确跟随新角色并拉近，无残留旧角色的拉近距离，旧角色恢复显示 |
| 8 | 手持武器贴墙 | 相机不被自己的武器阻挡（若被挡：把武器 Actor 加入 `Params.AddIgnoredActor`，或确认武器碰撞不 Block `ECC_Camera`） |
| 9 | 贴墙后在 90~130 距离区间来回微调 | 角色不闪烁也不来回渐变（迟滞生效：近于 90 才淡出，远于 130 才淡入） |
| 10 | 贴墙时释放技能（若已接技能镜头） | 技能特写镜头里角色正常显示（淡出只在纯第三人称生效） |
| 11 | 未配置 Fade 参数的角色贴墙 | 不报错，表现为到 0 时瞬间隐藏（退化行为，用于确认材质配置是否生效） |

排查提示：`ECC_Camera` 在项目里是否被正确使用，可在 World Settings 的 Collision 面板确认墙/柱用的是 Block `Camera` 的 Profile；若墙对 Camera 是 Ignore，探测永不命中，表现与关闭该功能一致。

关于"贴墙时相机进角色体内"：这是几何限制而非实现错误。角色胶囊半径约 34，墙就在胶囊背面，`SafeZoneRadius` 再留 30，两者相减后相机只能停在距胸口约 4 个单位处——推远会进墙，拉近必进角色，没有合法的第三人称机位。因此阶段 3 采用"近距淡出角色"兜底（`HideCharacterDistance=90` / `ShowCharacterDistance=130` 迟滞 + `FadeOutDuration=0.15` / `FadeInDuration=0.25` 线性渐变）。淡出依赖材质 Dither 配置（见本节 §2）；未配置时退化为"到 0 时瞬间隐藏"。把相机抬到头顶的做法在贴墙场景无效，因为抬升不改变离墙距离。

## 十九、阶段 4 第一刀改动记录（Anchored + Push 参数，2026-10-25）

### 1. 改了什么

- `UHelloCameraMode` 基类新增 `virtual void TickMode(float DeltaTime)`，管理器在每帧 `UpdateBlending` 之前调用；默认空实现，用于模式自管状态（计时等）。
- `AHelloPlayerCameraManager::PushCameraMode` 新增 `InBlendTime` / `InBlendOutTime` 参数，`< 0` 时沿用模式类默认值（ThirdPerson 0.15/0.15，Anchored 0.25/0.35）；GA/蓝图可直接指定过渡时长。
- `UHelloCameraMode_Anchored`：
  - 锚点真正使用 `Config.AnchorSocket`：`Mesh->GetSocketTransform(Socket, RTS_World)`；为空或不存在时依次回退 `DefaultAnchorSocket` → Mesh 组件变换 → Context 锚点，Socket 缺失只 Warning 一次。
  - 偏移语义改为 **乘法组合** `SocketTransform * AnchorOffset`（旧实现是世界空间加减，同一组数值的观感会不同：现在偏移和旋转都相对 Socket）。
  - `bLookAtCharacter` 真正实现：优先 `Config.LookAtSocket`，其次 `DefaultLookAtSocket`（默认 `head`），都不存在时回退到锚点上方 90；朝向计算后走基类 `SmoothRotation`（Slerp 短路径 + Pitch 钳制）。
  - 配置 `FieldOfView <= 0` 时用模式默认 `FieldOfView`（新增，默认 80），不再硬编码。
  - 删除 `IsExpired`，改为 `TickMode` 自计时：`HoldDuration > 0` 且到时 → `StartBlendOut()`；`< = 0` 完全由 GA 控制。到期只触发一次，与 GA 手动 Pop 幂等共存。

### 2. 预期效果

- Push 后镜头从当前 POV 平滑过渡到 Socket 机位（栈级 BlendIn），模式内部再做一次位置/旋转平滑；Hold 到期自动淡出并回到栈底默认态，栈大小回归 1。
- Socket 绑定在骨骼上时，角色播动作镜头会跟着 Socket 走（这是预期，不是抖动）；若不想跟动作走，用 `bLookAtCharacter` + 世界固定机位的组合，或后续给 Anchored 加"锁定首帧变换"选项。
- 过渡时间是"栈混合 + 模式内平滑"两层叠加，观感上会比配置的 BlendTime 稍慢；嫌拖沓就调大 Anchored 的 `ViewSmoothSpeed`/`RotationSmoothSpeed`。

### 3. 调试方法（无需 GA）

- 蓝图里直接测：`Get Hello Camera Manager`（WorldContext 传 self）→ `PushCameraMode`，Mode Class 选 `HelloCameraMode_Anchored`，Config 填 `AnchorSocket` / `AnchorOffset` / `bLookAtCharacter` / `FOV` / `HoldDuration`，即可看到过渡与自动回位。
- 日志关键字：`[HelloCamera] PushCameraMode ...`、`[HelloCamera] Anchored hold expired after ...`、`[HelloCamera] PopCameraMode ...`；Socket 名拼错会有一条 Warning。
- 常见现象对照：
  - 镜头落在脚底/地面 → Socket 没解析到（看 Warning），或 `AnchorOffset` 的 Z 没抬高。
  - 朝向不对 → `AnchorOffset` 现在会叠加旋转，检查其 Rotation；或改用 `bLookAtCharacter`。
  - 镜头不跟动作 → Socket 绑错了骨骼（检查 `DoesSocketExist` 的 Socket 是不是 Mesh 上的）。
  - Hold 到期画面突跳 → 确认走的是淡出而不是被 `CleanFinishedModes` 立即删除；日志顺序应为 `Anchored hold expired` →（约 BlendOutTime 后）`Mode finished`。
  - 栈泄漏（技能连放后相机卡住）→ `IsFinished` 依赖 `BlendAlpha` 归零，检查 `BlendOutTime` 是否被传成 0。

## 二十、阶段 4 第二刀改动记录（GA 封装 + Punch，2026-10-25）

### 1. 改了什么

- **相机更新拆层**：`UpdateViewTarget` 只调用 `Super::UpdateViewTarget`（基类内部虚调 `UpdateViewTargetInternal`）并处理角色淡出；原先的栈逻辑整体移入 `UpdateViewTargetInternal` 覆盖。这样基类的 `ApplyCameraModifiers` 会在栈合成结果之上执行——之前它作用于被覆盖掉的默认 POV，导致任何修饰器都不生效。
- `UHelloCameraModifier_Punch`：单组参数改为冲击列表 `FHelloCameraPunchInstance`，`Trigger` 追加而非覆盖；`ModifyCamera` 倒序遍历累加各冲击、移除过期项；新增 `IsActive()`；构造函数设 `bExclusive=false`；新增可配的 `ScreenOffsetDegreesPerUnit`。
- `AHelloPlayerCameraManager`：新增 `AddCameraPunch(FOVDelta=-6, ScreenOffset, Duration=0.2)`，懒创建并复用同一 modifier；一帧结束后若已无存活冲击则 `DisableModifier()`。
- `UHelloGA`：新增 `PushSkillCamera` / `PopSkillCamera` / `TriggerCameraPunch` 与 `GetHelloCameraManager()`（优先取 `ActorInfo.PlayerController` 的相机管理器，多玩家时不会串到别人的相机）；覆盖 `EndAbility` 做双保险：句柄 Pop，且**只有本 GA 压过镜头时**才额外 `PopAllTransientModes`（避免没碰相机的 GA 结束误清别人的瞬态镜头）。子类 GA 只要在末尾调用 `Super::EndAbility` 就自动获得清理。
- 第三刀把触发点挪进了动画通知（见 §二十一）：`ApplyDamageToActor` 不再直接触发镜头冲击，ComboConfig 里的按段 `CameraPunch` 字段已撤销。
- `CleanFinishedModes` 增加 `Mode finished: ... (StackSize->N)` 日志，补上淡出结束的可观测点。

### 2. 预期效果

- 每次命中 FOV 短暂收窄约 6° 后在 0.25s 内回弹，带极轻微偏转；连段多段连续命中时多个冲击叠加，不会互相截断。
- 技能 GA 只需在 `ActivateAbility` 里 `PushSkillCamera`，`EndAbility` 无需额外代码；被打断（受击/闪避）镜头同样平滑回位。
- AI 命中不会触发本地玩家的冲击（只有本地控制的角色会调 `AddCameraPunch`）。

### 3. 调试（PIE）

> 注：第三刀之后冲击改由动画通知触发，下面 1~3 条的触发方式以 §二十一 为准。

1. 普攻打敌人：每次命中 FOV 收一下再回弹；快速连续命中看到连续冲击叠加。
2. 需要冲击日志时把日志级别调到 Verbose，关键字 `[HelloCamera] Punch FOV=...`。
3. 打空气/被闪避（无敌帧）不应触发冲击——`ApplyDamageToActor` 在无敌帧分支提前 return，不会走到 Punch。
4. 技能镜头：用 `PushSkillCamera` 起镜头，再触发闪避打断技能，确认镜头回位、`Mode finished` 日志出现、栈大小回到 1。
5. 双人测试（若可）：另一名本地玩家或 AI 命中时，不应影响当前玩家相机。

### 4. 可调参数

- 冲击改为**动画通知**配置：`UAnimNotify_HelloCameraPunch` 的 `FOVDelta` / `ScreenOffset` / `Duration`（见 §二十一）。ComboConfig 中已无 `CameraPunch` 字段。
- `DecayExponent`（默认 4）与 `ScreenOffsetDegreesPerUnit`（默认 5）在 Punch modifier 上为 `EditDefaultsOnly`。

### 5. 已知风险

若某引擎版本的 `UpdateViewTargetInternal` 不是可覆盖的虚函数，编译会报错；备选方案是在自己的 `UpdateViewTarget` 末尾手动调用 `ApplyCameraModifiers`，但必须避免同一帧内推进两次修饰器时间（否则冲击衰减会快一倍）。本刀按 UE5 的标准接口实现。

## 二十一、阶段 4 第三刀改动记录（镜头配置进动画，2026-10-25）

### 1. 改了什么

- 新增 `UAnimNotify_HelloCameraPunch`（单帧通知）：字段 `FOVDelta` / `ScreenOffset` / `Duration`，`Notify` 里通过 `AHelloPlayerCameraManager::GetForActor` 只对**本地控制**的角色生效。
- 新增 `UAnimNotifyState_HelloSkillCamera`（区间通知）：`NotifyBegin` Push 一个 Anchored 镜头、`NotifyEnd` Pop；参数直接复用 `FHelloCameraModeConfig`（AnchorSocket / AnchorOffset / bLookAtCharacter / FOV），`HoldDuration` 在 Begin 时被强制置 0——**镜头存活时长完全由通知区间决定**；`BlendInTime` / `BlendOutTime` 可配（<0 用模式默认）。
- `AHelloPlayerCameraManager::GetForActor(Actor)`：从 Pawn 解析本地玩家相机管理器，AI/远端返回空，避免敌人动作抢玩家镜头。
- `AHelloCharacterBase`：新增 `ActiveSkillCameraHandle` 与 `PopActiveSkillCamera()`。AnimNotifyState 是共享 CDO，不能存实例状态，所以句柄记在角色上。
- `UHelloGA::EndAbility`：除自己的句柄外，还会弹出角色上记录的“通知镜头”，覆盖 Montage 被打断时 NotifyEnd 不触发的情况；只有确实压过镜头才 `PopAllTransientModes`。
- 换人：新增 `RemoveAllTransientModes()`（立即移除，不淡出），在 ViewTarget 变化时调用，避免旧角色的技能镜头按新角色 Socket 重新解析。
- 撤销 `ComboConfigDataAsset` 的 `FComboSegmentCameraPunch` 与 `ApplyDamageToActor` 中的冲击触发。

### 2. 配置方式

- **冲击**：打开 Montage → 在打击帧上右键 Add Notify → 选 `Hello Camera Punch`，在 Details 里调 `FOVDelta`（例如 -6）、`ScreenOffset`（0 = 纯 FOV）、`Duration`。哪一刀想震就在哪一刀加，其余段不加就完全没有。
- **技能镜头 / 处决**：拖一个 `Hello Skill Camera` 区间，起点=接管开始、终点=回默认；Details 里填 `AnchorSocket`、`AnchorOffset`、`bLookAtCharacter`、`FOV`、`BlendIn/OutTime`。勾选 `Require Hit` 后改为命中才推入镜头（见 §二十四）。

### 3. 注意事项

- 只对本地控制的角色生效；AI 或远端角色的动画通知不会抢玩家相机。
- 同一角色同时只支持一个通知镜头：区间重叠时 Begin 会先把上一个收掉。
- NotifyEnd 在 Montage 被强制中断时引擎行为不完全可靠，因此 GA 的 `EndAbility` 会兜底弹出；**不经过 GA 的演出**（例如 Sequencer 或自定义流程）需要自己保证有 Pop 路径，否则会卡在技能机位。
- 换人清理是“立即移除”而不是淡出：淡出期间模式仍会按新角色解析 Socket，会产生一帧跟随新角色的怪异画面。

### 4. 测试

1. 只在第 5 段动画上加一个 CameraPunch：只有终结技命中时 FOV 收缩，前 4 段完全安静。
2. 同一刀打中两个敌人：仍然只有一次冲击（通知是按时间轴触发，不是按命中次数）；如果需要“每次命中都震”，要改回代码侧触发。
3. 加一个 Skill Camera 区间：进入区间平滑推入，区间结束平滑回位；中途用闪避打断 → GA EndAbility 兜底回位，日志出现 `PopCameraMode` / `Mode finished`。
4. 技能镜头播放中换人：镜头立刻回到新角色的第三人称，日志出现 `RemoveAllTransientModes`。

> 2026-10-27 已改为“通知上膛 + 命中确认”，见 §二十三：挥空不再震，一刀多敌仍只震一次。

### 5. 镜头晃动排查（SkillCamera）

技能镜头抖动来自“Socket 挂在会动的骨骼上，逐帧重新解析”，按优先级处理：

1. `bFreezeAnchorOnStart`（默认 **true**）：Begin 时锁存 Socket 变换与看向目标，镜头不再跟随呼吸/摆头/挥刀。固定机位的特写、处决演出应该保持开启；想要手持跟拍感才关掉。
2. 换 Socket：抖动明显时优先把锚点放在稳定骨骼（骨盆/脊柱下端）而不是手、头；把“运镜”交给 `AnchorOffset` 的动画曲线而不是骨骼本身。
3. `ViewSmoothSpeedOverride` / `RotationSmoothSpeedOverride`：<=0 用模式默认（6 / 10）。数值越大越跟手、越小越稳；锁存模式下这两个基本只影响入场平滑，逐帧模式下才是主要抗抖手段。
4. 仍抖就检查角色本身是否有根运动/位移动画：Socket 跟随 Mesh，角色整体移动时镜头会跟着动，这属于预期，需要固定世界机位就得关掉跟随（当前未提供世界锚定选项）。

## 二十二、换人相机连续性（2026-10-27）

### 1. 问题与根因

切人时相机会“重置”：视角被甩回新角色背后，看起来像机位被重置。

根因：`ServerSwitchToCharacter_Implementation` 对调两名角色的位置/朝向后再 `Possess(NewTarget)`，引擎 Possess 流程会把 `ControlRotation` 对齐到新 Pawn 的 `ActorRotation`。本项目角色 `bUseControllerRotationYaw = false` + `bOrientRotationToMovement = true`，身体朝向跟随移动方向，和玩家视线 Yaw 经常相差很大（站桩转视角、横向移动、后撤时最明显）。相机每帧用 `PCOwner->GetControlRotation()` 计算机位（`EvaluateCameraStack` → `ThirdPerson::UpdateView` 的 `CamForward`），ControlRotation 一变，理想机位就绕着角色转过去，再由 `CameraLagSpeed` / `CameraRotationLagSpeed` 追赶，表现为 0.2~0.3s 的“甩回背后”。

### 2. 改了什么

只改 `AHelloPlayerController::ServerSwitchToCharacter_Implementation`（`HelloPlayerController.cpp`），在 `Possess(NewTarget)` 前后包一层：

- `Possess` 之前记录 `GetControlRotation()`；
- `Possess` 之后，若切换成功（`GetPawn() == NewTarget`），则 `SetControlRotation(SavedControlRotation)`；
- 打一条 `[HelloCamera] Possess preserved ControlRotation ...` 日志。

为什么不在 `Possess`/`OnPossess` 覆盖里做：本引擎版本的 `AController::Possess` 不可作为可覆盖虚函数（编译报“没有该成员”）；而 `OnPossess` 执行时，`Possess` 内部已经先松开了旧 Pawn，旧视角与“是否换人”的判断都拿不到。放在换人 RPC 里能完整包住 `Possess` 调用，也不依赖引擎虚函数。代价是只覆盖换人路径——以后若有复活/重生成等其它换 Pawn 路径想要同样效果，需要在对应路径照抄一次 save/restore。相机层零改动：`HelloPlayerCameraManager`、镜头栈、平滑逻辑都不动。

### 3. 预期效果

- 静止（视角与角色朝向不同）切人：画面完全不动，只是模型换人；不再甩回角色背后。
- 奔跑切人：镜头保持原方向继续跟随，不甩到移动方向背后。
- 切人后立刻按 W：移动方向仍是镜头前方（移动/闪避都以 `ControlRotation` 为基准，见 `Dodge_Directional.md`）。
- 两名角色 `CameraModeTag` 映射同一镜头类时，栈底不会重建，滞后历史连续。

### 4. 调试方法

- 正常：Output Log 每次切人出现一条 `[HelloCamera] Possess preserved ControlRotation: Pitch=... Yaw=... Roll=... (Pawn=...)`；首次 Possess（开局）不应出现该日志。
- 若仍甩镜头：在 `ServerSwitchToCharacter_Implementation` 的 `Possess()` 前后各打 `GetControlRotation()`，确认“后”是否等于新 Pawn 的 `ActorRotation`。若日志出现但视角仍被改写，检查 `SetControlRotation` 之后是否还有其他地方再次改写（GA 演出、`ClientRestart`、蓝图里的 Possess 逻辑）。
- 若切人后镜头是**一帧瞬移**而不是甩：说明不是 ControlRotation 连续性问题，而是栈底模式被重建（两人 `CameraModeTag` 映射到不同镜头类时，`SetOwner` 会清 `bHasLastView`），按本节 §6 的后续方案处理。
- 完全没有日志但视角仍被重置：确认实际走的是本类的 `ServerSwitchToCharacter`（蓝图子类若重写了换人逻辑，要沿用同样的 save/restore，不要绕过它直接调 `Possess`）。

### 5. 验收检查清单（PIE）

| # | 操作 | 期望 |
|---|---|---|
| 1 | 站桩把视角转到与角色朝向差 90°/180°，切人 | 画面不位移、不转向，只是模型换人 |
| 2 | 奔跑中切人 | 镜头方向保持，无甩镜 |
| 3 | 切人后立刻按 W | 角色朝镜头前方移动 |
| 4 | 原有 Tag 切换验收（§十三·3） | 仍平滑过渡，不瞬移不翻转 |
| 5 | 技能镜头播放中切人 | 仍立即清除瞬态镜头、栈大小回到 1（行为不变） |

### 6. 已知边界

- `SetControlRotation` 在服务器上不会复制给客户端；当前单机/Listen PIE 无影响。将来联机时，需要在拥有者客户端的换人路径上做同样的事（或改用 `ClientSetRotation`）。
- 目前只在换人路径保留视角。复活、重生成等其它调用 `Possess` 的路径若需要同样效果，要在对应位置补一次 save/restore（`OnPossess` 里拿不到旧 Pawn，不能放在那里）。
- 两个角色骨架高度/轴心不同时，锚点会有厘米级差异，由位置滞后吸收；若明显影响构图，再用按 Tag 的 `TargetOffset` 覆盖，而不是在换人逻辑里特判。
- 若需要“不同 `CameraModeTag` 的角色之间也无缝”，追加方案 B：给 `UHelloCameraMode` 加 `SeedView`，栈底重建时用上一帧 `GetCameraCachePOV()` 做种子；同时把 `ThirdPerson` 的穿透初始化改成独立状态位，不能继续借用 `HasValidLastView()` 当“未初始化”哨兵。
- 切人时正在播技能镜头仍会硬切（`RemoveAllTransientModes` 立即移除，§二十一 的设计），与本次改动无关。

## 二十三、命中确认驱动的镜头冲击（2026-10-27）

### 1. 问题

§二十一 把冲击触发从伤害结算挪进 AnimNotify，好处是“哪一刀震、抖多大”在时间轴上所见即所得；代价是通知到点必发——**挥空也震、打无敌帧也震**。§二十一·4 记录了这个行为，本次把它修掉。

### 2. 改了什么

| 文件 | 变更 |
|---|---|
| `Public/Animation/AnimNotify_HelloCameraPunch.h` | 新增 `ConfirmWindow`（默认 0.2s） |
| `Private/Animation/AnimNotify_HelloCameraPunch.cpp` | 不再直接 `AddCameraPunch`；改为 `AHelloCharacterBase::ArmCameraPunch`，只上膛 |
| `Public/Character/HelloCharacterBase.h` | 新增 `FHelloArmedCameraPunch` 待确认状态 + `ArmCameraPunch` / `TryConsumeCameraPunch` / `ClearCameraPunchArm` |
| `Private/Character/HelloCharacterBase.cpp` | 实现上述三个函数；`DisableWeaponTrace()` 开头清膛；`ApplyDamageToActor()` 在 `HitEnemiesThisAttack.Add` 之后消费 |

核心思路：**通知负责“这一刀想震”，命中负责“这一刀确实打到了”，两者都成立才发冲击。** 参数仍配在时间轴上，动画资产零迁移；已有 5 段上的通知原样保留，只是行为从“到点就震”变成“到点准备、命中才震”。

待确认状态存在角色上（`ArmedCameraPunch`），因为 AnimNotify 是共享 CDO，不能存实例状态——与技能镜头句柄的处理方式一致。

### 3. 运行时序

正常命中：

```
EnableWeaponTrace（伤害窗口开）
  → CameraPunch 通知（打击帧）    → ArmCameraPunch：记参数 + ExpireTime = now + ConfirmWindow
  → 武器 Overlap 命中敌人
  → ApplyDamageToActor 结算伤害   → TryConsumeCameraPunch：已上膛/未消费/未过期 → AddCameraPunch，锁定
  → DisableWeaponTrace            → ClearCameraPunchArm
```

挥空：通知照常上膛 → 没有 Overlap → 窗口过期（或收武器清膛）→ 全程不碰相机。

一刀多敌：首个命中消费并置 `bConsumed`，同一窗口内后续命中不再震。

### 4. 配置方式

通知 Details 字段：`FOVDelta`（默认 -6）、`ScreenOffset`（0 = 纯 FOV）、`Duration`（0.2）、`ConfirmWindow`（0.2）。

摆位建议：放在打击帧上，**不要早于 `EnableWeaponTrace`**。如果命中可能先于通知结算（武器碰撞在打击帧之前就开、Overlap 与 Enable 同帧），那一次挥砍会震不到；把通知放在打击帧或之后一帧即可。

### 5. 行为与取舍

- 冲击时刻 = 真实命中时刻，比打击帧晚 0~2 帧，属预期（这也是要的：手感对齐接触）。
- 同一刀多敌只震一次（`bConsumed`）。想“每命中都震”就把 `bConsumed` 换成冷却时间。
- **打无敌帧不震**：`ApplyDamageToActor` 的无敌帧分支在消费之前就 `return` 了，与 §二十 的调试预期一致。若想让它震，在该分支 `return` 前补一次 `TryConsumeCameraPunch()`。
- `ConfirmWindow` 是容错上限：太短会漏掉“通知之后才接触”的命中，太长会把同一攻击窗口里较晚的命中也算进去（仍是本次攻击，通常无害）。默认 0.2s 覆盖常见挥砍接触延迟。

### 6. 已知边界

- **联机**：命中判定仍依赖本地 Overlap 回调。客户端也会跑一遍 `ApplyDamageToActor`（伤害 GE 由服务器权威结算，客户端那次是空操作），所以纯专用服务器 + 多客户端时可能出现“客户端本地判中、服务器没中”的镜头震。当前单机 / Listen PIE 无影响；多端应改为服务端发 GameplayCue、客户端在 Cue 里震。
- AI / 远端角色即使命中也不会抢玩家镜头：`TryConsumeCameraPunch` 内部仍走 `AHelloPlayerCameraManager::GetForActor`，只对本地控制的角色生效。
- 清理依赖 `DisableWeaponTrace()`：正常攻击结束、连段剪断、后摇移动释放、`GA_NormalAttack::EndAbility` 都会调用（幂等），因此上膛不会跨刀残留；不走这套流程的自定义演出需要自己清膛或依赖窗口过期。
- `ApplyDamageToActor` 目前**没有阵营/队友判定**：武器 Mesh 蹭到自己的队友（也是 `AHelloCharacterBase`）同样会结算伤害并消费冲击。站在队友旁边空挥时，可能被误认为“空挥也震”。要堵这个口，在 `OnWeaponOverlap` 里加队伍过滤（现在还没有队伍数据结构，属于独立改动）。
- 技能镜头（`AnimNotifyState_HelloSkillCamera`）默认仍是纯时间轴驱动：空挥也会推镜头。需要“命中才给特写”就在通知上勾 `Require Hit`（§二十四），不需要的段落保持不勾即可。

### 7. 调试方法

- `[HelloCamera] Punch consumed by hit on X`（Log 级）：确认命中、已发冲击。一次上膛最多一条。
- `[HelloCamera] Punch armed FOV=...`（Verbose）：打击帧通知到了，已上膛。
- `[HelloCamera] Punch arm expired without hit`（Verbose）：上膛后没等到命中，本次作废（挥空的正常结果）。
- `[HelloCamera] Punch FOV=...`（Verbose）：真正写进 modifier 的冲击参数。
- 镜头有变化但以上日志一条都没有：那不是冲击。先看有没有 `PushCameraMode ... HelloCameraMode_Anchored`——技能镜头是时间轴驱动的（§二十一），本来就不看命中；观感区分是“FOV 一收一弹”=冲击，“整体推近/换机位再回位”=技能镜头。
- 命中了但没震：检查该动画上有没有 CameraPunch 通知、通知是否早于 `EnableWeaponTrace`、`ConfirmWindow` 是否过短、角色是否本地控制。
- 没命中却震了：先看 `Punch consumed by hit on ...` 打的是谁——`ApplyDamageToActor` 目前没有阵营判定，武器 Mesh 蹭到**队友**也会算命中（会掉血并消费冲击）。其次检查 `DisableWeaponTrace` 是否被调用（上一刀上膛残留），以及是否有别的代码路径直接调 `AddCameraPunch`。
- 打无敌帧：日志出现 `命中无敌帧中的 ... → 完美闪避` 时，不应出现 Punch 日志。

### 8. 验收检查清单（PIE）

| # | 操作 | 期望 |
|---|---|---|
| 1 | 对着空气挥砍 | 镜头完全不动，无 `[HelloCamera] Punch` 日志 |
| 2 | 命中敌人 | 接触瞬间 FOV 收缩，约 `Duration` 内回弹 |
| 3 | 一刀命中两个敌人 | 只收缩一次，不叠加两次冲击 |
| 4 | 攻击处于无敌帧的敌人 | 不震；日志只有完美闪避 |
| 5 | 只在第 5 段配通知 | 前 4 段命中也不震（时间轴逐刀配置仍有效） |
| 6 | 闪避打断连段后再出刀 | 不会沿用上一刀的上膛参数（参数不同即可辨认） |
| 7 | AI 攻击玩家 | 玩家镜头不晃 |

## 二十四、技能镜头可配置“命中才生效”（2026-10-27）

### 1. 背景

Punch 效果不再使用；保留技能特写，但希望它像 Punch 一样可选“必须命中才播”。本次给区间通知加开关，默认行为不变。

### 2. 改了什么

| 文件 | 变更 |
|---|---|
| `Public/Animation/AnimNotifyState_HelloSkillCamera.h` | 新增 `bRequireHit`（默认 false）与 `MinHoldAfterHit`（默认 0，仅 `bRequireHit` 时有效） |
| `Private/Animation/AnimNotifyState_HelloSkillCamera.cpp` | Begin：勾选时只上膛、不推镜头；否则走原逻辑（顺带清掉残留待命）。End：勾选时走 `EndSkillCameraWindow`，否则走原 Pop |
| `Public/Character/HelloCharacterBase.h` | 新增 `FHelloSkillCameraWindow` 状态 + `ArmSkillCameraForHit` / `TryConsumeSkillCameraOnHit` / `EndSkillCameraWindow` / `ClearSkillCameraWindow`；因持有 `FHelloCameraModeConfig` 引入对应头文件 |
| `Private/Character/HelloCharacterBase.cpp` | 命中结算处（`ApplyDamageToActor`）调用消费；`PopActiveSkillCamera` 增加延迟回位定时器清理与 `bPushed` 复位；`DisableWeaponTrace` 作废待命，防跨刀残留 |
| `Private/AbilitySystem/GA/HelloGA.cpp` | `EndAbility` 在 Pop 之后补 `ClearSkillCameraWindow()`，覆盖被打断时 NotifyEnd 不触发的情况 |

状态同样记在角色上（`AnimNotifyState` 是共享 CDO），与原有 `ActiveSkillCameraHandle` 一致。

### 3. 运行时序（bRequireHit = true）

```
区间 Begin（打击帧附近）  → ArmSkillCameraForHit：记 Config/混合时长/最小保持，不推镜头
命中（ApplyDamageToActor 结算伤害）
                          → TryConsumeSkillCameraOnHit：推入 Anchored 镜头，记录命中时刻
区间 End                  → 未命中：清膛，全程没有任何镜头变化
                          → 已命中：回位；若命中时刻 + MinHoldAfterHit 还没到，用定时器延迟回位
被打断（闪避/受击/换人）   → GA EndAbility：PopActiveSkillCamera + ClearSkillCameraWindow 兜底
```

`bRequireHit = false` 时完全沿用旧路径：Begin Push、End Pop，空挥也播。

### 4. 配置方式

打开动画 Montage → 选中 `Hello Skill Camera` 区间通知 → Details：

- 勾 `Require Hit`：命中才推镜头。
- `Min Hold After Hit`（秒）：命中后至少保持多久再回位；0 = 完全由区间决定。命中靠近区间尾巴时用它避免“只闪一下”。
- 其余字段（`AnchorSocket` / `AnchorOffset` / `bLookAtCharacter` / `FOV` / `BlendIn/OutTime`）语义不变。

摆位建议：区间起点放在打击帧**略前**（1~2 帧）。命中后才推，所以提前开始只是多等一会儿，不影响正确性；但如果起点和打击帧同帧，命中可能先于区间 `Begin` 结算而导致漏掉。区间终点决定特写总时长。

Punch 不再使用的话，把动画上的 `Hello Camera Punch` 通知删掉即可，代码保留不影响运行（`FOVDelta=0` 也能等效关闭）。

### 5. 行为与取舍

- 空挥：整个区间无任何镜头变化（这是本次要的效果）。
- 命中：镜头在**接触帧**才开始混合进入，比时间轴触发更晚但也更“实”。
- 打无敌帧不算命中（`ApplyDamageToActor` 的完美闪避分支提前 return），不会推镜头。
- 打队友算命中（目前没有阵营判定，见 §二十三·6），会推镜头。
- 同一角色同时只支持一个技能镜头区间：新区间 Begin 会先收掉上一个镜头（沿用原约束）。
- 命中必须发生在区间内：区间 `Begin` 之前或 `End` 之后的命中都不会推镜头。起点和打击帧同帧时，若命中先结算会漏——起点提前 1~2 帧即可。
- 延迟回位的定时器在 GA 结束、新镜头到来时会被取消并立即回位，不会卡在特写机位。

### 6. 调试方法

- `[HelloCamera] SkillCamera armed, waiting for hit`（Verbose）：区间 Begin 已上膛。
- `[HelloCamera] SkillCamera pushed by hit (X, Handle=N)`（Log）：命中确实推入了镜头。
- `[HelloCamera] SkillCamera window ended without hit`（Verbose）：空挥，区间结束未命中。
- `[HelloCamera] SkillCamera pop delayed X.XXs (min hold after hit)`（Verbose）：命中太晚，延迟回位。
- 空挥有 `armed` 没有 `pushed by hit` 属正常；有 `pushed` 但没回位，先看 NotifyEnd 有没有触发（打断场景靠 GA EndAbility 兜底），再看是否有 `pop delayed` 但定时器被清掉。
- 命中却没推：确认区间勾了 `Require Hit`、角色是本地控制、命中确实走了伤害结算（看 `造成 X 点伤害` 日志）。

### 7. 验收检查清单（PIE）

| # | 操作 | 期望 |
|---|---|---|
| 1 | 勾 `Require Hit`，对空气放终结技 | 全程无镜头变化；日志 armed + ended without hit |
| 2 | 勾 `Require Hit`，命中终结技 | 接触瞬间推入特写，区间结束平滑回位 |
| 3 | 不勾 `Require Hit` | 与旧行为一致，空挥也推镜头 |
| 4 | 命中靠近区间末尾，`Min Hold After Hit=0.5` | 镜头至少保持 0.5s 再回位 |
| 5 | 命中后立刻闪避打断 | 镜头立即回位，无卡镜头 |
| 6 | 命中后连续切段/换人 | 不残留特写机位，栈大小回到 1 |








