# hslr-modkit — 「幻世錄 Remake」四合一工具 Mod 套件

> **English abstract** — A four-in-one mod kit for the Unity IL2CPP game
> *幻世錄 Remake* (Steam AppID 4030150), implemented as a `d3d11.dll` proxy that
> forwards all 51 D3D11 exports to the system DLL and inline-hooks the game with
> MinHook. It adds menu hotkeys, battle speed control, deterministic stat-item
> gains, and thief-skill buffs. The interesting part is the **runtime
> relocation layer** (`shared/rva_reloc.h`): hard-coded RVAs are downgraded to
> *anchors* and re-located at load time by masked-pattern search, so a small
> official game update no longer disables the whole mod on the first frame.
> Build with `zig cc`; no MSVC, no Python, no runtime dependencies. MIT.

---

## 这是什么

四个功能打成一个包，**共用同一个 `d3d11.dll` 代理**（不是四个互相冲突的插件）：

| 模块 | 功能 | 玩家侧开关 |
|---|---|---|
| **hotkey**（伞形，主 Mod） | 角色行动菜单键盘快捷键：空格 = 待机，也可绑移动/攻击/技/魔法/道具/状态 | `[features] wait` |
| **speedkey**（卫星） | 战斗与大地图的 1x / 2x / 5x / 10x 倍速，`+` / `-` 切换 | `[features] speed` |
| **pinattr**（卫星） | 「之源 / 之魂」类属性道具的加值固定为指定值，免去反复 SL 刷随机 | `[features] pinattr` |
| **goldhand**（卫星） | 金之手强化：命中率、气力格数、必定偷到，以及杀敌金钱倍率 | `[features] goldhand` |

「伞形 + 卫星」是这套东西的核心架构：`d3d11.dll` 负责挂钩与每帧驱动，
另外三个 DLL 被它 `LoadLibraryW` 进来、只提供功能本体。
好处是挂钩点唯一（多个 DLL 各自钩同一个地址会互相把跳板踩掉），
且任一卫星失效不会连坐其他功能。

**适配**：游戏 2.2.4（Steam build 25767968）/ Windows 10、11 x64。
**不改存档、不联网、不起外部进程、不修改游戏任何原文件。**

---

## 为什么这个小项目值得一提

游戏是 Unity **IL2CPP** 编译的，没有可读的 C# 程序集，只有 `GameAssembly.dll`
里的一堆机器码。要调用游戏函数，就得知道每个函数的 **RVA**（相对映像基址的偏移）。

问题在于：**RVA 会随官方每次更新而漂移**。哪怕官方只是加了个无关的新功能、
把代码段整体挪了位置，我们写死的 RVA 就全部指错，Mod 在第一秒整体失效。

`shared/rva_reloc.h` 的做法是把「写死的 RVA」降级成**锚点（anchor）**——
含义从「它就在这里」变成「我上次重解时它在哪」——启动时按锚点附近的窗口做
**掩码字节搜索**，唯一命中才采用：

| 模式 | 触发条件 | 行为 |
|---|---|---|
| `NATIVE` | 当前 `GameAssembly.dll` 的 SHA256 命中白名单 | 恒等映射。零开销、零行为变化，与没有这层时逐字节相同 |
| `ADAPTIVE` | 哈希不认识（官方更新了） | 在锚点 ±256 KB 窗口内做掩码搜索重新定位；定位不到的**单个**功能降级，其余照常 |
| `SELFTEST` | 本地验证用 | 强制走扫描，并把查询锚点人为偏移若干字节，用来证明扫描器真能从错误锚点恢复 |

两个关键设计取舍：

1. **只认「窗口内恰好一个命中」**，多命中/零命中一律判失败并降级。
   实测 16 字节指纹在 4 MB 窗口内有上千个误命中，「取最近」没有判据——
   一旦选错，就是拿着错误地址去调用游戏函数（这正是历史上那次启动闪退的形态）。
   宁可少一个功能，不可乱调一个地址。
2. **指纹长度自动升长**：对每个锚点依次尝试 256 / 512 / 1024 字节，
   取第一个能在窗口内唯一命中的长度。Unity 那些只有几行的短 getter
   （`Component$$get_gameObject` 之类）256 字节不唯一，512~1024 才收敛。

离线自测（`tools/`）把真实 `GameAssembly.dll` 用
`LOAD_LIBRARY_AS_IMAGE_RESOURCE` 映射进进程、零代码执行，直接跑与出货 DLL
**同一份**扫描器：在人为偏移 ±0x2A00 / ±0x30000 / +0x3E000（逼近 256 KB 窗口边缘）
五组条件下，四个模块的锚点全部精确找回。

> 详细设计见 `docs/01-设计与架构.md`，加载链路与状态机见 `docs/02-应用模型.md`。

---

## 快速开始（玩家）

**方式 A：创意工坊订阅**（推荐）

1. 游戏主菜单 → 左下角「创意工坊」→ 订阅并下载本 Mod
2. 下载完成后在 Mod 列表里**勾选启用**，然后点 **「套用變更」**
   （不点的话重启后会失效）
3. 重启游戏，`d3d11.dll` 生效

**方式 B：手动安装**

