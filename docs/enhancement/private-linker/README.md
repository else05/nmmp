# NMMP 私有 loader：实现与使用

2026-09-12。已实现可选私有 loader，并完成指定 APK 在 **ARM64 / API27** 的构建、正常签名启动和交错启动对照。**尚未完成整套 ACCEPTANCE；默认 OFF。** API26 实机、完整业务语义/热点、首次激活等缺口见 [验证报告](VALIDATION.md)。上级按需解码/token/native VM 及 P5 stage0 已接入，支持 legacy 与显式 on-demand-v1；最新证据见 [实施记录](../ON_DEMAND_WORK.md)。

## 使用

要求现有 Android SDK/NDK/CMake 环境，以及 Python 3.9+、JDK 17+；本次正式构建使用 WSL NDK r26d、CMake 3.22.1、O-MVLL 1.8.0。开发测试工程的 shared VM 不等同于正式模板的 inner/outer。

```bash
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export NMMP_PRIVATE_LINKER=ON
export NMMP_PRIVATE_STAGE0_VM=ON
export NMMP_VM_DECODE_MODE=on-demand-v1
export CMAKE_BUILD_PARALLEL_LEVEL=5
java -jar /path/to/current/vm-protect.jar apk /path/to/input.apk /path/to/convertRules.txt /path/to/mapping.txt
```

关闭使用 `NMMP_PRIVATE_LINKER=OFF`，未设置也为 OFF；其他值报错。ON 仅接受 ARM64，使用 API26 编译目标，运行时只接纳 API26/27。原库名 `libc++_en.so` 和 APK 集成位置不变；不缩减规则。私有装载或认证失败会明确失败，不生成/加载明文兜底。

将新 JAR 放在独立目录运行，可避免旧 `tools/vmsrc.zip` 缓存混用。ON 会校验 loader 模板存在及格式版本；不要将旧模板的缺失错误当作成功关闭保护。正式模板由 `nmm-protect/mksrc/build-src.sh` 同步，修改源码后需重新构建 JAR。

## 实际调用链

`inner` 包含静态 VM、Codec、VmBinding、ConstantPool、全部生成 wrapper 和注册代码。生成器原有两条 JNI 初始化分支保持原调用顺序；仅在 inner 编译中将 JNI_OnLoad 重命名，交给统一 C bootstrap 适配器调用。wrapper 继续通过 RegisterNatives 注册真实 inner 地址。

构建依赖顺序：链接 inner → ELF/版本/C++ 特性审计 → 拆出 PHDR/DYNAMIC/RELA/JMPREL → 验证各 LOAD 还原等价 → zlib → ChaCha20-Poly1305 → 生成 outer 的载荷。每次打包生成独立 key/nonce；build ID 同时进入 outer、认证头和 inner bootstrap。

运行顺序：outer JNI_OnLoad → stage0 native VM 恢复 key → 一次性认证解压 → 严格表验证 → 匿名映射/BSS → 公共依赖与 eager relocation → cache flush/权限/RELRO → 构造器 → inner bootstrap。业务方法不逐次查符号、解包或经过外层转发。VM 入口仅增加永久装载失败标志的原子检查，不改变解释器循环。

VmBinding 仍独立核验包名、PackageManager signer、实际映射 APK 的 v2 signer 和原绑定参数。当前源码按包名扫描 APK 映射，不用自身函数地址定位，故没有替换其查找路径，也没有信任 outer 传入的路径。loader READY 与 Context 激活 READY 分开。

## 格式、约束及许可

- [FORMAT](FORMAT.md)：独立 loader_format=1，精确字节布局和受控默认版本等价子集。
- [BOOTSTRAP_ABI](BOOTSTRAP_ABI.md)：ABI=1、失败标志、JNI 生命周期和进程常驻合同。
- [ELF_AUDIT](ELF_AUDIT.md) / [原始 P0 审计](p0-audit.json)：C++、CRT、48 项 API stub 检查及已知边界。
- [DEPENDENCIES](DEPENDENCIES.md)：新 loader 源码采用 [Apache-2.0](../../../nmm-protect/mksrc/loader/LICENSE)，Monocypher 4.0.2 采用 BSD-2-Clause；保留原始许可证，与产物一起提供。

仅支持自有 ELF64 LE AArch64 ET_DYN、普通 RELA 的 NONE/RELATIVE/ABS64/GLOB_DAT/JUMP_SLOT。拒绝 text relocation、TLS、IFUNC、COPY、APS2、RELR、必要的非默认符号版本及异常/RTTI/TLS 运行库符号。构建端审计 API26/27 公共 stub；运行端绝对路径打开 `/system/lib64` 公共库，核验提供者及 `dlsym == dlvsym`，不会选择另一版本实现或全局兜底。

成功模块及其依赖常驻。构造器前错误释放本次资源；构造器或 JNI 注册后失败先发布永久失败标志，保留可能被引用的映像。无热更新/卸载。保留展开数据用于本地诊断；没有实现系统 unwinder 对匿名模块的自动注册，不能声称支持 native C++ exception 穿越模块。

运行时不落盘 inner/完整解压容器。key 恢复材料仍在客户端，匿名机器码和 ART 注册指针可被动态观察；这项实现增加静态提取步骤，不承诺不可 dump 或不可 hook。

## 验证与本地诊断

独立测试源码和 golden vectors 位于 `nmm-protect/mksrc/loader/tests`。主机 C 测试在 Linux/WSL 中运行，需要 GNU 兼容链接器的 `--wrap` 和 zlib 开发文件：

```text
python -m unittest discover -s nmm-protect/mksrc/loader/tests -p test_pack.py -v
cmake -S nmm-protect/mksrc/loader/tests -B <host-build> -GNinja -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build <host-build>
ctest --test-dir <host-build> --output-on-failure
```

Android 测试使用 NDK toolchain、arm64-v8a 和 android-26。`GenerateInitFixtures.java` 以当前 JAR 为 classpath 输出绑定/未绑定初始化源码；配置 `NMMP_GENERATED_INIT_TESTS` 后生成 bootstrap 测试程序。实际命令、输入/输出摘要及原始样本见验证目录。

outer 以 `NMMP-Loader` tag 记录 build ID 前缀、load_bias、映射大小和低频装载阶段耗时，不打印 key。完整 build ID 和带符号 inner 留在对应 CMake `private/` 目录。符号化时将崩溃 PC 减去同进程记录的 load_bias，再执行：

```text
llvm-symbolizer --obj=<private/libnmmp_inner.so> <relative-address>
```

阶段状态：P0 静态审计及当前 API27 导入等价已验证；P1 打包/认证及定向坏输入通过；P2 独立映射、构造器、故障、并发测试通过当前设备覆盖；P3 正式集成及指定 APK 启动通过，完整功能矩阵未完成；P4 仅完成本批启动/内存观察，不能标成完整性能验收；P5 已实现并通过 API27 组合启动、同 inner 开关对比及主机/实机程序校验；完整 S5 性能矩阵仍待完成。
