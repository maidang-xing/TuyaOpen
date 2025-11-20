# UI界面开发指南

本文档详细介绍如何基于模板文件创建新的UI界面，并将其集成到TuyaOpen AI Pocket Pet项目中。

---

## 🎨 开发前推荐：使用 LVGL PC 模拟器

在开始嵌入式UI开发之前，强烈建议先使用 **LVGL PC 模拟器** 进行UI设计和调试。这将大大提高开发效率。

### 推荐工具：lv_port_pc_vscode

**项目地址**: [https://github.com/lvgl/lv_port_pc_vscode](https://github.com/lvgl/lv_port_pc_vscode)

#### 为什么使用 PC 模拟器？

1. **快速迭代** - 在PC上直接运行和调试，无需每次都烧录到硬件
2. **即时预览** - 实时查看UI效果，快速调整布局和样式
3. **调试方便** - 使用VS Code调试器，设置断点、查看变量
4. **跨平台** - 支持 Windows、Linux、macOS
5. **降低成本** - 减少硬件损耗，延长设备寿命

#### 快速开始

```bash
# 克隆项目
git clone https://github.com/lvgl/lv_port_pc_vscode.git
cd lv_port_pc_vscode

# 安装依赖（Ubuntu/Debian）
sudo apt-get install build-essential libsdl2-dev

# 在VS Code中打开项目
code .

# 按 F5 开始调试运行
```

#### 开发流程建议

```
┌─────────────────────┐
│ 1. PC模拟器设计UI   │  ← 在 lv_port_pc_vscode 中开发
│    - 快速原型设计   │
│    - UI布局调试     │
│    - 交互逻辑测试   │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ 2. 迁移到嵌入式项目│  ← 复制代码到本项目
│    - 复制UI代码     │
│    - 适配屏幕尺寸   │
│    - 集成到栈管理   │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ 3. 硬件测试验证     │  ← 在实际硬件上测试
│    - 烧录到设备     │
│    - 性能优化       │
│    - 最终调试       │
└─────────────────────┘
```

#### PC模拟器与本项目的对应关系

| PC模拟器 | 本项目 | 说明 |
|---------|--------|------|
| `main.c` | `app_display.c` | 应用入口 |
| `lv_conf.h` | LVGL配置 | 配置文件 |
| 自定义屏幕代码 | `src/display/ui/xxx_screen.c` | UI实现 |
| SDL2窗口尺寸 | 384x168 | 设备屏幕尺寸 |

#### 示例：在PC模拟器中测试

```c
// 在 lv_port_pc_vscode 项目的 main.c 中
void lv_example_get_started_1(void)
{
    // 创建测试屏幕
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 384, 168);  // 设置为目标设备尺寸
    
    // 添加UI元素
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Test Screen");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    // 加载屏幕
    lv_scr_load(screen);
}
```

测试通过后，将UI代码迁移到本项目的 `template_screen.c` 中。

---

## 目录
- [🎨 开发前推荐：使用 LVGL PC 模拟器](#-开发前推荐使用-lvgl-pc-模拟器)
- [1. 架构概述](#1-架构概述)
- [2. 模板文件详解](#2-模板文件详解)
- [3. 创建新界面的步骤](#3-创建新界面的步骤)
- [4. 集成到项目](#4-集成到项目)
- [5. 最佳实践](#5-最佳实践)
- [6. 常见问题](#6-常见问题)

---

## 1. 架构概述

### 1.1 屏幕管理系统（Screen Manager）

项目采用**基于栈（Stack）的屏幕管理系统**，这是一种优雅且高效的多屏幕导航方案。

#### 栈管理原理

```
栈顶 →  [当前屏幕]     ← 用户看到的界面
        [上一屏幕]     ← screen_back() 返回到这里
        [更早的屏幕]
        ...
栈底 →  [主屏幕]       ← screen_back_bottom() 返回到这里
```

#### 为什么使用栈？

1. **自然的导航逻辑** - 符合"前进-后退"的用户习惯
2. **自动状态管理** - 屏幕状态随栈保存，返回时自动恢复
3. **内存高效** - 只有栈顶屏幕处于激活状态
4. **防止循环引用** - 避免屏幕间的复杂依赖关系

#### 栈操作示例

```c
// 场景：从主屏幕 → 设置屏幕 → 亮度调节屏幕

// 初始状态：栈中只有主屏幕
[main_screen]  ← 栈顶

// 用户点击"设置"按钮，调用：
screen_load(&settings_screen);

// 栈状态变为：
[settings_screen]  ← 栈顶（当前显示）
[main_screen]

// 用户点击"亮度"选项，调用：
screen_load(&brightness_screen);

// 栈状态变为：
[brightness_screen]  ← 栈顶（当前显示）
[settings_screen]
[main_screen]

// 用户按ESC键，调用：
screen_back();

// 栈状态恢复为：
[settings_screen]  ← 栈顶（重新显示）
[main_screen]

// 用户长按HOME键，调用：
screen_back_bottom();

// 栈状态恢复为：
[main_screen]  ← 栈顶（直接返回主屏幕）
```

#### 核心功能

| 功能 | 说明 | 应用场景 |
|-----|------|---------|
| **栈式导航** | 屏幕按序压入/弹出栈 | 多级菜单导航 |
| **过渡动画** | 屏幕切换时的流畅动画 | 提升用户体验 |
| **生命周期管理** | 自动调用 init/deinit | 资源管理 |
| **状态保持** | 可选的状态数据保存 | 保持用户输入 |

### 1.2 核心数据结构

#### Screen_t 结构体

这是整个屏幕管理系统的核心数据结构：

```c
typedef struct {
    void (*init)(void);           // 屏幕初始化函数指针
    void (*deinit)(void);         // 屏幕反初始化函数指针
    lv_obj_t **screen_obj;        // LVGL屏幕对象指针的指针
    char *name;                   // 屏幕名称标识符（用于调试）
    void *state_data;             // 屏幕状态数据指针（可选）
} Screen_t;
```

**字段详解**：

| 字段 | 类型 | 作用 | 注意事项 |
|-----|------|------|---------|
| `init` | 函数指针 | 创建UI元素、注册事件 | 每次屏幕显示时调用 |
| `deinit` | 函数指针 | 清理资源、移除事件 | 每次屏幕隐藏时调用 |
| `screen_obj` | 二级指针 | 指向LVGL屏幕对象 | 必须指向静态变量 |
| `name` | 字符串 | 屏幕标识名称 | 便于调试日志输出 |
| `state_data` | void指针 | 保存屏幕状态数据 | 用于状态保持（可选）|

#### 生命周期图示

```
screen_load() 被调用
        │
        ▼
┌─────────────────┐
│ 1. deinit()     │  ← 反初始化当前屏幕（如果有）
│    旧屏幕       │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ 2. 压入栈       │  ← 将新屏幕压入栈顶
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ 3. init()       │  ← 初始化新屏幕
│    新屏幕       │     - 创建UI对象
└────────┬────────┘     - 注册事件回调
         │               - 启动定时器
         ▼
┌─────────────────┐
│ 4. 屏幕动画     │  ← 滑动动画切换
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ 5. 显示新屏幕   │  ← 用户看到新界面
└─────────────────┘
```

### 1.3 屏幕管理API

#### 核心函数说明

```c
/**
 * @brief 加载新屏幕到栈顶
 * @param newScreen 指向新屏幕的指针
 * 
 * 工作流程：
 * 1. 检查栈是否已满
 * 2. 调用当前屏幕的 deinit()
 * 3. 将新屏幕压入栈
 * 4. 调用新屏幕的 init()
 * 5. 播放切换动画（从左滑入）
 */
void screen_load(Screen_t *newScreen);

/**
 * @brief 返回到上一个屏幕
 * 
 * 工作流程：
 * 1. 调用当前屏幕的 deinit()
 * 2. 从栈中弹出当前屏幕
 * 3. 调用上一屏幕的 init()（重新初始化）
 * 4. 播放切换动画（向右滑出）
 */
void screen_back(void);

/**
 * @brief 返回到栈底的主屏幕
 * 
 * 工作流程：
 * 1. 循环调用 deinit() 清理所有中间屏幕
 * 2. 保留栈底屏幕
 * 3. 重新初始化栈底屏幕
 * 4. 播放切换动画
 */
void screen_back_bottom(void);

/**
 * @brief 获取当前屏幕（栈顶）
 * @return 指向当前屏幕的指针，栈空时返回 NULL
 */
Screen_t* screen_get_now_screen(void);
```

#### 使用示例

```c
// 示例1：加载新屏幕
void button_clicked_handler(lv_event_t *e)
{
    printf("Loading settings screen...\n");
    screen_load(&settings_screen);  // 跳转到设置屏幕
}

// 示例2：返回上一屏幕
void back_button_handler(lv_event_t *e)
{
    printf("Going back...\n");
    screen_back();  // 返回上一个屏幕
}

// 示例3：返回主屏幕
void home_button_handler(lv_event_t *e)
{
    printf("Going to home...\n");
    screen_back_bottom();  // 直接返回主屏幕
}

// 示例4：获取当前屏幕信息
void debug_current_screen(void)
{
    Screen_t *current = screen_get_now_screen();
    if (current) {
        printf("Current screen: %s\n", current->name);
    } else {
        printf("No screen loaded\n");
    }
}
```

---

## 2. 模板文件详解

### 2.1 模板文件位置
- **源文件**: `src/display/ui/template_screen.c`
- **头文件**: `src/display/ui/template_screen.h`

### 2.2 模板结构分析

#### 头文件 (template_screen.h)
```c
#ifndef TEMPLATE_SCREEN_H
#define TEMPLATE_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "screen_manager.h"

// 导出屏幕对象，供其他模块使用
extern Screen_t template_screen;

// 声明初始化和反初始化函数
void template_screen_init(void);
void template_screen_deinit(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*TEMPLATE_SCREEN_H*/
```

#### 源文件 (template_screen.c)

**1. 变量定义**
```c
static lv_obj_t *ui_template_screen;  // LVGL屏幕对象
static lv_timer_t *timer;              // 定时器

// Screen_t 结构体实例
Screen_t template_screen = {
    .init = template_screen_init,
    .deinit = template_screen_deinit,
    .screen_obj = &ui_template_screen,
    .name = "template",
};
```

**2. 初始化函数**
```c
void template_screen_init(void)
{
    // 1. 创建屏幕对象
    ui_template_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_template_screen, 384, 168);
    lv_obj_set_style_bg_color(ui_template_screen, lv_color_white(), 0);

    // 2. 创建UI元素
    lv_obj_t *title = lv_label_create(ui_template_screen);
    lv_label_set_text(title, "TuyaOpen\nLVGL Temp");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    // 3. 创建定时器
    timer = lv_timer_create(template_timer_cb, 1000, NULL);
    
    // 4. 注册键盘事件
    lv_obj_add_event_cb(ui_template_screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
    
    // 5. 添加到LVGL组并设置焦点
    lv_group_add_obj(lv_group_get_default(), ui_template_screen);
    lv_group_focus_obj(ui_template_screen);
}
```

**3. 反初始化函数**
```c
void template_screen_deinit(void)
{
    if (ui_template_screen) {
        // 移除事件回调
        lv_obj_remove_event_cb(ui_template_screen, keyboard_event_cb);
        // 从LVGL组中移除
        lv_group_remove_obj(ui_template_screen);
    }
    if (timer) {
        // 删除定时器
        lv_timer_del(timer);
        timer = NULL;
    }
}
```

**4. 事件处理**
```c
static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    
    switch (key) {
        case KEY_UP:
            // 处理上键
            break;
        case KEY_DOWN:
            // 处理下键
            break;
        case KEY_LEFT:
            // 处理左键
            break;
        case KEY_RIGHT:
            // 处理右键
            break;
        case KEY_ENTER:
            // 处理确认键
            break;
        case KEY_ESC:
            // 处理返回键
            screen_back();
            break;
    }
}
```

---

## 3. 创建新界面的步骤

### 步骤 1: 复制模板文件

```bash
# 在 src/display/ui/ 目录下
cd src/display/ui/
cp template_screen.c my_new_screen.c
cp template_screen.h my_new_screen.h
```

### 步骤 2: 修改头文件

编辑 `my_new_screen.h`：

```c
#ifndef MY_NEW_SCREEN_H
#define MY_NEW_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "screen_manager.h"

// 修改屏幕对象名称
extern Screen_t my_new_screen;

// 修改函数名称
void my_new_screen_init(void);
void my_new_screen_deinit(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*MY_NEW_SCREEN_H*/
```

### 步骤 3: 修改源文件

编辑 `my_new_screen.c`：

**3.1 修改变量名称**
```c
static lv_obj_t *ui_my_new_screen;
static lv_timer_t *timer;

Screen_t my_new_screen = {
    .init = my_new_screen_init,
    .deinit = my_new_screen_deinit,
    .screen_obj = &ui_my_new_screen,
    .name = "my_new",
};
```

**3.2 实现初始化函数**
```c
void my_new_screen_init(void)
{
    // 1. 创建屏幕
    ui_my_new_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_my_new_screen, AI_PET_SCREEN_WIDTH, AI_PET_SCREEN_HEIGHT);
    
    // 2. 设置背景色
    lv_obj_set_style_bg_color(ui_my_new_screen, lv_color_hex(0x000000), 0);
    
    // 3. 创建UI元素
    lv_obj_t *title = lv_label_create(ui_my_new_screen);
    lv_label_set_text(title, "My New Screen");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    
    // 4. 添加更多UI元素...
    
    // 5. 注册事件
    lv_obj_add_event_cb(ui_my_new_screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), ui_my_new_screen);
    lv_group_focus_obj(ui_my_new_screen);
    
    // 6. 创建定时器（如果需要）
    timer = lv_timer_create(my_new_timer_cb, 1000, NULL);
}
```

**3.3 实现反初始化函数**
```c
void my_new_screen_deinit(void)
{
    if (ui_my_new_screen) {
        lv_obj_remove_event_cb(ui_my_new_screen, keyboard_event_cb);
        lv_group_remove_obj(ui_my_new_screen);
    }
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
}
```

**3.4 实现事件处理函数**
```c
static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    
    switch (key) {
        case KEY_UP:
            // 实现上键逻辑
            break;
        case KEY_DOWN:
            // 实现下键逻辑
            break;
        case KEY_ENTER:
            // 实现确认键逻辑
            break;
        case KEY_ESC:
            // 返回上一个屏幕
            screen_back();
            break;
        default:
            break;
    }
}
```

### 步骤 4: 设计UI布局

根据需求添加LVGL控件：

```c
void my_new_screen_init(void)
{
    // ... 创建屏幕代码 ...
    
    // 示例：添加按钮
    lv_obj_t *btn = lv_btn_create(ui_my_new_screen);
    lv_obj_set_size(btn, 100, 40);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click Me");
    lv_obj_center(btn_label);
    
    // 示例：添加滚动列表
    lv_obj_t *list = lv_list_create(ui_my_new_screen);
    lv_obj_set_size(list, 200, 150);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_list_add_text(list, "Item 1");
    lv_list_add_text(list, "Item 2");
    
    // 示例：添加图表
    lv_obj_t *chart = lv_chart_create(ui_my_new_screen);
    lv_obj_set_size(chart, 200, 100);
    lv_obj_align(chart, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
}
```

---

## 4. 集成到项目

### 4.1 添加到CMakeLists.txt

编辑 `CMakeLists.txt`，在源文件列表中添加：

```cmake
set(SOURCES
    # ... 现有文件 ...
    src/display/ui/my_new_screen.c
    # ... 其他文件 ...
)
```

### 4.2 在其他屏幕中调用

在需要跳转到新屏幕的地方，添加头文件包含：

```c
#include "my_new_screen.h"
```

然后使用 `screen_load()` 函数跳转：

```c
// 在某个事件处理函数中
case KEY_ENTER:
    screen_load(&my_new_screen);  // 跳转到新屏幕
    break;
```

### 4.3 从主菜单添加入口

编辑 `main_screen.c`，添加菜单项：

```c
// 在头部添加包含
#include "my_new_screen.h"

// 在菜单选择逻辑中添加
static void menu_selection_event_cb(lv_event_t *e)
{
    // ... 现有代码 ...
    
    if (selected_index == YOUR_MENU_INDEX) {
        screen_load(&my_new_screen);
    }
}
```

### 4.4 完整的调用示例

```c
// 示例：从主屏幕跳转到新屏幕
static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    
    switch (key) {
        case KEY_ENTER:
            printf("Loading my new screen\n");
            screen_load(&my_new_screen);
            break;
        case KEY_ESC:
            printf("Going back\n");
            screen_back();
            break;
    }
}
```

---

## 5. 最佳实践

### 5.1 内存管理

```c
void my_screen_deinit(void)
{
    // 1. 先移除事件回调
    if (ui_my_screen) {
        lv_obj_remove_event_cb(ui_my_screen, keyboard_event_cb);
        lv_group_remove_obj(ui_my_screen);
    }
    
    // 2. 删除定时器
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    
    // 3. 释放动态分配的内存
    if (my_data) {
        free(my_data);
        my_data = NULL;
    }
    
    // 4. LVGL会自动删除屏幕对象及其子对象
}
```

### 5.2 状态保存与恢复

```c
typedef struct {
    int selected_index;
    char input_text[128];
    bool is_playing;
} MyScreenState_t;

static MyScreenState_t *screen_state = NULL;

void my_screen_init(void)
{
    // 如果有保存的状态，恢复它
    if (my_new_screen.state_data) {
        screen_state = (MyScreenState_t *)my_new_screen.state_data;
    } else {
        // 创建新状态
        screen_state = malloc(sizeof(MyScreenState_t));
        screen_state->selected_index = 0;
        screen_state->is_playing = false;
        my_new_screen.state_data = screen_state;
    }
    
    // 使用状态数据初始化UI
    // ...
}

void my_screen_deinit(void)
{
    // 保存状态，不释放（下次可以恢复）
    // 如果不需要保存状态，可以释放：
    // if (screen_state) {
    //     free(screen_state);
    //     my_new_screen.state_data = NULL;
    // }
}
```

### 5.3 定时器使用

```c
// 定时器回调函数
static void update_timer_cb(lv_timer_t *timer)
{
    // 更新UI
    static int counter = 0;
    counter++;
    
    lv_label_set_text_fmt(counter_label, "Count: %d", counter);
    
    // 条件停止定时器
    if (counter >= 100) {
        lv_timer_pause(timer);
    }
}

void my_screen_init(void)
{
    // 创建周期性定时器（每500ms执行一次）
    timer = lv_timer_create(update_timer_cb, 500, NULL);
}
```

### 5.4 键盘事件处理模板

```c
static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    
    switch (key) {
        case KEY_UP:
            // 向上导航
            if (selected_index > 0) {
                selected_index--;
                update_selection();
            }
            break;
            
        case KEY_DOWN:
            // 向下导航
            if (selected_index < MAX_ITEMS - 1) {
                selected_index++;
                update_selection();
            }
            break;
            
        case KEY_LEFT:
            // 左侧操作（如翻页）
            previous_page();
            break;
            
        case KEY_RIGHT:
            // 右侧操作（如翻页）
            next_page();
            break;
            
        case KEY_ENTER:
            // 确认选择
            confirm_selection();
            break;
            
        case KEY_ESC:
            // 返回上一屏幕
            screen_back();
            break;
            
        default:
            printf("Unhandled key: %d\n", key);
            break;
    }
}
```

### 5.5 动画效果

```c
void my_screen_init(void)
{
    // ... 创建UI元素 ...
    
    // 添加淡入动画
    lv_obj_t *label = lv_label_create(ui_my_screen);
    lv_obj_set_style_opa(label, LV_OPA_TRANSP, 0);  // 初始透明
    
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 500);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_start(&a);
}
```

### 5.6 调试技巧

```c
void my_screen_init(void)
{
    printf("[%s] Screen initializing...\n", my_new_screen.name);
    
    // 添加调试信息
    printf("[%s] Screen size: %dx%d\n", 
           my_new_screen.name, 
           AI_PET_SCREEN_WIDTH, 
           AI_PET_SCREEN_HEIGHT);
    
    // ... 初始化代码 ...
    
    printf("[%s] Screen initialized successfully\n", my_new_screen.name);
}

void my_screen_deinit(void)
{
    printf("[%s] Screen deinitializing...\n", my_new_screen.name);
    
    // ... 清理代码 ...
    
    printf("[%s] Screen deinitialized\n", my_new_screen.name);
}
```

---

## 6. 常见问题

### 6.1 屏幕不显示

**问题**：创建的屏幕显示为黑屏或空白

**解决方案**：
```c
void my_screen_init(void)
{
    // 确保创建屏幕对象
    ui_my_screen = lv_obj_create(NULL);
    
    // 确保设置正确的尺寸
    lv_obj_set_size(ui_my_screen, AI_PET_SCREEN_WIDTH, AI_PET_SCREEN_HEIGHT);
    
    // 检查屏幕对象是否创建成功
    if (!ui_my_screen) {
        printf("Failed to create screen object\n");
        return;
    }
}
```

### 6.2 键盘事件不响应

**问题**：按键无反应

**解决方案**：
```c
void my_screen_init(void)
{
    // ... 创建UI ...
    
    // 必须添加到组
    lv_group_add_obj(lv_group_get_default(), ui_my_screen);
    
    // 必须设置焦点
    lv_group_focus_obj(ui_my_screen);
    
    // 必须注册事件回调
    lv_obj_add_event_cb(ui_my_screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
}
```

### 6.3 内存泄漏

**问题**：长时间运行后内存不足

**解决方案**：
```c
void my_screen_deinit(void)
{
    // 1. 移除所有事件回调
    if (ui_my_screen) {
        lv_obj_remove_event_cb(ui_my_screen, keyboard_event_cb);
        lv_group_remove_obj(ui_my_screen);
    }
    
    // 2. 删除所有定时器
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    
    // 3. 释放所有malloc的内存
    if (allocated_data) {
        free(allocated_data);
        allocated_data = NULL;
    }
    
    // 4. 清理状态数据（如果不需要保留）
    if (my_new_screen.state_data) {
        free(my_new_screen.state_data);
        my_new_screen.state_data = NULL;
    }
}
```

### 6.4 屏幕切换动画卡顿

**问题**：切换屏幕时出现卡顿

**解决方案**：
```c
void my_screen_init(void)
{
    // 在init中执行耗时操作前先显示加载提示
    // 或将耗时操作放到定时器中异步执行
    
    // 错误做法：
    // load_large_data();  // 耗时操作
    
    // 正确做法：
    lv_timer_create(load_data_async, 10, NULL);  // 延迟加载
}

static void load_data_async(lv_timer_t *timer)
{
    load_large_data();
    lv_timer_del(timer);  // 一次性定时器
}
```

### 6.5 定时器未清理

**问题**：切换屏幕后定时器仍在运行

**解决方案**：
```c
static lv_timer_t *timer = NULL;

void my_screen_deinit(void)
{
    // 确保删除定时器
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;  // 重要：置为NULL
    }
}
```

---

## 附录：完整示例

### 示例：简单的设置屏幕

```c
// settings_screen.h
#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include "screen_manager.h"

extern Screen_t settings_screen;

void settings_screen_init(void);
void settings_screen_deinit(void);

#endif
```

```c
// settings_screen.c
#include "settings_screen.h"
#include <stdio.h>

static lv_obj_t *ui_settings_screen;
static lv_obj_t *brightness_slider;
static lv_obj_t *volume_slider;
static int selected_item = 0;

Screen_t settings_screen = {
    .init = settings_screen_init,
    .deinit = settings_screen_deinit,
    .screen_obj = &ui_settings_screen,
    .name = "settings",
};

static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    
    switch (key) {
        case KEY_UP:
            if (selected_item > 0) {
                selected_item--;
            }
            break;
        case KEY_DOWN:
            if (selected_item < 1) {
                selected_item++;
            }
            break;
        case KEY_LEFT:
            // 减小当前选中项的值
            if (selected_item == 0) {
                int val = lv_slider_get_value(brightness_slider);
                lv_slider_set_value(brightness_slider, val - 10, LV_ANIM_ON);
            }
            break;
        case KEY_RIGHT:
            // 增加当前选中项的值
            if (selected_item == 0) {
                int val = lv_slider_get_value(brightness_slider);
                lv_slider_set_value(brightness_slider, val + 10, LV_ANIM_ON);
            }
            break;
        case KEY_ESC:
            screen_back();
            break;
    }
}

void settings_screen_init(void)
{
    // 创建屏幕
    ui_settings_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_settings_screen, AI_PET_SCREEN_WIDTH, AI_PET_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(ui_settings_screen, lv_color_hex(0x1E1E1E), 0);
    
    // 标题
    lv_obj_t *title = lv_label_create(ui_settings_screen);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    
    // 亮度设置
    lv_obj_t *brightness_label = lv_label_create(ui_settings_screen);
    lv_label_set_text(brightness_label, "Brightness:");
    lv_obj_align(brightness_label, LV_ALIGN_TOP_LEFT, 20, 50);
    lv_obj_set_style_text_color(brightness_label, lv_color_white(), 0);
    
    brightness_slider = lv_slider_create(ui_settings_screen);
    lv_obj_set_size(brightness_slider, 200, 10);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_LEFT, 20, 80);
    lv_slider_set_range(brightness_slider, 0, 100);
    lv_slider_set_value(brightness_slider, 50, LV_ANIM_OFF);
    
    // 音量设置
    lv_obj_t *volume_label = lv_label_create(ui_settings_screen);
    lv_label_set_text(volume_label, "Volume:");
    lv_obj_align(volume_label, LV_ALIGN_TOP_LEFT, 20, 110);
    lv_obj_set_style_text_color(volume_label, lv_color_white(), 0);
    
    volume_slider = lv_slider_create(ui_settings_screen);
    lv_obj_set_size(volume_slider, 200, 10);
    lv_obj_align(volume_slider, LV_ALIGN_TOP_LEFT, 20, 140);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, 70, LV_ANIM_OFF);
    
    // 注册事件
    lv_obj_add_event_cb(ui_settings_screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), ui_settings_screen);
    lv_group_focus_obj(ui_settings_screen);
    
    printf("[%s] Screen initialized\n", settings_screen.name);
}

void settings_screen_deinit(void)
{
    if (ui_settings_screen) {
        lv_obj_remove_event_cb(ui_settings_screen, keyboard_event_cb);
        lv_group_remove_obj(ui_settings_screen);
    }
    
    printf("[%s] Screen deinitialized\n", settings_screen.name);
}
```

---

## 总结

本文档提供了完整的UI界面开发流程，包括：

1. **理解架构**：基于栈的屏幕管理系统
2. **使用模板**：复制和修改template_screen文件
3. **设计UI**：使用LVGL控件创建界面
4. **集成项目**：添加到构建系统和导航系统
5. **最佳实践**：内存管理、状态保存、事件处理
6. **问题解决**：常见问题及解决方案

按照本指南，您可以快速创建新的UI界面并集成到TuyaOpen AI Pocket Pet项目中。

---

**版本**: 1.0  
**更新日期**: 2025-11-20  
**作者**: TuyaOpen Team
