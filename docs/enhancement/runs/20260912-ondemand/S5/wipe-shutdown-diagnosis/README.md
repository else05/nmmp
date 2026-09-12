# ART 测试观察器退出诊断

原始 `wipe-device/commands.jsonl` 中 Release 原语义集打印1351断言通过，但随后 SIGSEGV、shell退出139，所以该次运行判定失败。设备epoch 1591025279.741，PID29725，Shutdown thread；栈为未知PC 0x7e64135160 → __cxa_thread_finalize → exit → __libc_init。完整原始crash buffer保存于 wipe-device/crash-buffer.log，包含历史负向APK崩溃，不能都归到本次运行。

相同生产运行时代码、关闭 NMMP_TEST_DECODE_WIPE 的 Release 对照库在新远端目录运行，同一ART语义集1351通过且退出0。原始命令、库SHA与返回码见 nohook-commands.jsonl/nohook-result.json。这将失败范围缩小到测试观察器；栈与桥中 thread_local vector 的析构回调在库卸载后失效一致，未从该未知PC直接恢复符号。

最小修复仅涉及测试桥：以mutex保护线程ID到帧栈的映射，外层退出即删除空项；不再创建 thread_local 对象。回调方法描述符经锁一次发布后只读，JNI解析不持观察锁。保留父帧隔离、异常和并发退出检查，不以 NODELETE 或跳过进程退出规避。生产运行时、模板、JAR与APK不因这次测试修复变更。

修复后主机证据在 wipe-host/tls-free-review；新ARM64 Debug/Release库与原失败库分目录，实机结果保存在 wipe-device-fixed：按需Debug/Release各1351语义断言及6408次退出观察、legacy各1338语义断言均通过，所有进程退出0。必须同时满足全部断言与进程退出0，原失败记录不覆盖、不计通过。
