# Native-release 完整重建与回归结果

> 日期：2026-10-15
> 负责人：Mirage 维护者
> 工作项：M8-12（完整产品）、M8-05（生命周期）、M9-14（展示预算）
> 状态：构建与产物校验通过；两项附加回归未通过，不作完整运行验收声明
> 源提交：`6402da6ec528682a9ed9b1ea44cc200d26c402a1`

维护者已推进部分修复，本轮按要求从空 `build/native-release` 重新配置并构建。
只读使用当前源码与 pinned 依赖，没有修改产品代码、用户会话、配置或输入法。
构建前没有运行中的 Mirage 产品进程；本轮只在隔离环境启动测试进程，没有打开
用户产品。既有桌面入口指向本次更新的相同路径，无需重新注册。

## 构建和产物

```bash
cmake --preset native-release -DMIRAGE_FETCH_DEPENDENCIES=OFF
cmake --build --preset native-release --target mirage-native-product \
  conversation_budget_test session_client_test native_agent_integration_test \
  native_chat_model_test pet_animation_test pet_resource_loader_test \
  pet_frames_test pet_frame_presentation_test tray_runtime_test -j 6
```

两条命令 exit 0。Release、native、pet、PP-OCR 和视觉资源包均开启；配置确认
X11、AT-SPI2、Wayland ScreenCast/PipeWire 可用。Mira pin 为 `a29374a`，
Mirador `ae8cf87`，EUI `89c2d0f`；没有升级依赖或修改 lock。

五个可执行文件存在、可执行且 `ldd` 无缺失依赖：

| 路径（相对 build/native-release） | SHA-256 |
| --- | --- |
| `apps/mirage` | `24c635989884699ae08444b5c3b524a76580d7734ba1c21680411de5f96948ad` |
| `apps/mirage-service` | `b692ba7d1e4c896b0e9dc91336c0d6301ee52894e6882457465521e9f7d737d7` |
| `apps/tray/mirage-tray` | `56eb1391013cecc88752a3f33926c1ad93670f255976e0fb9182d7bdab11cc93` |
| `apps/native/mirage-native` | `9ee19759aeb015fb4ebfea119892fc5e7e3da0bb0fdbf294bab1c2939bca3973` |
| `apps/pet/mira_pet` | `c2c4e4ebce46da1c6f3fdef2e6f3ba7d9573998fb27d69a99991f9b7703ed7be` |

完整摘要与字节数在 `/tmp/mirage-native-release-20261015-artifacts.json`；以上摘要来自该
机器生成报告。Native/pet 字体存在；输出目录九套 pet 素材与锁定 manifest
逐一 SHA-256 相符。OCR 输出的摘要和长度与 `packaging/vision/resources.lock.json`
逐一相符。构建日志：`/tmp/mirage-native-release-20261015-{configure,build}.log`。

## 回归结果

```bash
ctest --test-dir build/native-release \
  -R '^(conversation_budget_test|native_chat_model_test|native_agent_integration_test|pet_animation_test|pet_resource_loader_test|pet_frames_test|pet_frame_presentation_test|tray_runtime_test)$' \
  --output-on-failure
python3 tests/manual/pet_preview_acceptance.py --build build/native-release \
  --output /tmp/mirage-native-release-pet-20261015
```

- CTest **10/10 通过**，含八项实际测试及 setup/cleanup，13.59 秒。日志为
  `/tmp/mirage-native-release-20261015-tests.log`。
- `session_client_test` 在临时 XDG 目录单独执行，外层 `timeout` 上限 45 秒；实际
  exit 1，**138 项检查中 1 项失败**，并非触及外层期限。失败于
  `scenario_timeout_closes_session_fail_closed` 中的
  `session.diagnostic.find("timed out") != std::string::npos`。需按 DEC-094 核对
  新契约、迟到响应及原回归断言，不能仅凭当前结果判断只是旧测试未同步。
  日志：`/tmp/mirage-native-release-20261015-session-client.log`。
- 私有 Xvfb/DBus 桌宠验收完成 **26 项，25 项通过、1 项失败**：
  `hidden graceful exit stays within owner budget`。受管启动/readiness、像素/透明、
  动画推进、拖动/位置、独立停止/重启及崩溃后显式恢复均到达通过点；隐藏后的
  正常退出未满足 `tray.returncode == 0 && elapsed < 6s`。脚本在此终止，后续
  socket 回收及异常 owner 退出场景未执行，不填写原有完整 32/32。
  当前 `check_hidden_exit()` 只在 30 秒 refresh 回调中调用，是后续检查重点；
  本轮没有放宽期限或修改退出实现。日志及结果：
  `/tmp/mirage-native-release-20261015-pet.log`、
  `/tmp/mirage-native-release-pet-20261015/pet-results.json`。

本轮没有新增产品行为，不另建 sanitizer 目录；没有执行 ASAN/UBSAN/TSAN、真实
桌面输入、真实供应商调用或 Windows 验收。负责人为 Mirage 维护者；两项失败
修复并同步测试后，按上述 release 命令补跑，涉及并发/关闭的修改再补 sanitizer。
M8-05/12 与 M9 对应未完成项维持开放。
