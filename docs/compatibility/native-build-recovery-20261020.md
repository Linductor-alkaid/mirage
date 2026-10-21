# native-release 运行目录误删后的恢复（2026-10-20）

> 状态：本机恢复与真实窗口验收完成
> 工作项：M6-04/M6-12 产品资源与本机交付维护
> 依据：DEC-037/040/043/045；维护者明确授权恢复
> 负责人：Mirage 维护者

## 故障证据及范围

维护者报告 native 前台文字无法渲染、Mira pet 停止动画。检查时 tray/native/pet
三个进程仍存活，但 /proc/<pid>/exe 均指向本仓库 build/native-release 下已删除
的文件，pet 的 cwd 也已删除；整个 build/ 不存在。桌面入口仍指向该目录。
维护者确认该目录刚被另一会话误清理。随后旧产品进程已退出，未强制终止它们。

字体、图标及九套 WebP 在运行时从目标 assets 读取；删除运行目录后，已加载程序
仍可短暂存活，但资源不可再加载，pet 可进入静态失败回退。本次恢复现有完整产品
及资源，不修改渲染/动画实现或 pinned 依赖，不回退到旧 DEB，不更改模型与权限配置。
恢复前记录 19 个会话/状态文件的摘要用于比对，不保存用户对话正文或凭据。

## 恢复与验证

```bash
cmake --preset native-release
cmake --build --preset native-release -j 6 --target mirage-native-product mirage-native-register-desktop pet_texture_test pet_animation_test pet_frame_presentation_test pet_resource_loader_test
```

从当前工作树重建，保留此前 DEC-056/110 与有界随机轨迹更新。preset 启用完整
native/pet、PP-OCR 和锁定视觉资源；重新校验 submodule、字体归档/字体、WebP 清单
及视觉模型摘要。完整构建无缓存，不以仅复制前端可执行文件替代产品恢复。

完整产品构建及桌面入口注册成功。补充并执行：

```bash
cmake --build --preset native-release -j 4 --target pet_frames_test
ctest --preset native-release -R '^(pet_texture|pet_animation|pet_frame_presentation|pet_resource_loader|pet_frames|bundled_vision_resources)_test$' --output-on-failure -j 3
./build/native-release/apps/mirage start --shell /home/linductor/mirage/build/native-release/apps/native/mirage-native --tray /home/linductor/mirage/build/native-release/apps/tray/mirage-tray
./build/native-release/apps/mirage service status
./build/native-release/apps/mirage task list
./build/native-release/apps/mirage pet status
```

定向 **8/8**（6 个用例 + setup/cleanup）通过，无 skip；含全部九套 WebP 的实际
解码、资源加载/取消、动画状态、帧呈现、GPU RGBA 纹理与视觉资源篡改拒绝。
资源摘要对账 **31/31**：native/pet 两份中文字体、九份 WebP、五份锁定 OCR 输出，
以及 native 的 15 个图标/图标字体资源。CLI --version 可运行。

按桌面入口相同参数重新启动 tray/native/pet；IPC 报 frontend_ready=true、
pet_state=running、display_state=idle、active_work=0，task list 为 0。三个运行中的
exe 与 pet cwd 均为已恢复目录，不再是 deleted。开发树中直接运行不带 --shell/--tray
的 CLI start 不能发现子目录内程序，本次按已注册入口的显式路径启动。

对实际 native/pet 窗口使用只读 X11 framebuffer 检查，无键鼠事件或窗口移动。
目视确认中文正文、侧栏、输入提示及图标正常；约 4 秒采样得到 **17** 个不同 pet
图像帧，最多 **62068** 个 RGB 字节相对首帧变化，X11 错误为 0。不是仅以进程存活
代替文字/动画验收。截图只在权限 0700 的私有临时目录使用，不将用户窗口或会话
内容加入仓库。恢复前后的 **19/19** 会话/状态文件摘要一致。

本轮无源码修复及依赖变更；git diff --check 通过。原随机轨迹等未提交工作树改动
保持原样。此次只恢复当前 native-release，未重新生成被删除的其他构建配置。


## 限制

本次是误删构建输出的恢复，不构成新的渲染器/平台兼容性保证，也未执行在线模型
任务、真实桌面点击或 Windows 验收。运行目录仍是开发构建目录，后续清理应先退出
产品；需要独立于构建目录的安装部署时，沿 DEC-031 打包/安装流程处理。
