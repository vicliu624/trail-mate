# GPS 抖动过滤（结合 6 轴 IMU 的方案）

## 目标
在团队轨迹采样时，过滤掉明显不合理的 GPS 跳点，减少“偏移很大”的记录。
方案不是用 IMU 直接算速度，而是用 IMU 做**物理可行性约束**。

## 可行性结论
6 轴 IMU **不适合直接积分得到绝对速度**（漂移很快），但非常适合：
- 判断**静止/运动**状态
- 给出**加速度上限**，用于限制 GPS 速度变化

因此 IMU 用于**门控**（判定 GPS 是否物理合理）是可行且稳妥的。

## 输入数据
- GPS：`lat, lon, timestamp`（可选 `speed/hdop`）
- IMU：加速度 + 陀螺仪（高频）

## 核心逻辑
每次 GPS fix 到来时，计算“这两个点之间的速度”，再用 IMU 限制：

```
dt = t1 - t0
d  = distance(p0, p1)
v_gps = d / dt
```

### 1) IMU 预处理（轻量）
1. **重力去除**  
   - 低通滤波估计重力 `g`
   - `a_lin = a_raw - g`
2. **运动强度**  
   - `a_rms` = 近 2–5 秒 `|a_lin|` RMS  
   - `gyro_rms` = 近 2–5 秒角速度 RMS  
3. **运动状态**  
   - `is_stationary = (a_rms < A_STILL) && (gyro_rms < G_STILL)`

### 2) GPS 合理性判定
定义一个保守的速度上限 `v_max`：

- 如果静止：  
  `v_max = V_STILL_MAX`（例如 0.5–1.0 m/s）
- 如果在移动：  
  `v_max = v_prev + a_max * dt + V_MARGIN`

其中：
- `v_prev` 是上一次接受点的速度
- `a_max` 可以来自 IMU 峰值（再限制）或固定值
- `V_MARGIN` 是安全裕度

判定规则：
- 若 `v_gps > v_max` → **认为漂移，丢弃**
- 否则 → **接受**，并更新 `v_prev`

### 3) 失败保护
避免连续误判导致“永远不更新”：
- 连续拒绝 N 次 → 放行一次并打低置信标记
- 或逐步放宽 `V_MARGIN`

## 推荐初始参数
保守起步（后续用实测日志调）：
- `A_STILL` = 0.05–0.08 g
- `G_STILL` = 2–4 deg/s
- `V_STILL_MAX` = 1.0 m/s
- `a_max` = 4.0 m/s^2（步行/徒步），快速移动可用 8.0
- `V_MARGIN` = 0.5–1.0 m/s
- `N` = 3

## 为什么有效
GPS 大跳点意味着极高的“瞬时速度”，而 IMU 在静止或低运动状态下不可能支持这种速度。
用 IMU 做约束，能可靠过滤极端漂移，且不会引入积分漂移的问题。

## 局限
- 不会计算绝对速度，只做“可行性判断”
- IMU 安装和标定不佳会影响阈值
- GPS 间隔很长时 `dt` 大，阈值要更保守

## 最小实现清单
1. 做一个 IMU 运动状态估计（RMS + 静止判断）
2. 维护 `last_accepted_fix` 与 `v_prev`
3. 对每个新 GPS fix 进行 `v_gps` vs `v_max` 判定
4. 添加日志用于调参

## 建议日志格式
```
[TRACK] gps dt=120s d=180m v=1.5m/s
[TRACK] imu a_rms=0.03g gyro=1.2dps state=STILL v_max=1.0
[TRACK] reject: v_gps>v_max
```

