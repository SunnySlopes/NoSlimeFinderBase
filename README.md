# NoSlimeFinderBase

NoSlimeFinder 的核心计算库 + 独立命令行程序。

本仓库从 [NoSlimeFinderGUI](https://github.com/SunnySlopes/NoSlimeFinderGUI) 中拆出：
所有 C++ 搜索逻辑、cubiomes 群系采样和 SlimeOptimizer 刷怪环几何都在这里，
主库只保留 Java 界面、JNI 薄桥接和一个构建脚本。

## 与主库的契约

| 项目 | 位置 | 说明 |
| --- | --- | --- |
| 公共 C API | `include/noslimefinder/base.h` | 主库 JNI 薄桥接唯一包含的头文件；API 版本 1.0.0 |
| 静态库 | `build/lib/libnoslimefinderbase.a` | `jni/NoSlimeFinderBridge.cpp` 链接它生成 DLL |
| 独立 CLI | `build/bin/NoSlimeFinderBase-cli.exe` | 可单独分发，也随主库 JAR 一起打包 |
| 数据结构 | `NoSlimeParams` / `NoSlimeResult` / `NoSlimeBiome` | 字段顺序是契约的一部分 |
| 版本解析 | `noslime_parse_mc` / `noslime_mc_name` / `noslime_newest_mc` | 主库不再直接依赖 cubiomes |
| 群系列表 | `noslime_list_biomes` | 仅主世界、当前版本存在、按 id 降序 |

主库侧的顺序是：

```text
gradlew buildMainJar
  -> verifyBaseSubmodule      检查本子模块与嵌套子模块是否就绪（缺失则构建失败）
  -> jni/build-jni.bat        调用本仓库 scripts/build_msys2.ps1，再链接薄桥接
  -> 打包 JAR                 native/windows/libNoSlimeFinderLibJ.dll
                              native/windows/NoSlimeFinderBase-cli.exe
```

主库固定把本仓库放在 `NoSlimeFinderGUI/NoSlimeFinderBase` 作为 git 子模块。

## 子模块

| 子模块 | 地址 | 用途 |
| --- | --- | --- |
| `cubiomes/` | <https://github.com/xpple/cubiomes> | 群系生成 `getBiomeAt()`、群系名、版本解析 |
| `slimeoptimizer/` | <https://github.com/CITYWIDESIGN/SlimeOptimizer> | 刷怪环几何 `computeRingSpans()`、`isSlimeChunk()` |

首次克隆后：

```powershell
git submodule update --init --recursive
```

两个子模块都未修改；`slimeoptimizer` 固定的 commit 记录在
`docs/SLIMEOPTIMIZER_COMMIT.txt`。

## 构建

### MSYS2 脚本（主库使用，不需要 CMake）

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_msys2.ps1
```

产物：

```text
build\lib\libnoslimefinderbase.a
build\bin\NoSlimeFinderBase-cli.exe
```

脚本会自动查找 `C:\msys64\ucrt64\bin`（或 `mingw64`），可用环境变量
`MSYS2_ROOT` / `MSYS2_MINGW` 覆盖。`-Clean` 会先清空 `build\obj` 等目录。

### CMake

```powershell
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake
.\build-cmake\bin\NoSlimeFinderBase-cli.exe --help
```

CMake 构建同样使用 `cubiomes/` 和 `slimeoptimizer/` 源码，不修改它们的构建文件。

### 独立分发 exe

```powershell
cmake --install build-cmake --prefix dist
```

`dist\bin\NoSlimeFinderBase-cli.exe` 已静态链接 GCC 运行库和 libstdc++，
只依赖 Windows 自带的 UCRT，复制单个 exe 即可运行，不需要任何配置文件。
安装目录同时包含 `dist\bin\LICENSE.txt`，分发时请与 exe 一并保留。
`scripts\build_msys2.ps1` 也会把同一份许可证复制为 `build\bin\LICENSE.txt`。

## CLI 用法

```text
NoSlimeFinderBase-cli --seed <seed> [options]
NoSlimeFinderBase-cli --list-biomes [--mc <version>]
```

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--seed <int64>` | 必填 | 世界种子，可负 |
| `--min-x` / `--max-x` | `-2048` / `2047` | 挂机点 X 范围（含端点） |
| `--min-z` / `--max-z` | `-2048` / `2047` | 挂机点 Z 范围（含端点） |
| `--square <side>` | 未设置 | 以 0,0 为中心的正方形；不能与显式 min/max 混用 |
| `--threads <n>` | CPU 核数（≤256） | 工作线程数 |
| `--mc <id\|version>` | 最新（`26.3`） | 群系版本；也接受 cubiomes `MCVersion` 数值 |
| `--biome-y <int>` | `-63` | 群系采样高度（-64..319） |
| `--min-area <blocks>` | `49000` | 最低有效面积（0..49640） |
| `--max-results <n>` | `100` | 最多输出条数（1..1000） |
| `--avoid <ids>` | 空 | 逗号分隔的避让群系 id，可重复，例如 `--avoid 14,52` |
| `--candidate-limit <n>` | `0` | 粗筛候选格上限；0 = 自动 |
| `--out <file>` | 未设置 | 把同一份文本报告写入文件（同时仍打印到 stdout） |
| `--quiet` | 关闭 | 不显示进度行 |
| `--list-biomes` | — | 列出当前版本的主世界群系（`id\|name`）后退出 |
| `--help` / `-h` | — | 帮助 |
| `--version` | — | CLI 与 API 版本 |

示例：

```powershell
# 以 0,0 为中心 4096x4096，输出前 20 条到 txt
NoSlimeFinderBase-cli --seed 0 --square 4096 --max-results 20 --out result.txt

# 指定区域/版本/避让群系
NoSlimeFinderBase-cli --seed -184718958561915 --min-x -8192 --max-x 8191 `
    --min-z -8192 --max-z 8191 --mc 1.18 --avoid 14,52 --threads 8 --out r.txt

# 列出 26.3 的主世界群系
NoSlimeFinderBase-cli --list-biomes --mc 26.3
```

进度输出到 stderr；结果报告输出到 stdout（并写入 `--out`）。程序支持
Ctrl+C 取消，退出码见下。

## 输出格式

```text
# NoSlimeFinderBase CLI 1.0.0
# seed=0 afk=[-2048,2047]x[-2048,2047] ring=24<d<=128 threads=12 mc=26.3(35) biome_y=-63 min_area=49000 max_results=100 avoid=none hits=95 elapsed=1.234s
# columns: x z area slime chunks avoid_x avoid_y tp
x	z	area	slime	chunks	avoid_x	avoid_y	tp
48	32	46462	3178	13	0	0	/tp 48 -64 32 area=46462 slime=3178 chunks=13 avoid=0+0
```

| 列 | 含义 |
| --- | --- |
| `x` / `z` | 挂机点方块坐标（`/tp` 的 Y 固定 -64） |
| `area` | 有效面积 `49640 - slime - avoid_x` |
| `slime` | 环内落在史莱姆区块中的方块数 |
| `chunks` | 192 甜甜圈内的史莱姆区块数 |
| `avoid_x` / `avoid_y` | 避让群系中非史莱姆 / 同时是史莱姆的面积 |
| `tp` | 可直接粘贴的 `/tp ...` 命令；`avoid` 显示为 `x+y` |

结果按 `area` 降序、距离升序排列，并做 64×64 邻域去重。

## 版本与群系

`--mc` 接受 `1.18`、`1.18.2`、`1.19`、`1.19.2`、`1.19.4`、`1.20`、`1.20.6`、
`1.21.3`、`1.21.4`、`1.21.5`、`26.1`、`26.2`、`26.3`、`latest` 或 cubiomes
`MCVersion` 数值。主世界群系列表会按版本过滤（例如 1.18 没有
`dappled_forest`），并按 id 降序返回。

## 退出码

| 码 | 含义 |
| --- | --- |
| `0` | 成功 |
| `1` | 运行错误（参数/文件等） |
| `2` | 命令行用法错误 |
| `3` | 搜索过程出错 |
| `4` | 被 Ctrl+C 取消 |

## 目录结构

```text
NoSlimeFinderBase/
├─ CMakeLists.txt              CMake 构建（静态库 + CLI）
├─ scripts/build_msys2.ps1     MSYS2 直接构建（主库默认使用）
├─ include/noslimefinder/base.h  公共 C API（契约）
├─ src/
│  ├─ noslime_search.cpp       搜索核心（粗筛 / 扫描 / 4×4 / 1×1）
│  ├─ biomes.cpp               cubiomes 封装：版本、群系列表
│  └─ cli/main.cpp             独立 CLI：参数、进度、文本报告
├─ cxx_compat/pre_cubiomes.h   C++20 与 cubiomes rng.h 的 lerp 兼容
├─ docs/SLIMEOPTIMIZER_COMMIT.txt
├─ cubiomes/                   子模块
└─ slimeoptimizer/             子模块
```

## 验证

拆分时用同一个 Java 回归程序分别加载拆分前 DLL 与拆分后 DLL，对以下参数做了
逐行比较，结果完全一致：不同种子、±512/±1024/±2048 范围、1.18/26.2/26.3、
带/不带避让群系、不同线程数与输出条数。另验证了 `--list-biomes` 的群系数量
（1.18=50、1.19.2=52、1.21.3=53、1.21.4=54、26.1=54、26.2=55、26.3=56）和
JNI 的进度/暂停/继续/停止。

## 许可证

本项目以 **MIT 许可证**发布，Copyright (c) 2026 SunnySlopes，完整文本见
[LICENSE](LICENSE)。主库 JAR 内也会携带该许可证（`META-INF/licenses/NoSlimeFinderBase-LICENSE.txt`）。

第三方组件保留各自许可：

- `cubiomes` — MIT，Copyright (c) 2020 Cubitect，见 `cubiomes/LICENSE`。
- `slimeoptimizer` — 版权归 CITYWIDESIGN；上游未附许可证文件，本仓库未修改其内容。
