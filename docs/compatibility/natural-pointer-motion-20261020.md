# 有界随机轨迹 v2 验收（2026-10-20）

> 状态：Linux 限定实现与构建验收完成；真实桌面模型/手感验收开放
> 工作项：M7-25/26/31；[DEC-110](../decisions/DEC-110-continuous-input-and-device-compatibility.md)
> 基线：6a4e6ab + feat/desktop-input-policy 工作树
> 负责人：Mirage 维护者

## 契约与边界

模型三条物理点击工具保持 4.0.0，只声明意图。pointer_motion_policy 独立版本升到 2，
held-session budget_version 保持 1。`smooth_move_to` 增加可选宿主侧
PointerMotionVariation（seed / target_width）；未传入时保留旧确定性直线基线。
C++ 调用方需重编译，自定义 override 需增加 variation 参数并处理或拒绝；测试代理
同步转发 variation，不能丢弃它。

自动接近每次以局部 std::random_device 生成种子，无全局或 thread_local RNG。
纯函数按种子重建计划，std::mt19937_64 的高 53 位显式映射，避免依赖标准分布的
实现差异。这里的固定种子回放针对核心规划器；普通工具回执尚未携带生产种子。±12% 时长变化后截到 120～650 ms；距离/目标宽度公式不变。平滑弧线
最多 min(24,0.08d,0.75w) 像素，末端修正及部分过冲最多 3 像素并随距离/目标宽度缩小；
空间扰动采用实际目标宽度（未知时 32），不套时长的 8 像素下限。末端修正的速度/
加速度在窗口两端均为零。最终采样时间及坐标精确等于计划总时长和目标。X11/Portal 裁剪中间点到可用范围。

不新增线程、队列、定时器或 Executor owner；复用既有有限动作，失败/取消/释放/
落后停止经原路径上报。随机源或准备失败在派发前返回错误，不吞掉平台执行异常。
目标重验、完整点击与释放后才返回的顺序不变。未修改 pinned 依赖。

## 验证范围

- smooth_move_schedule_test：128 个固定种子的时间变化、左右弯曲、部分过冲、精确
  终点、重放一致、节拍/点数上限；零/短距离、窄目标、反向及 INT32 极值。
- visual_glide_tools_test：真实工具默认传递宿主 variation；原目标变化、失败/取消、
  老参数/版本拒绝仍保留；nominal golden 迁入 automatic-pointer-motion-v2.json。
- x11_smooth_move_test：私有 Xvfb、独立连接观察在途弯曲、时间范围和精确终点，
  自动曲线路径移动中取消及屏幕边缘裁剪；保留旧确定性光标路径矩阵。
- portal_input_test：共享流缩放后曲线路径、完整移动中无按钮事件、时间下界、
  窄目标/屏幕边缘及最终精确落点；保留原取消/撤权/释放/落后/拒绝矩阵。
- 全量 Debug 与定向 sanitizer、native-release 用于接口变更和产品重编译验证。

实际执行命令：

```bash
cmake --build --preset debug -j 4
ctest --preset debug --output-on-failure -j 4
cmake --build --preset <asan或ubsan或tsan> -j 2 --target smooth_move_schedule_test visual_glide_tools_test portal_input_test x11_smooth_move_test desktop_image_supply_test
ctest --preset <asan或ubsan> -R '^(smooth_move_schedule|visual_glide_tools|portal_input|x11_smooth_move|desktop_image_supply)_test$' --output-on-failure -j 3
setarch x86_64 -R ctest --preset tsan -R '^(smooth_move_schedule|visual_glide_tools|portal_input|x11_smooth_move|desktop_image_supply)_test$' --output-on-failure -j 3
cmake --build --preset native-release -j 2 --target mirage-native-product smooth_move_schedule_test visual_glide_tools_test portal_input_test x11_smooth_move_test desktop_image_supply_test visual_marks_test gui_fallback_test local_visual_workflow_test
ctest --preset native-release -R '^(smooth_move_schedule|visual_glide_tools|portal_input|x11_smooth_move|desktop_image_supply|visual_marks|gui_fallback|local_visual_workflow)_test$' --output-on-failure -j 3
```

最终 ASan、UBSan、TSan 定向各 **7/7**（5 个用例 + setup/cleanup），无 sanitizer/
race 报告。固定种子属性及旧基线合计 **18559** 检查通过；旧负坐标半像素取整
保持兼容。native-release 完整 product 构建通过（CLI/服务/托盘/native/pet），相关 **10/10**
（8 个用例 + setup/cleanup）通过，CLI --version 可运行。Debug 全量 **101/101** 通过。

初轮扩展 X11 取消用例暴露旧“y 不离开直线”断言，按新曲线走廊修正；未删除
取消时长、副作用或不回放检查。补齐窄目标断言后，空间扰动按实际宽度收紧，与
时长的 8 像素下限分离；最终统一重新构建后回归，不使用开发中旧库的测试结果。


本机未发现运行中的 Mirage 产品实例，本轮没有启动/重启产品或操作真实桌面。
文档本地链接 124 项、JSON golden 与 git diff --check 通过；third_party/ 和依赖锁未变。

## 尚未执行

真实 GNOME/其他 compositor 的在线模型完整点击、人工手感及人类数据校准未执行。
负责人 Mirage 维护者；补跑条件为授权桌面、公开测试应用与模型就绪后，预登记任务/
阈值并按 M7-31 取证。私有 X11 和 Portal Notify seam 不替代真实平台验收。
