#!/usr/bin/env python3
"""
Convert PNG image to LVGL v9 C code format (RGB565A8)

Usage:
    python png_to_lvgl.py <input.png> <output.c> [image_name]

Example:
    python png_to_lvgl.py images/img_usb.png src/ui/assets/img_usb_v9.c img_usb
"""

import sys
import os
from PIL import Image
import struct

def rgb565(r, g, b):
    """Convert RGB888 to RGB565"""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def convert_png_to_lvgl(input_path, output_path, image_name="img"):
    """Convert PNG to LVGL v9 C code format"""
    
    # Open and convert image
    img = Image.open(input_path)
    
    # Convert to RGBA if not already
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    width, height = img.size
    
    # Calculate stride (width * 2 bytes for RGB565 + width * 1 byte for alpha)
    stride = width * 2 + width  # RGB565 (2 bytes) + Alpha (1 byte)
    
    # Prepare output data
    data = []
    
    # Get pixel data as a list (more reliable than img.load())
    pixels = list(img.getdata())
    
    # Count non-transparent pixels for debugging
    non_transparent_count = 0
    non_zero_count = 0
    
    # Convert each pixel to RGB565A8 format
    for y in range(height):
        for x in range(width):
            pixel_idx = y * width + x
            r, g, b, a = pixels[pixel_idx]
            
            # Debug: count non-transparent and non-zero pixels
            if a > 0:
                non_transparent_count += 1
            if r > 0 or g > 0 or b > 0:
                non_zero_count += 1
            
            # Convert to RGB565
            rgb565_val = rgb565(r, g, b)
            
            # Pack as little-endian (2 bytes)
            data.append(rgb565_val & 0xFF)  # Low byte
            data.append((rgb565_val >> 8) & 0xFF)  # High byte
            
            # Add alpha (1 byte)
            data.append(a)
    
    # Print debug info
    print(f"Debug: Non-transparent pixels: {non_transparent_count}/{width*height}")
    print(f"Debug: Non-zero color pixels: {non_zero_count}/{width*height}")
    
    # Generate C code
    output_lines = []
    output_lines.append("")
    output_lines.append("#include \"lvgl.h\"")
    output_lines.append("")
    output_lines.append("#if LVGL_VERSION_MAJOR == 9")
    output_lines.append("#ifndef LV_ATTRIBUTE_MEM_ALIGN")
    output_lines.append("#define LV_ATTRIBUTE_MEM_ALIGN")
    output_lines.append("#endif")
    output_lines.append("")
    # Clean up image name (remove 'img_' prefix if present to avoid duplication)
    attr_name = image_name.upper()
    if attr_name.startswith("IMG_"):
        attr_name = attr_name[4:]  # Remove "IMG_" prefix
    
    output_lines.append(f"#ifndef LV_ATTRIBUTE_IMG_{attr_name}")
    output_lines.append(f"#define LV_ATTRIBUTE_IMG_{attr_name}")
    output_lines.append("#endif")
    output_lines.append("")
    output_lines.append("static const")
    output_lines.append(f"LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_{attr_name}")
    output_lines.append(f"uint8_t {image_name}_map[] = {{")
    output_lines.append("")
    
    # Write data in hex format (similar to existing files)
    # Match the format of img_gps_v9.c: 4 spaces, no spaces between hex values
    bytes_per_line = 20  # Match the format of existing files
    for i in range(0, len(data), bytes_per_line):
        line_data = data[i:i+bytes_per_line]
        hex_str = ",".join([f"0x{b:02x}" for b in line_data])
        output_lines.append(f"    {hex_str},")
    
    output_lines.append("")
    output_lines.append("};")
    output_lines.append("")
    output_lines.append(f"const lv_image_dsc_t {image_name} = {{")
    output_lines.append("  .header.magic = LV_IMAGE_HEADER_MAGIC,")
    output_lines.append("  .header.cf = LV_COLOR_FORMAT_RGB565A8,")
    output_lines.append("  .header.flags = 0,")
    output_lines.append(f"  .header.w = {width},")
    output_lines.append(f"  .header.h = {height},")
    output_lines.append(f"  .header.stride = {stride},")
    output_lines.append(f"  .data_size = sizeof({image_name}_map),")
    output_lines.append(f"  .data = {image_name}_map,")
    output_lines.append("};")
    output_lines.append("")
    output_lines.append("#endif")
    output_lines.append("")
    
    # Write to file
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(output_lines))
    
    print(f"Successfully converted {input_path} to {output_path}")
    print(f"Image size: {width}x{height}, Data size: {len(data)} bytes")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    image_name = sys.argv[3] if len(sys.argv) > 3 else "img_usb"
    
    if not os.path.exists(input_path):
        print(f"Error: Input file '{input_path}' not found")
        sys.exit(1)
    
    # Create output directory if it doesn't exist
    output_dir = os.path.dirname(output_path)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    convert_png_to_lvgl(input_path, output_path, image_name)
