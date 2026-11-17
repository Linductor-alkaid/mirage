// Private native view, included after shared page helpers in app.cpp.
void workspace_switch(eui::Ui &ui, float width, const Palette &p) {
    for (int i = 0; i < 2; ++i) {
        const bool selected = state().workflow_page == (i == 1);
        const float w = (width - 56) / 2;
        control_button(ui, i ? "workspace.workflow" : "workspace.chat")
            .position(24 + static_cast<float>(i) * (w + 8), 76)
            .size(w, 36)
            .text(i ? "工作流" : "会话")
            .fontSize(scaled_em(14))
            .style(button_style(p, selected))
            .onClick([i] {
                if (state().workflow_page != (i == 1))
                    toggle_workflows();
            })
            .build();
    }
}
void workflow_sidebar(eui::Ui &ui, float width, float height, const Palette &p,
                      const components::theme::ThemeColorTokens &tokens) {
    auto &s = state();
    control_button(ui, "workflow.new")
        .position(24, 128)
        .size(width - 48, 36)
        .text("新建工作流")
        .icon(icons::add)
        .fontSize(scaled_em(14))
        .style(button_style(p))
        .disabled(s.workflow_editor.pending)
        .onClick([] {
            state().workflow_runs = false;
            const bool fresh = !state().workflow_editor.pending && !state().workflow_editor.dirty();
            state().workflow_editor.create();
            if (!fresh)
                return;
            if (auto request = state().workflow_editor.tools())
                send_workflow_editor(std::move(*request));
        })
        .build();
    text(ui, "workflow.assets.label", "工作流资产", 36, 186, width - 100, 24, 12, p.muted);
    icon_button(ui, "workflow.assets.refresh", icons::refresh, width - 60, 180, p,
                refresh_workflows, false, s.workflows.busy());
    components::scrollView(ui, "workflow.assets")
        .position(24, 224)
        .size(width - 48, std::max(80.0f, height - 372))
        .gap(8)
        .theme(tokens)
        .scrollbarWidth(4)
        .scrollbarGap(4)
        .content([&](eui::Ui &list, float w, float) {
            if (s.workflows.catalog.empty())
                list.text("workflow.assets.empty")
                    .size(w, 72)
                    .text("还没有工作流\n新建或从已完成任务中提取。")
                    .wrap()
                    .lineHeight(22)
                    .fontSize(scaled_em(13))
                    .color(p.muted)
                    .build();
            for (const auto &asset : s.workflows.catalog) {
                const auto key = "workflow.asset." + asset.workflow_id;
                list.stack(key)
                    .size(w, 58)
                    .content([&] {
                        list.rect(key + ".hit")
                            .size(w, 58)
                            .radius(7)
                            .states(s.workflow_editor.document.id == asset.workflow_id
                                        ? p.selected
                                        : eui::Color{0, 0, 0, 0},
                                    p.hover, p.selected)
                            .focusable()
                            .cursor(eui::CursorShape::Hand)
                            .onClick([id = asset.workflow_id, digest = asset.head_digest] {
                                open_workflow(id, digest);
                            })
                            .build();
                        icon(list, key + ".icon", icons::part_tool, 10, 8, 16, 22, p.muted);
                        text(list, key + ".name", fitted_title(asset.name, w - 46, 14), 36, 6,
                             w - 46, 24, 14, p.text, 500);
                        text(list, key + ".state", asset.runnable ? "已发布" : "草稿", 36, 32,
                             w - 46, 20, 12, p.text);
                    })
                    .build();
                keyboard_activation(list, key + ".hit");
            }
        })
        .build();
    control_button(ui, "workflow.runs.open")
        .position(24, height - 132)
        .size(width - 48, 36)
        .text(s.workflow_runs ? "返回流程图" : "运行与恢复")
        .icon(icons::session_running)
        .fontSize(scaled_em(14))
        .style(button_style(p))
        .onClick([] { state().workflow_runs = !state().workflow_runs; })
        .build();
}
void workflow_canvas(eui::Ui &ui, const eui::Screen &screen, float x, float width, const Palette &p,
                     const components::theme::ThemeColorTokens &tokens) {
    auto &s = state();
    auto &editor = s.workflow_editor;
    const bool enabled = !editor.pending && !editor.stale && editor.supported;
    auto button = [&](const std::string &id, const std::string &label, float bx, float by, float bw,
                      bool active, auto action, bool filled = false) {
        auto style = button_style(p, filled);
        if (!active) {
            style.text = style.icon = p.muted;
            style.normal = filled ? p.hover : eui::Color{0, 0, 0, 0};
            style.hover = style.pressed = style.normal;
        }
        control_button(ui, id)
            .position(bx, by)
            .size(bw, 34)
            .text(label)
            .fontSize(scaled_em(13))
            .style(style)
            .disabled(!active)
            .onClick(action)
            .build();
    };
    const std::string page_title =
        editor.document.id.empty() ? "把重复工作，连接起来" : editor.document.name;
    text(ui, "workflow.canvas.title", fitted_title(page_title, width - 260, 20), x, 80, width - 260,
         32, 20, p.text, 600);
    button("workflow.editor.discard", "丢弃修改", x + width - 252, 80, 80,
           !editor.pending && editor.dirty(), [] { state().workflow_editor.discard(); });
    button("workflow.editor.save", "保存", x + width - 164, 80, 60,
           enabled && !editor.document.id.empty(), [] {
               if (auto request = state().workflow_editor.save(false))
                   send_workflow_editor(std::move(*request));
           });
    button(
        "workflow.editor.publish", "验证发布", x + width - 96, 80, 96,
        enabled && !editor.document.id.empty(),
        [] {
            if (auto request = state().workflow_editor.save(true))
                send_workflow_editor(std::move(*request));
        },
        true);
    std::string detail = editor.pending           ? "正在与服务同步…"
                         : !editor.notice.empty() ? editor.notice
                         : editor.document.id.empty() ? "从左侧选择工作流，或创建一个新的任务流程。"
                         : editor.dirty() ? "有未保存的修改 · 发布后可运行并被其他工作流复用"
                                          : "顺序执行 · 条件验证 · 有界回环 · 被调用流程复用";
    if (!editor.capability_confirmed)
        detail += " 持久化状态待服务确认。";
    else
        detail +=
            editor.durable ? " 已保存的工作流可在服务重启后恢复。" : " 当前目录不跨服务重启保留。";
    ui.text("workflow.editor.notice")
        .position(x, 122)
        .size(width, 46)
        .text(detail)
        .wrap()
        .lineHeight(21)
        .fontSize(scaled_em(13))
        .color(p.muted)
        .build();
    if (editor.document.id.empty()) {
        icon(ui, "workflow.canvas.empty.icon", icons::part_tool, x, 226, 32, 42, p.muted);
        text(ui, "workflow.canvas.empty.title", "从一个小任务开始", x, 286, width, 32, 22, p.text,
             600);
        ui.text("workflow.canvas.empty.help")
            .position(x, 334)
            .size(std::min(460.0f, width), 88)
            .text("将原子任务连成工作流，再把常用的任务段提取出来。\n嵌套后仍能进入被调用流程，查看"
                  "每个条件与动作。")
            .wrap()
            .lineHeight(26)
            .fontSize(scaled_em(14))
            .color(p.muted)
            .build();
        button("workflow.canvas.refresh", "刷新目录", x, 452, 112, !s.workflows.busy(),
               refresh_workflows);
        return;
    }
    const float inspector = std::clamp(width * 0.43f, 248.0f, 340.0f);
    const float graph_w = width - inspector - 32, ix = x + graph_w + 32;
    ui.rect("workflow.canvas.divider")
        .position(ix - 16, 180)
        .size(1, std::max(100.0f, screen.height - 204))
        .color(p.border)
        .build();
    if (!editor.return_path.empty())
        button("workflow.editor.back", "返回调用来源", x, 176, 112,
               !editor.pending && !editor.dirty(), [] { state().workflow_editor.back(); });
    else
        text(ui, "workflow.graph.heading", "流程结构", x, 176, graph_w, 28, 13, p.muted);
    button("workflow.editor.add", "添加任务", x + std::max(0.0f, graph_w - 98), 176, 98, enabled,
           [] {
               state().workflow_replace_atom = false;
               state().workflow_atom_picker = !state().workflow_atom_picker;
               if (state().workflow_atom_picker)
                   if (auto request = state().workflow_editor.tools())
                       send_workflow_editor(std::move(*request));
           });
    components::scrollView(ui, "workflow.graph.scroll")
        .position(x, 224)
        .size(graph_w, std::max(120.0f, screen.height - 248))
        .gap(0)
        .offset(s.workflow_graph_scroll)
        .onChange([](float offset) { state().workflow_graph_scroll = offset; })
        .theme(tokens)
        .scrollbarWidth(4)
        .scrollbarGap(8)
        .content([&](eui::Ui &graph, float w, float) {
            graph.stack("workflow.graph")
                .size(w, std::max(200.0f,
                                  104.0f * static_cast<float>(editor.document.nodes.size()) + 80))
                .content([&] {
                    const auto trace = mix_color(p.border, p.muted, 0.3f);
                    const float rail = 20;
                    graph.rect("workflow.graph.rail")
                        .position(rail, 24)
                        .size(2, std::max(0.0f, 104.0f * static_cast<float>(
                                                             editor.document.nodes.size())))
                        .color(trace)
                        .build();
                    text(graph, "workflow.graph.start", "开始", 38, 0, w - 42, 24, 12, p.muted);
                    for (std::size_t i = 0; i < editor.document.nodes.size(); ++i) {
                        const auto &node = editor.document.nodes[i];
                        const auto key = "workflow.node." + std::to_string(i);
                        const float y = 36 + static_cast<float>(i) * 104;
                        const bool selected = editor.selected == i;
                        graph.rect(key + ".station")
                            .position(15, y + 27)
                            .size(12, 12)
                            .radius(6)
                            .color(selected ? p.accent : p.muted)
                            .build();
                        graph.rect(key + ".connector")
                            .position(27, y + 32)
                            .size(17, 1)
                            .color(trace)
                            .build();
                        graph.rect(key + ".hit")
                            .position(44, y)
                            .size(w - 48, 82)
                            .radius(10)
                            .states(selected ? p.selected : p.surface, p.hover, p.selected)
                            .focusable()
                            .cursor(eui::CursorShape::Hand)
                            .onClick([i] { state().workflow_editor.select(i); })
                            .build();
                        keyboard_activation(graph, key + ".hit");
                        icon(graph, key + ".icon",
                             node.called_workflow_id.empty() ? icons::part_tool
                                                             : icons::open_manifest,
                             58, y + 12, 16, 22, selected ? p.text : p.muted);
                        text(
                            graph, key + ".name",
                            fitted_title(node.name.empty() ? "未命名任务" : node.name, w - 110, 14),
                            82, y + 11, w - 110, 24, 14, p.text, 500);
                        const auto kind = !node.called_workflow_id.empty() ? "调用 · 最新发布"
                                          : node.kind == "verify"   ? "验证条件"
                                          : node.kind == "control"  ? "有界回环"
                                          : node.kind == "navigate" ? "导航"
                                                                    : "原子任务";
                        const float kind_width = node.called_workflow_id.empty() ? w - 76 : w - 174;
                        const auto kind_text =
                            !node.called_workflow_id.empty() && w < 340
                                ? std::string{"调用 · 最新"}
                                : std::string{kind} + (node.precondition.empty() ? "" : " · 条件") +
                                      (node.verification.empty() ? "" : " · 验证");
                        text(graph, key + ".kind", fitted_title(kind_text, kind_width, 12), 58,
                             y + 46, kind_width, 22, 12, p.text);
                        if (!node.called_workflow_id.empty())
                            control_button(graph, key + ".open")
                                .position(w - 108, y + 45)
                                .size(52, 28)
                                .text("进入")
                                .fontSize(scaled_em(12))
                                .style(button_style(p))
                                .disabled(!enabled || editor.dirty())
                                .onClick(
                                    [id = node.called_workflow_id] { open_workflow(id, {}, true); })
                                .build();
                        if (!node.fallback_to.empty()) {
                            for (std::size_t j = i + 1; j < editor.document.nodes.size(); ++j)
                                if (editor.document.nodes[j].id == node.fallback_to) {
                                    const float bottom = 36 + static_cast<float>(j) * 104 + 72;
                                    graph.rect(key + ".fallback")
                                        .position(w - 2, y + 72)
                                        .size(1, bottom - y - 72)
                                        .color(p.warning)
                                        .build();
                                    graph.rect(key + ".fallback.top")
                                        .position(w - 10, y + 72)
                                        .size(8, 1)
                                        .color(p.warning)
                                        .build();
                                    graph.rect(key + ".fallback.bottom")
                                        .position(w - 10, bottom)
                                        .size(8, 1)
                                        .color(p.warning)
                                        .build();
                                }
                        }
                        if (!node.jump_to.empty()) {
                            for (std::size_t j = 0; j < i; ++j)
                                if (editor.document.nodes[j].id == node.jump_to) {
                                    const float top = 36 + static_cast<float>(j) * 104 + 33;
                                    graph.rect(key + ".loop")
                                        .position(3, top)
                                        .size(1, y + 33 - top)
                                        .color(p.accent)
                                        .build();
                                    graph.rect(key + ".loop.top")
                                        .position(3, top)
                                        .size(12, 1)
                                        .color(p.accent)
                                        .build();
                                    graph.rect(key + ".loop.bottom")
                                        .position(3, y + 33)
                                        .size(12, 1)
                                        .color(p.accent)
                                        .build();
                                }
                        }
                    }
                    text(graph, "workflow.graph.end", "结束", 38,
                         40 + static_cast<float>(editor.document.nodes.size()) * 104, w - 42, 24,
                         12, p.muted);
                })
                .build();
        })
        .build();
    text(ui, "workflow.inspector.title",
         s.workflow_atom_picker ? "添加原子任务 / 被调用流程" : "节点设置", ix, 178, inspector, 28,
         14, p.text, 600);
    auto style = components::InputStyle(tokens);
    style.background = style.focused = p.surface;
    style.text = p.text;
    style.placeholder = p.muted;
    style.border = p.border;
    style.focusBorder = p.action;
    style.cursor = p.text;
    style.shadow = {};
    style.radius = 7;
    components::scrollView(ui, "workflow.inspector.scroll")
        .position(ix, 224)
        .size(inspector, std::max(120.0f, screen.height - 248))
        .gap(10)
        .offset(s.workflow_inspector_scroll)
        .onChange([](float offset) { state().workflow_inspector_scroll = offset; })
        .theme(tokens)
        .scrollbarWidth(4)
        .scrollbarGap(8)
        .content([&](eui::Ui &list, float w, float) {
            auto label = [&](const std::string &id, const std::string &value, float h = 22) {
                list.text(id)
                    .size(w, h)
                    .text(value)
                    .wrap()
                    .lineHeight(20)
                    .fontSize(scaled_em(12))
                    .color(p.muted)
                    .build();
            };
            if (s.workflow_atom_picker) {
                label("workflow.atoms.help", "执行时使用被调用工作流的最新发布版本。", 44);
                for (std::size_t i = 0; i < editor.atoms.size(); ++i) {
                    const auto tool = editor.atoms[i];
                    auto title = tool.wire_name;
                    if (title.starts_with("workflow."))
                        for (const auto &asset : s.workflows.catalog)
                            if (title.substr(9, 32) == asset.workflow_id)
                                title = "被调用流程 · " + asset.name;
                    control_button(list, "workflow.atom." + std::to_string(i))
                        .size(w, 36)
                        .text(fitted_title(title, w - 16, 12))
                        .fontSize(scaled_em(12))
                        .style(button_style(p))
                        .disabled(!enabled)
                        .onClick([tool] {
                            if (state().workflow_replace_atom)
                                state().workflow_editor.replace(tool);
                            else
                                state().workflow_editor.append(tool);
                            state().workflow_atom_picker = false;
                        })
                        .build();
                }
                return;
            }
            auto input = [&](const std::string &id, const std::string &caption,
                             const std::string &value, auto change) {
                label(id + ".label", caption);
                if (!enabled) {
                    list.text(id + ".readonly")
                        .size(w, 36)
                        .text(fitted_title(value, w, 13))
                        .fontSize(scaled_em(13))
                        .color(p.muted)
                        .build();
                    return;
                }
                components::input(list, id)
                    .size(w, 36)
                    .value(value)
                    .placeholder("")
                    .inset(control_text_inset)
                    .fontSize(scaled_em(13))
                    .style(style)
                    .onChange([change, enabled](const std::string &updated) {
                        if (enabled && updated.size() <= 65536)
                            change(updated);
                    })
                    .build();
            };
            if (editor.document.nodes.empty()) {
                label("workflow.inspector.empty", "点击「添加任务」，选择一个原子任务或工作流。",
                      66);
                return;
            }
            if (!editor.node.called_workflow_id.empty())
                control_button(list, "workflow.node.enter")
                    .size(w, 34)
                    .text("进入工作流")
                    .icon(icons::open_manifest)
                    .fontSize(scaled_em(13))
                    .style(button_style(p))
                    .disabled(!enabled || editor.dirty())
                    .onClick([] {
                        const auto node = state().workflow_editor.node;
                        open_workflow(node.called_workflow_id, {}, true);
                    })
                    .build();
            input("workflow.node.name", "任务名称", editor.node.name, [](const auto &v) {
                auto &e = state().workflow_editor;
                e.node.name = v;
                e.node_changed = true;
            });
            if (editor.node.kind == "control")
                input("workflow.node.iterations", "回环次数上限（1–64）",
                      std::to_string(editor.node.max_iterations), [](const auto &value) {
                          unsigned count = 0;
                          const auto parsed =
                              std::from_chars(value.data(), value.data() + value.size(), count);
                          if (parsed.ec == std::errc{} &&
                              parsed.ptr == value.data() + value.size() && count >= 1 &&
                              count <= 64) {
                              auto &e = state().workflow_editor;
                              e.node.max_iterations = count;
                              e.node_changed = true;
                          }
                      });
            label("workflow.node.arguments.label", "执行参数（JSON）");
            if (enabled)
                components::input(list, "workflow.node.arguments")
                    .size(w, 92)
                    .value(editor.node.arguments)
                    .placeholder("输入 JSON 对象")
                    .multiline(true)
                    .scrollbar(true)
                    .inset(control_text_inset)
                    .fontSize(scaled_em(13))
                    .style(style)
                    .onChange([](const std::string &value) {
                        if (value.size() > 65536)
                            return;
                        auto &e = state().workflow_editor;
                        e.node.arguments = value;
                        e.node_changed = true;
                    })
                    .build();
            else
                list.text("workflow.node.arguments.readonly")
                    .size(w, 92)
                    .text(editor.node.arguments)
                    .fontSize(scaled_em(13))
                    .lineHeight(20)
                    .wrap()
                    .color(p.muted)
                    .build();
            if (editor.node.kind == "tool_call")
                control_button(list, "workflow.node.replace")
                    .size(w, 34)
                    .text("更换原子任务 / 被调用流程")
                    .fontSize(scaled_em(13))
                    .style(button_style(p))
                    .disabled(!enabled)
                    .onClick([] {
                        state().workflow_replace_atom = true;
                        state().workflow_atom_picker = true;
                        if (auto request = state().workflow_editor.tools())
                            send_workflow_editor(std::move(*request));
                    })
                    .build();
            auto condition = [&](const std::string &key, const std::string &heading,
                                 bool validation) {
                auto &predicate = validation ? editor.node.validation : editor.node.condition;
                label(key + ".heading", heading, 28);
                control_button(list, key + ".toggle")
                    .size(w, 32)
                    .text(predicate.enabled ? "已启用 · 点击移除" : "添加条件")
                    .fontSize(scaled_em(12))
                    .style(button_style(p))
                    .disabled(!enabled)
                    .onClick([validation] {
                        auto &e = state().workflow_editor;
                        auto &c = validation ? e.node.validation : e.node.condition;
                        c.enabled = !c.enabled;
                        c.edited = e.node_changed = true;
                    })
                    .build();
                if (!predicate.enabled)
                    return;
                input(key + ".signal", "信号（如 run_parameter:ready）", predicate.signal,
                      [validation](const auto &v) {
                          auto &e = state().workflow_editor;
                          auto &c = validation ? e.node.validation : e.node.condition;
                          c.signal = v;
                          c.edited = e.node_changed = true;
                      });
                input(key + ".op", "比较方式（eq / ne / lt / gt / exists）", predicate.op,
                      [validation](const auto &v) {
                          auto &e = state().workflow_editor;
                          auto &c = validation ? e.node.validation : e.node.condition;
                          c.op = v;
                          c.edited = e.node_changed = true;
                      });
                input(key + ".value", "比较值（文字加引号；数字或 true / false）", predicate.value,
                      [validation](const auto &v) {
                          auto &e = state().workflow_editor;
                          auto &c = validation ? e.node.validation : e.node.condition;
                          c.value = v;
                          c.edited = e.node_changed = true;
                      });
            };
            condition("workflow.condition", "执行条件", false);
            condition("workflow.validation", "完成验证", true);
            input("workflow.node.recovery", "失败处理（JSON，可选）", editor.node.recovery,
                  [](const auto &v) {
                      auto &e = state().workflow_editor;
                      e.node.recovery = v;
                      e.node_changed = true;
                  });
            control_button(list, "workflow.node.apply")
                .size(w, 34)
                .text("应用到草稿")
                .fontSize(scaled_em(13))
                .style(button_style(p))
                .disabled(!enabled || !editor.node_changed)
                .onClick([] { state().workflow_editor.apply_node(); })
                .build();
            input("workflow.document.name", "工作流名称", editor.name,
                  [](const auto &v) { state().workflow_editor.name = v; });
            input("workflow.document.summary", "用途说明", editor.summary,
                  [](const auto &v) { state().workflow_editor.summary = v; });
            label("workflow.segment.heading", "复用任务段", 28);
            control_button(list, "workflow.segment.mark")
                .size(w, 34)
                .text(editor.segment_marked ? "已标记起点 · 选择结束节点"
                                            : "以当前节点作为任务段起点")
                .fontSize(scaled_em(12))
                .style(button_style(p))
                .disabled(!enabled)
                .onClick([] {
                    auto &e = state().workflow_editor;
                    e.segment_first = e.selected;
                    e.segment_marked = true;
                })
                .build();
            control_button(list, "workflow.segment.extract")
                .size(w, 34)
                .text("提取所选任务段为工作流")
                .fontSize(scaled_em(12))
                .style(button_style(p))
                .disabled(!enabled || !editor.segment_marked)
                .onClick([] { state().workflow_editor.extract(); })
                .build();
        })
        .build();
}
