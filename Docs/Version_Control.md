# 版本管理（Git + Git LFS）

> 状态：**方案已确认，规则文件已落地（2026-10-28）**；仓库初始化与远端推送需在本机执行（本文 §三、§四）。
> 方案：Git + Git LFS，仓库放本机，另配私有远端（GitHub / Gitee）做异地备份。
> 适用：单人开发、UE 5.5 C++ 项目，资源含整套 SwordAnimsetPro（2300+ 文件，二进制为主）。

## 一、目标与边界

| 需求 | 做法 | 落点 |
|---|---|---|
| 任意节点可回滚 | 每次可验收闭环一次提交，关键节点打 Tag | §五、§七 |
| 二进制资源可版本化 | `.uasset`/`.umap`/图片/音频/FBX 走 Git LFS | `.gitattributes`（§二） |
| 不把生成物塞进仓库 | 忽略 `Binaries`/`Intermediate`/`Saved`/`DerivedDataCache` 及 IDE 文件 | `.gitignore`（§二） |
| 异地备份 | 私有远端 + `git push` | §四 |
| 不进 Git 的东西 | 编译产物、缓存、日志、`.sln`（可重建）、本机编辑器布局 | §二 |

明确不做：不做多人协同（二进制资源无法合并）、不做 P4 式文件锁、不做自动构建。将来多人同时改同一资源，再考虑 `git lfs lock` 或 Perforce。

## 二、仓库规则文件

项目根目录已有两个文件，**必须在首次 `git add` 之前就位**：

- `.gitignore`：排除 `Binaries/`、`DerivedDataCache/`、`Intermediate/`、`Saved/`、`.vs/`、`.idea/`、`.vscode/`、`*.sln`、`*.suo`、`*.VC.db`、系统临时文件。
- `.gitattributes`：`*.uasset`、`*.umap`、`*.ubulk`、`*.pak`、`*.ucas`、`*.utoc` 以及 png/jpg/tga/exr/hdr/psd、wav/mp3/ogg/flac、mp4/mov/avi、fbx/obj/blend/max/ma/mb、ttf/otf 全部标记为 `filter=lfs diff=lfs merge=lfs -text`。

判断规则是否生效：`git status` 里不应出现 `Binaries/`、`Intermediate/`、`Saved/` 下的任何文件；出现即说明 `.gitignore` 被改坏或没放在仓库根目录。

LFS 抽查：`git lfs ls-files` 有输出、`git lfs status` 正常。

## 三、首次初始化（只做一次）

前置：安装 Git for Windows（自带 LFS），关闭 UE 编辑器（文件被占用会导致漏提交或提交半保存状态）。

```powershell
git --version
git lfs version

git config --global user.name "chenxin"
git config --global user.email "你的邮箱"
git config --global core.longpaths true     # UE 资源路径常超 260 字符
git config --global core.autocrlf false     # 避免换行被改写

cd E:\UE\Hello
git init -b main
git lfs install --local                      # 必须在 add 资产之前
git add .gitattributes .gitignore
git add .
git commit -m "chore: 初始化版本库，首次导入 UE5.5 项目"
```

**顺序坑**：若先 `git add .` 再装 LFS，资产会以普通二进制进入 Git 对象，之后必须补救：

```powershell
git lfs migrate import --include="*.uasset,*.umap,*.png,*.wav,*.fbx" --everything
```

首次提交体积可达数 GB、耗时数分钟，属正常。检查结果：

```powershell
git lfs ls-files | Measure-Object -Line
git log --oneline
git status          # 应为 clean
```

## 四、远端配置

先在 GitHub / Gitee 建**空**私有仓库（不勾 README，避免首推冲突）。

```powershell
git remote add origin https://github.com/chenxinLALALA/hello.git
git push -u origin main
```

改地址（填错、换账号）：`git remote set-url origin 新地址`；改名：`git remote rename 旧名 新名`。均不影响本地历史。

**HTTPS 证书报错**（`SSL certificate ... unable to get local issuer certificate (20)`）按序处理：

1. 走 Windows 证书库：`git config --global http.sslBackend schannel`，然后 `git ls-remote origin` 验证。
2. 检查环境变量 `GIT_SSL_CAINFO` / `SSL_CERT_FILE` 是否指向不存在的路径（装过 Anaconda 常见），是则删除。
3. 代理/安全软件改写 TLS：导入其根证书，或关闭其 TLS 解密；仅让 Git 走代理时用 `git config --global http.proxy http://127.0.0.1:7890`。
4. 换干净 CA 包：`git config --global http.sslCAInfo "$env:USERPROFILE\cacert.pem"`（文件从 https://curl.se/ca/cacert.pem 下载）。
5. 以上都不通改用 SSH：`git remote set-url origin git@github.com:chenxinLALALA/hello.git`；22 端口被挡时在 `~/.ssh/config` 里配 `Host github.com / HostName ssh.github.com / Port 443 / User git`。