1. 把发行包解压到 `%USERPROFILE%\AppData\LocalLow\UserJoy\HSLR\MODS\待机快捷键\`
2. 双击其中的 `manage.cmd`，选 **3）安装**
3. 进游戏，在 Mod 列表里启用并「套用變更」

`manage.cmd` 菜单：`1` 状态 / `2` 环境自检 / `3` 安装 / `4` 安装+测试配置 /
`5` 卸载 / `6` 看日志 / `7` 设置游戏目录。**不需要装任何东西**——
它用的是 Windows 自带的 PowerShell 5.1。

用不出效果时：先跑 `2` 环境自检，再把游戏根目录的 `hslr-*.log` 发给作者。

---

## 目录结构

```
README.md            本文件
LICENSE              MIT
NOTICE.md            第三方组件与致谢（MinHook / 上游参考实现）
docs/
  01-设计与架构.md     代理 DLL、伞形+卫星、运行时重定位、指纹校验的设计与取舍
  02-应用模型.md       加载链路、线程模型、状态同步、闸门与失败降级
  03-配置说明-基础.md   给玩家：功能开关、倍率/数值、改键
  04-配置说明-完整.md   全部配置项逐条说明（含排障项）
  05-构建与发布.md     zig 构建、主机侧测试、打包
shared/              四个模块共用的代码
  common.h           日志 / 长路径解析 / GameAssembly 构建校验（各 DLL 各编译一份）
  rva_reloc.h        运行时重定位层（NATIVE / ADAPTIVE / SELFTEST）
  exports.h          伞形↔卫星的导出函数签名与卫星 tick 驱动表
  keymap.h           Unity Key 枚举 ↔ ini 键名
  proxy.S, proxy.def d3d11 代理与 51 条导出转发
  minhook/           x64 内联 hook 引擎（BSD-2，见 NOTICE.md）
hotkey/              伞形主 Mod（= d3d11.dll）
  src/waitkey.c        主循环：挂钩、读键、菜单门禁、卫星装载、日志、热重载
  src/menu.h           角色行动菜单门禁与原生点击派发
  src/approach.h       自动接近（已废弃；源码与地址常量保留，不再维护）
  src/config.h         ini 解析与默认配置生成
  src/fingerprints*.h  构建一致性字节指纹表
  src/rva_anchors.h    锚点表（生成物，见 docs/05）
  build.ps1 installer.ps1 manage.cmd settings.ini 使用说明.md README.md
speedkey/  pinattr/  goldhand/
  同构：各含 src/、build.ps1、README.md（speedkey 另有 tests/）
  卫星的运行期配置由各自 DLL 首次启动时自动生成，仓库里不放 settings.ini
```

---

## 构建

需要 **Zig 0.14.1**（自带 clang + mingw-w64，不需要装 MSVC / Windows SDK）：

```bash
# 以伞形为例
cd hotkey
zig cc -target x86_64-windows-gnu -O2 -Wall -Wextra -shared \
  -I src -I ../shared -I ../shared/minhook/include \
  src/waitkey.c ../shared/proxy.S src/proxy.def \
  ../shared/minhook/src/buffer.c ../shared/minhook/src/hook.c \
  ../shared/minhook/src/trampoline.c ../shared/minhook/src/hde/hde64.c \
  -ladvapi32 -luser32 -o dist/d3d11.dll
```

每个模块的 `build.ps1` 把上面这条命令包装好了：

```powershell
./hotkey/build.ps1 -Zig <zig.exe 路径> -OutDir ./hotkey/dist
```

> **注意**：`-I src` 不能省。`shared/rva_reloc.h` 内部 `#include "rva_anchors.h"`，
> 而锚点表是各模块自己的（放在各自 `src/`）——引号包含只搜索「包含者所在目录」。

三个卫星编译时用 `speedkey.c` / `pinattr.c` / `goldhand.c` 替换 `waitkey.c`，
且 speedkey 需要多链一个 `-lgdi32`。

---

## 测试

`hotkey/tests/` 与 `speedkey/tests/` 里有**不启动游戏**就能跑的主机侧验证：

```bash
zig cc -target x86_64-windows-gnu -O2 -Wall -Wextra -I src ../shared tests/hotkey-tests.c -o build/hotkey-tests.exe
./build/hotkey-tests.exe                  # 配置层单测
zig cc -target x86_64-windows-gnu -O2 tests/proxy-smoke.c -o build/proxy-smoke.exe
./build/proxy-smoke.exe dist/d3d11.dll    # 代理冒烟：导出表 + 转发
```

代码在 `-Wall -Wextra` 下应当**零告警**。

---

## 已知限制

- **只适配 2.2.4（build 25767968）**。官方更新后 `NATIVE` 模式失效，
  会转入 `ADAPTIVE` 自动重定位；能找回多少功能取决于官方改了多少。
  日志里搜 `reloc:` 可以看到逐点结果。
- 与其他改写 `NewInputManager.Update` 或同样放置 `d3d11.dll` 的 Mod **互斥**。
- 反作弊/杀软可能对代理 DLL + 内联挂钩误报，需要加白名单。

---

## 免责声明

本项目仅用于个人学习与单机游戏体验增强。使用前请自行评估风险；
因使用本 Mod 造成的存档损坏、封号或其他损失，作者不承担责任。
游戏本体版权归 UserJoy Technology 所有，本项目与官方无关联。

## 作者

**baimudakafei**（笔名 CheN.exe）· <https://github.com/baimudakafei/>

问题反馈、功能建议与适配新版游戏的请求，欢迎到 GitHub Issues 提出。

## 许可

MIT，见 `LICENSE`。第三方组件见 `NOTICE.md`。
