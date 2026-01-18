# LVGL 图片转换工具使用说明

## 推荐方式：使用 LVGL 官方在线工具（最简单）

### 详细步骤：

1. **打开在线工具**
   - 访问：https://lvgl.github.io/lv_img_conv/
   - 或访问：https://lvgl.io/tools/imageconverter

2. **上传图片**
   - 点击 "Choose File" 或拖拽图片
   - 选择 `images/img_usb.png`

3. **设置转换参数**
   - **Output name**: 输入 `img_usb`（必须与代码中使用的名称一致）
   - **Color format**: 选择 `RGB565A8` 或 `CF_TRUE_COLOR_ALPHA`
     - 这是 LVGL v9 使用的格式
     - 对应 `LV_COLOR_FORMAT_RGB565A8`
   - **Output format**: 选择 `C array`
   - **Output file**: 可以留空或输入 `img_usb_v9.c`

4. **转换并下载**
   - 点击 "Convert" 按钮
   - 等待转换完成
   - 下载生成的 C 文件

5. **替换文件**
   - 将下载的文件重命名为 `img_usb_v9.c`
   - 替换 `src/ui/assets/img_usb_v9.c`
   - 确保文件格式与现有的 `img_gps_v9.c` 或 `img_configuration_v9.c` 一致

### 注意事项：

- ✅ 确保颜色格式选择 `RGB565A8` 或 `CF_TRUE_COLOR_ALPHA`
- ✅ 输出名称必须与代码中使用的变量名一致（`img_usb`）
- ✅ 生成的文件应该包含 `LV_ATTRIBUTE_IMG_USB` 宏定义
- ✅ 文件应该放在 `src/ui/assets/` 目录下

## 离线方式（需要 Node.js）

如果系统已安装 Node.js，可以使用命令行工具：

```bash
# 克隆工具仓库
git clone https://github.com/lvgl/lv_img_conv.git
cd lv_img_conv
npm install

# 转换图片
node lv_img_conv.js path/to/your_image.png -f -c CF_TRUE_COLOR_ALPHA -o img_usb
```

参数说明：
- `-f`: 输出 C 数组格式
- `-c CF_TRUE_COLOR_ALPHA`: 颜色格式（对应 RGB565A8）
- `-o img_usb`: 输出名称

## 注意事项

- 确保生成的 C 文件中的颜色格式与 LVGL v9 兼容
- 生成的变量名应该与你在代码中使用的名称一致
- 文件应该放在 `src/ui/assets/` 目录下