不要用 `http.sslVerify false` 长期绕过校验。

**LFS 配额**：免费额度通常约 1GB 存储，整套资源很可能超。推送失败时退路一是远端只放代码与文档、资源另存移动硬盘；二是本机裸仓库当备份远端：

```powershell
git init --bare E:\Backup\Hello.git
git remote add backup E:/Backup/Hello.git
git push backup main
```

## 五、日常最小流程

```powershell
git status                    # 改了哪些、哪些已暂存
git add .                     # 全提交；只想交部分则 git add 具体路径
git commit -m "feat: xxx"     # 一次可验收的闭环一次提交
git push                      # 推远端
git log --oneline -10         # 确认当前历史
```

节奏：每完成一个能验收的功能点提交一次（与 Docs 的更新同步），不要攒到“做完一大块”才提交；提交信息写清做了什么，例如 `feat: 技能镜头支持命中才生效`、`fix: 换人后镜头回弹`、`docs: 补相机文档 §二十四`。前缀沿用 `feat/fix/docs/chore/refactor` 即可。

## 六、常用操作速查

| 场景 | 指令 |
|---|---|
| 看具体改动 | `git diff` / `git diff --staged` |
| 撤销单文件改动 | `git restore 路径` |
| 把文件撤出暂存 | `git restore --staged 路径` |
| 改提交信息（未推） | `git commit --amend -m "新信息"` |
| 拉取远端更新 | `git pull --rebase` |
| 开实验分支 | `git switch -c feature/xxx` / `git switch main` |
| 临时存放改动 | `git stash` / `git stash pop` |
| 打里程碑 | `git tag -a v0.4 -m "连段完成"` 然后 `git push --tags` |
| 看某次提交内容 | `git show 哈希` |
| 查某文件历史 | `git log --oneline -- 路径` |

早期不必使用 `reset --hard`、`rebase -i`、`cherry-pick`。

## 七、回滚与恢复

```powershell
git log --oneline --graph                 # 找目标提交
git switch -c restore/v0.3 v0.3           # 安全：从旧节点开新分支
git restore --source=v0.3 -- Content/Hello/BP_PlayerCharacter.uasset   # 单文件还原
git reset --hard v0.3                     # 危险：丢弃之后所有改动，仅确认不要时用
```

回滚 LFS 资源与普通文件一致，`git switch` / `git restore` 会自动取回对应版本的 LFS 对象。

## 八、UE 项目注意事项

1. **提交前关编辑器**：避免文件锁与半保存状态；`Saved/` 被忽略也意味着自动保存、布局、日志不会入库。
2. **克隆后没有 `.sln`**：右键 `Hello.uproject` → Generate Visual Studio project files 重新生成；重编译即可，`Binaries/` 不入库是刻意的。
3. **二进制不可合并**：同一 `.uasset` 两端修改只能二选一；单人无碍，多人时用 `git lfs lock` 或改 Perforce。
4. **`.git/lfs` 会持续增长**：每个资源每个版本都留一份，美术反复迭代时占盘明显；定期清理未使用资源，必要时 `git lfs prune`。
5. **`.git` 不要放在 OneDrive/网盘同步目录**，同步冲突会损坏仓库。
6. **远端只存必要内容**：若 LFS 配额吃紧，优先保证 `Source/`、`Config/`、`Docs/` 与项目自有资产，外包资源包可另存。

## 九、故障排查

| 现象 | 原因 / 处理 |
|---|---|
| `repository not found` / 403 | 地址拼错、仓库私有未认证、无权限；先 `git remote -v` |
| SSL 证书校验失败 | 见 §四 |
| `this exceeds GitHub's file size limit` | LFS 规则未生效；检查 `.gitattributes` 并 `git lfs migrate import` |
| `LFS quota exceeded` | 见 §四退路 |
| `git status` 出现 `Saved/` 文件 | `.gitignore` 未生效或被误改 |
| 推送很慢/中断 | 首次推数 GB 正常；可重试 `git push`，已传对象会复用 |

## 十、验收清单

1. `git status` 显示 clean，且从未出现 `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`。
2. `git lfs ls-files` 有输出，`.uasset`/`.umap` 均在列表中。
3. `git log --oneline` 能看到首次导入提交；`git remote -v` 地址正确。
4. 远端私有仓库能看到代码、Config、Docs 与 LFS 资产。
5. 改一个文件 → `status` 可见 → `add` + `commit` → `push` → 远端页面可见该提交。
6. `git switch -c test/restore 首次提交哈希` 后资源完整可打开，再切回 `main` 无异常。
7. 打一个 `v0.x` Tag 并 `git push --tags`，远端 Tags 页可见。
