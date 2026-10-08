# hotkey — 伞形主模块（`d3d11.dll`）

四个功能里唯一跟游戏直接挂钩的模块，也是玩家侧看到的那个 Mod 条目
（「待机快捷键」）。它做四件事：

1. **代理 d3d11**：把 51 个导出原样转发给 `System32\d3d11.dll`。
2. **挂钩**：`NewInputManager$$Update` / `OnApplicationFocus` / `AMenu.show`，
   外加一个非致命的 `LogicMgr$$Update` 实例捕获钩。
3. **功能本体**：角色行动菜单的键盘快捷键（默认空格 → 待机）。
4. **装载并每帧驱动三个卫星**（speedkey / pinattr / goldhand）。

玩家向说明见 `使用说明.md`；整体架构见 `../docs/01-设计与架构.md`。

---

## 源码布局

| 文件 | 作用 |
|---|---|
| `waitkey.c` | 主循环：worker 初始化、挂钩、读键、菜单门禁派发、卫星装载与驱动、日志、配置热重载 |
| `menu.h` | 角色行动菜单门禁判定 + 调用游戏自己的 `UIButtonEx.InvokeClick` 完成点击 |
| `approach.h` | 「自动接近」——**已废弃**：源码与地址常量保留、纳入指纹校验，但功能不再维护（且需额外的游戏内 Mod 条目） |
| `config.h` | 零依赖 INI 解析器 + 默认配置文件生成 |
| `fingerprints.h` | 手工维护的一致性指纹（25 条，覆盖每个被调用/挂钩的 RVA） |
| `fingerprints_auto.h` | 自动生成的指纹（16 条，覆盖已废弃的 approach 用到的地址，仍参与校验） |
| `rva_anchors.h` | **生成物**：锚点表（长掩码指纹，供运行时重定位用） |
| `proxy.def` | 51 条 d3d11 导出名（原样转发，未增删） |

`../shared/` 下的 `common.h` / `rva_reloc.h` / `exports.h` / `keymap.h` /
`proxy.S` / `minhook/` 是四个模块共用的代码，都以「头文件 + `static`」
的形式在本 DLL 内各编译一份 —— 这样模块之间不互相导入，
任一卫星损坏都不会影响伞形加载。

## 关键设计点

- **DllMain 里只起线程，不做任何重活。** DllMain 持有 loader lock，
  在里面等待或加载其他 DLL 会死锁。
- **worker 先装卫星再自检。** 三个卫星的指纹表互不相同，伞形自己的校验
  不该连坐卫星能通过的那份。
- **重定位层必须在任何地址使用之前就绪。** `rva_reloc_init()` 在
  `check_fingerprints()` 与全部 `addhook()` 之前调用。
- **`FN()` 宏是唯一的咽喉点。** 所有经函数指针的调用都写成
  `FN(rva, ret, ...)`，底层是 `(BYTE *)ga + rva_of(rva)`。
  少量「直接取指针」的地方用 `rva_of(0x…)` 显式包一层。
- **明文日志第一屏就有诊断信息**：版本、DLL 实际路径、GameAssembly SHA256、
  重定位模式。远端用户把日志发回来，多数问题一眼可见。

## 编译

```powershell
./build.ps1 -Zig <zig.exe 路径> -OutDir ./dist
```

手工命令与必须知道的坑（`-I src` 不能省、Zig 缓存目录、`.buildid` 随机段）
见 `../docs/05-构建与发布.md`。

## 测试

```bash
zig cc -target x86_64-windows-gnu -O2 -Wall -Wextra -I src -I ../shared \
  tests/hotkey-tests.c -o build/hotkey-tests.exe && ./build/hotkey-tests.exe
zig cc -target x86_64-windows-gnu -O2 -Wall -Wextra tests/proxy-smoke.c -o build/proxy-smoke.exe
./build/proxy-smoke.exe dist/d3d11.dll build/scratch
```

## 版本

`WK_VERSION`（`waitkey.c` 顶部）。与 `installer.ps1` 的 `$ModVersion` /
`$ToolVersion`、条目 `mod.json` 的 `version` 需保持一致。

## 许可

MIT。`menu.h` 与代理骨架沿用上游参考实现，见 `../NOTICE.md`。
