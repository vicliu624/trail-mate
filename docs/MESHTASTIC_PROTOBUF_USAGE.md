# Meshtastic Protobuf 使用情况

本文档说明 trail-mate 项目中实际使用的 Meshtastic protobuf 报文类型，以及未使用的类型。

## 项目概述

trail-mate 是一个基于 LilyGo T-LoRa Pager 的 Meshtastic 兼容聊天应用，实现了基本的文本消息传递、节点发现和路由功能。

## 已使用的 Protobuf 类型

### 核心数据结构

#### `meshtastic_Data`
- **用途**: 核心数据消息载体
- **使用位置**: `mt_codec_pb.cpp`, `mt_adapter.cpp`
- **字段**: portnum, payload, want_response, bitfield
- **功能**: 承载所有应用层数据 (文本消息、用户信息、路由消息等)

#### `meshtastic_User`
- **用途**: 用户/节点信息
- **使用位置**: `mt_codec_pb.cpp`, `mt_adapter.cpp`
- **字段**: id, short_name, long_name, macaddr, public_key, hw_model, role
- **功能**: 节点发现和用户信息广播

### 端口号枚举

#### 已使用的端口
- `meshtastic_PortNum_TEXT_MESSAGE_APP` - 文本消息 ([portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h))
- `meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP` - 压缩文本消息 ([portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h))
- `meshtastic_PortNum_NODEINFO_APP` - 节点信息广播 ([portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h))
- `meshtastic_PortNum_ROUTING_APP` - 路由确认消息 ([portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h))

### 数据结构

#### `meshtastic_Data` - 核心数据消息载体
- **用途**: 承载所有应用层数据 (文本消息、用户信息、路由消息等)
- **使用位置**: `mt_codec_pb.cpp`, `mt_adapter.cpp`
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)
- **字段**: portnum, payload, want_response, bitfield

#### `meshtastic_User` - 用户信息
- **用途**: 用户/节点信息和公钥
- **使用位置**: `mt_codec_pb.cpp`, `mt_adapter.cpp`
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)
- **字段**: id, short_name, long_name, macaddr, public_key, hw_model, role

### 路由相关

#### `meshtastic_Routing` - 路由消息
- **用途**: 路由消息和错误处理
- **使用位置**: `mt_adapter.cpp` (sendRoutingAck函数)
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)
- **字段**: error_reason
- **功能**: 消息传递确认和错误报告

#### `meshtastic_Routing_Error` - 路由错误枚举
- `meshtastic_Routing_Error_NONE` - 无错误确认
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)

### 硬件模型

#### `meshtastic_HardwareModel` - 硬件型号枚举
- `meshtastic_HardwareModel_T_LORA_PAGER` - LilyGo T-LoRa Pager
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)

### 设备配置

#### `meshtastic_Config_DeviceConfig_Role` - 设备角色
- `meshtastic_Config_DeviceConfig_Role_CLIENT` - 客户端角色
- **文件**: [config.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/config.pb.h)

## 未使用的 Protobuf 类型详解

### 位置和导航相关

#### `meshtastic_PortNum_POSITION_APP` - GPS位置信息
- **用途**: 广播设备的GPS位置坐标
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **使用场景**:
  - 实时位置共享和跟踪
  - 地图应用显示节点位置
  - 紧急情况下的位置报告
  - 导航和集合点协调

#### `meshtastic_PortNum_WAYPOINT_APP` - 航点信息
- **用途**: 定义和管理导航航点
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **使用场景**:
  - 户外探险路线规划
  - 搜索和救援行动
  - 共享兴趣点 (POI)
  - 团队协调导航目标

#### `meshtastic_Position` - 位置数据结构
- **用途**: 存储GPS坐标、精度、时间戳等位置信息
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)
- **字段**: latitude, longitude, altitude, precision, timestamp等

#### `meshtastic_Waypoint` - 航点数据结构
- **用途**: 定义导航点的信息
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)
- **字段**: name, description, position, icon等

### 遥测和传感器数据

#### `meshtastic_PortNum_TELEMETRY_APP` - 遥测数据
- **用途**: 收集和广播设备传感器数据
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **使用场景**:
  - 环境监测 (温度、湿度、大气压)
  - 设备状态监控 (电池电量、信号强度)
  - 气象数据收集
  - 农业和工业物联网应用

#### `meshtastic_Telemetry` - 传感器数据结构
- **用途**: 封装各种传感器测量值
- **文件**: [telemetry.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/telemetry.pb.h)
- **支持的传感器类型**:
  - 环境传感器 (温度、湿度、气压)
  - 运动传感器 (加速度计、陀螺仪)
  - 电源管理 (电池电压、电流)
  - 无线电性能 (SNR、RSSI)

### 远程硬件控制

#### `meshtastic_PortNum_REMOTE_HARDWARE_APP` - 远程硬件控制
- **用途**: 远程控制连接到Meshtastic节点的硬件设备
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **使用场景**:
  - 远程开关控制 (继电器、LED)
  - 传感器数据读取
  - 执行器控制 (电机、阀门)
  - 物联网设备集成

#### `meshtastic_RemoteHardware` - 硬件控制消息
- **用途**: 定义硬件控制命令和响应
- **文件**: [remote_hardware.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/remote_hardware.pb.h)
- **支持的操作**: GPIO读写、ADC读取、PWM控制等

### 网络诊断

