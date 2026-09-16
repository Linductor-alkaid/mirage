/// 进程级外观单例：视图与组件只经它读写外观，不直接触碰挂载细节。

import { ThemeManager } from './theme-manager.js';

export const themeManager = new ThemeManager();
