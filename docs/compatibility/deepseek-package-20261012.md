# DeepSeek 修复安装包更新（2026-10-12）

> 状态：Completed（Linux amd64 安装包生成与校验）；用户原会话测试待验收
> 负责人：Mirage 维护者
> 工作项：M6-39
> 依据：[DEC-043](../decisions/DEC-043-provider-presets-and-anthropic.md)、[DEC-051](../decisions/DEC-051-native-motion-and-model-discovery.md)

## 交付

维护者要求更新安装包并自行测试。`out/packages/mirage_0.1.0_amd64.deb` 已替换为包含
DeepSeek 工具名往返映射与模型列表路径修复的 Native Release 包。版本仍为 0.1.0，
包大小 55,058,546 字节，SHA-256 为
`df90dbab9577b7cea1dfdc7bf765041e79d36c5ccac0e61f27528b3e3c9470ad`。

原构建缓存引用的 ncnn 源码目录已恢复：从官方仓库获取 Mirador 既有锁定提交
`e54f7b1f88434e1d844ea0551b880a1cfb079ce1`，验证提交和源码工作树后恢复同一构建路径。
没有升级依赖 pin；PP-OCR、PipeWire 与内置视觉资源继续随包交付。

## 验证

- `cmake --preset native-release` 配置成功，依赖锁与视觉资源校验通过。
- `cmake --build --preset native-release --target mirage mirage-service mirage-tray mirage-native native_agent_integration_test -j 3` 构建成功。
- `ctest --test-dir build/native-release -R '^native_agent_integration_test$' --output-on-failure`：3/3 通过（集成用例及状态 setup/cleanup）。
- `cpack --config build/native-release/CPackConfig.cmake -B build/native-release/deepseek-package` 生成 DEB。
- 解包校验：四个可执行 ELF 的 SHA-256 与对应 Native Release 程序一致；五项 OCR 资源的字节数及 SHA-256 与 manifest、资源锁一致。
- 校验通过后原子替换 `out/packages` 中的 DEB；此前安装包保存在忽略的构建目录中。

包哈希、四程序哈希与验证摘要见 [summary.json](data/deepseek-package-20261012/summary.json)。
本轮没有安装或启动产品，没有再次使用模型 Key。用户原会话测试由维护者安装并重新启动
Mirage 后执行；该项尚未标记完成。未提交或推送源码，Windows 安装包不在本轮交付范围。
