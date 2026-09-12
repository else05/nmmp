# 按需解码与私有 loader 使用

当前已实现按需指令/异常读取、token 化记录、4 行映射、小型 native VM，以及私有 loader 的 stage0 VM 密钥恢复。新模式的循环热点尚未达到原方案总回退 ≤10% 的门槛，保持显式实验选项。API26/27 是支持范围，当前实机证据仅有 ARM64/API27。

## 模式

| 设置 | 默认 | 作用 |
| --- | --- | --- |
| `NMMP_VM_DECODE_MODE=legacy` | legacy | codec2，明确保留旧整方法解码路径 |
| `NMMP_VM_DECODE_MODE=on-demand-v1` | — | codec3/template4，全部选中方法使用按需/token/root native VM；不自动降级 |
| `NMMP_PRIVATE_LINKER=ON` | OFF | 内层压缩、认证加密及私有映射/重定位 |
| `NMMP_PRIVATE_STAGE0_VM=ON` | ON | loader 启用时，外层受限 VM 恢复解包密钥；OFF 仅作旧份额路径对照 |

Java 属性 `-DvmDecodeMode=on-demand-v1` 可覆盖模式环境变量。非法值构建失败。按需或私有 loader 开启时，仅接受 ARM64，选择 API26 编译目标；运行时只接受 API26/27。全部规则方法保持不变，不支持时明确失败。

当前 legacy 仍共用升级后的读取层和检查，实测热点相对原提交也有回退。此开关不等于恢复整个旧实现；精确回退原始运行时应使用 `14bc959` 快照及对应模板。不要把当前 legacy 的功能兼容验证当成性能与原始基线相同。

## 本轮构建入口

本轮最终审查修复后的 JAR：`nmm-protect/build/libs/vm-protect-2026-09-13-0526.jar`，SHA256 `28d6f83f8d099fecbcbdfbbd7acc3b41cb7e9b0347260920430d742fa9530ad7`。本轮未覆盖 `D:/Android/SDK_WSL/nmmp/vm-protect.jar` 或 `E:/env-tool/nmmp-wsl.sh`，继续使用这些旧入口不会自动切换到此版本。

先将新 JAR 和输入 APK 复制到新的本地工作目录，避免重复写入历史生成目录或混用旧 `tools/vmsrc.zip`。在已配置的 WSL 环境中执行：

```bash
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
unset NMMP_TEST_SEED
export NMMP_VM_DECODE_MODE=on-demand-v1
export NMMP_PRIVATE_LINKER=ON
export NMMP_PRIVATE_STAGE0_VM=ON
export OMVLL_CONFIG=/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/scripts/wsl/omvll-config.py
export CMAKE_BUILD_PARALLEL_LEVEL=5
java -jar /path/to/new-run/vm-protect.jar apk \
  /path/to/new-run/input.apk \
  /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt \
  /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt
```

生成的 `build/input-protect.apk` 仍需使用已授权本机签名配置对齐、签名。不要把密码或私钥写入报告。修改 native 源码后，通过 `nmm-protect/mksrc/build-src.sh` 同步正式模板，再构建 JAR；仅编译 Android 测试工程不会更新保护器模板。

当前已签名的完整组合候选位于 `nmm-protect/build/on-demand-20260912/final-wipe-acceptance/protected-signed.apk`，SHA256 `0e11e1b2191c53a0b99ea028f4c3a8ed17a610edc722f24a493921287d0cbb96`。它使用原证书、全部526个选中方法，且未使用固定测试seed；最终擦除修复与验证见 [最终审查修复](POST_REVIEW.md)。[S5结果](S5_RESULTS.md) 中三组冻结性能数据属于擦除补齐前版本，不能冒充此候选的完整性能验收。

## 测试随机参数

仅性能对照使用 `NMMP_TEST_SEED` 或优先级更高的 `-Dnmmp.testSeed=`，严格要求16位ASCII十六进制；空值也报错。它固定 Java 生成器的独立目的随机流，不控制 Python loader 或 O-MVLL，不能保证二进制可复现。两者均未设置时使用 `SecureRandom`；生产构建不要带固定测试参数。

新格式不能与旧模板或记录混用。root、映射、边界描述会在初始化后驻留只读页；指令和异常数据执行时按需读取，每调用最多64字节密钥流缓存、array批次最多256字节，另有固定80字节解释器解码暂存。正常返回与错误出口可靠擦除该暂存，不建立跨调用明文指令/基本块缓存；不承诺返回值、CPU寄存器或编译器副本全部零残留。具体合同见 [格式说明](ON_DEMAND_FORMAT.md)，实现与阶段测试见 [实施记录](ON_DEMAND_WORK.md)。