#### `meshtastic_PortNum_TRACEROUTE_APP` - 路由跟踪
- **用途**: 诊断网络路径和性能
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **使用场景**:
  - 网络故障排查
  - 路由优化分析
  - 网络拓扑发现
  - 连接质量评估

#### `meshtastic_TraceRoute` - 路由跟踪数据
- **用途**: 记录消息经过的节点路径
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)
- **字段**: hop列表、延迟时间、信号质量等

### 高级消息类型

#### `meshtastic_PortNum_STORE_FORWARD_APP` - 存储转发
- **用途**: 在节点离线时存储消息，待上线后转发
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **Protobuf**: [storeforward.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/storeforward.pb.h)
- **使用场景**:
  - 间歇性连接的网络
  - 移动节点的离线通信
  - 延迟容忍网络应用

#### `meshtastic_PortNum_RANGE_TEST_APP` - 距离测试
- **用途**: 测试节点间的通信距离和信号质量
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **编码格式**: 简单文本消息 (无需专用 protobuf)
- **使用场景**:
  - 网络覆盖范围评估
  - 天线性能测试
  - 通信距离优化

#### `meshtastic_PortNum_PAXCOUNTER_APP` - 人群计数器
- **用途**: 使用WiFi嗅探统计附近设备数量
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **Protobuf**: [paxcount.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/paxcount.pb.h)
- **使用场景**:
  - 人群密度监测
  - 交通流量分析
  - 商业场所客流量统计

#### `meshtastic_PortNum_ATAK_PLUGIN` - ATAK插件
- **用途**: 与Android Team Awareness Kit (ATAK) 集成
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **Protobuf**: [atak.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/atak.pb.h)
- **使用场景**:
  - 军事和应急响应通信
  - 专业团队协调
  - GIS数据集成

#### `meshtastic_PortNum_AUDIO_APP` - 音频消息
- **用途**: 传输 codec2 编码的音频数据
- **文件**: [portnums.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h)
- **编码格式**: codec2 音频帧 (非 protobuf，直接二进制)
- **使用场景**:
  - 语音通信 (仅限 2.4GHz 带宽)
  - 音频数据传输
- **相关配置**: `meshtastic_ModuleConfig_AudioConfig` ([module_config.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/module_config.pb.h))

### 配置和设置

#### `meshtastic_Config` - 完整配置结构
- **用途**: 设备的完整配置管理
- **文件**: [config.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/config.pb.h)
- **包含的配置类型**: LoRa、WiFi、蓝牙、显示等所有模块配置

#### `meshtastic_Config_LoRaConfig` - LoRa配置
- **用途**: LoRa无线电参数配置
- **文件**: [config.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/config.pb.h)
- **配置项**: 频率、调制参数、发射功率、区域设置等

#### `meshtastic_ModuleConfig` - 模块配置
- **用途**: 各功能模块的配置 (MQTT、遥测、位置等)
- **文件**: [module_config.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/module_config.pb.h)
- **使用场景**: 通过网络远程配置设备

#### `meshtastic_Channel` - 频道设置
- **用途**: 定义通信频道和加密设置
- **文件**: [channel.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/channel.pb.h)
- **字段**: 频道名称、PSK密钥、设置等

### 高级网络功能

#### `meshtastic_NodeInfo` - 扩展节点信息
- **用途**: 比User更详细的节点信息
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)
- **额外字段**: 设备状态、邻居节点、路由表等

#### `meshtastic_DeviceState` - 设备状态
- **用途**: 报告设备运行状态
- **文件**: [mesh.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h)
- **字段**: 电池状态、内存使用、温度等

#### `meshtastic_MqttClientProxyMessage` - MQTT代理消息
- **用途**: 通过MQTT网关连接到互联网服务
- **文件**: [mqtt.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/mqtt.pb.h)
- **使用场景**: 云服务集成、远程监控、数据转发

#### `meshtastic_AdminMessage` - 管理消息
- **用途**: 远程设备管理和配置
- **文件**: [admin.pb.h](../src/chat/infra/meshtastic/generated/meshtastic/admin.pb.h)
- **支持的操作**: 重启、配置更新、固件升级等

## 架构说明

### 应用层抽象
项目使用 domain types (`ChatMessage`, `NodeInfo`, `MeshConfig` 等) 而不是直接使用 Meshtastic protobuf，这样提供了更好的抽象和可维护性。

### 协议兼容性
通过 `mt_codec_pb.cpp` 实现 domain types 与 Meshtastic protobuf 之间的转换，确保与 Meshtastic 网络的兼容性。

### 功能范围限制
当前实现专注于基本的聊天功能， deliberately 不实现以下高级功能：
- GPS位置共享
- 传感器数据收集
- 远程硬件控制
- 复杂网络管理功能

这使得代码更简洁，内存占用更少，适合资源受限的嵌入式设备。

## 扩展建议

如果将来需要添加更多功能，可以考虑实现：

1. **位置服务**: 使用 `meshtastic_Position` 和相关端口
2. **遥测功能**: 使用 `meshtastic_Telemetry` 收集传感器数据
3. **远程控制**: 使用 `meshtastic_RemoteHardware` 实现硬件控制
4. **网络诊断**: 使用 `meshtastic_TraceRoute` 进行网络分析

## 总结

trail-mate 项目仅使用了 Meshtastic protobuf 的核心子集，专注于提供可靠的文本通信功能。这种设计选择既保证了与 Meshtastic 网络的兼容性，又保持了代码的简洁性和设备的轻量化。