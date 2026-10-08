# speedkey — 战斗倍速（`mod-speedkey.dll`）

**卫星模块**。它不自己挂钩输入，由伞形 `d3d11.dll` 在启动时
`LoadLibraryW` 进来，然后每帧调用它导出的 `sk_frame_tick()`。

```
伞形 d3d11.dll ──LoadLibraryW──► mod-speedkey.dll
               ──每帧调用─────► sk_frame_tick()
               ──焦点变化─────► sk_input_focus(bool)
```

## 做什么

战斗与大地图的 1x / 2x / 5x / 10x 倍速，主键盘与小键盘的 `+` / `-` 切档。
改的是游戏自己的写速总闸（`Global.SetGameSpeed`），所以是游戏认可的速度，
不是靠改时间缩放之类的旁路。

对话与剧情演出期间自动让路（`dialogue_force_1x`）；
每次进战斗 / 读档 / 切场景后自动恢复当前档位（`guard_reassert`）。

## 源码

| 文件 | 作用 |
|---|---|
| `speedkey.c` | 全部实现：配置、挂钩、状态机、tick |
| `config.h` | ini 解析 + 默认配置生成 |
| `fingerprints.h` | 一致性指纹表 |
| `rva_anchors.h` | 生成物：运行时重定位锚点表 |

`../shared/` 里的 `common.h` / `rva_reloc.h` / `exports.h` / `keymap.h` /
`proxy.S` / `minhook/` 为四个模块共用。

## 挂钩点

| 目标 | 用途 |
|---|---|
| Stage 相关 | 场景/战斗实例切换 → 恢复档位、重判上下文 |
| Menu 实例 | 判断当前处于战斗内还是大地图 |
| Talk / 对话 Update | 判定「对话进行中」，用于让路 |
| `Global.SetGameSpeed` | 写速总闸（`trace_gset` 只做记录，不干预） |

## 配置

完整说明见 `../docs/04-配置说明-完整.md` 第 2 节。常用两条：

```ini
[options]
speed_steps = 1,2,5,10        ; 档位序列（升序，上限 = 最后一项）

[hotkeys]
equals = speed_up
minus = speed_down
```

伞形 `hslr-wait-hotkey.ini` 的 `[features] speed = 0` 可整体关掉本模块。

`settings.ini` 是**安装器用的源文件**（键位与 modId 的源头），
安装时会被同步进游戏根目录的 `hslr-speed-hotkey.ini`。

## 编译

```powershell
./build.ps1 -Zig <zig.exe 路径> -OutDir ./dist
```

比 hotkey 多链一个 `-lgdi32`（叠字显示需要）。其余注意点见
`../docs/05-构建与发布.md`。

## 测试

`tests/proxy-smoke.c` 与 hotkey 的同名测试同源，验证代理导出表与转发。

## 版本

`SK_VERSION`（`speedkey.c` 顶部）。

## 许可

MIT。
