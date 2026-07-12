# Robotdog SLAM KV Simulation

这个目录模拟“长期状态和地图靠 KVStore”的部分，不改现有 KVStore 服务端。

核心适配点：

- 实时链路仍然假设走 ROS2 topic / shared memory。
- KVStore 只承载长期状态、配置、地图元数据、关键帧摘要、语义 tile。
- 当前 KVStore 协议用空白符分词，所以 value 统一写成 `base64url(compact_json)`。
- 当前 `SET` 不覆盖已有 key，所以客户端封装了 `upsert`: 先 `SET`，如果返回 `FAIL` 再 `MOD`。
- 默认使用 `hash` 引擎，也就是 `HSET/HGET/HMOD/HDEL`。

## Key Schema

```text
robot:{robot_id}:config:camera:front_rgbd
robot:{robot_id}:config:model:segmentation
robot:{robot_id}:mission:current

robot:{robot_id}:state:pose:latest
robot:{robot_id}:state:slam:status
robot:{robot_id}:state:segmentation:latest

map:{map_id}:metadata
map:{map_id}:pose_graph:node:{node_id}
map:{map_id}:pose_graph:edge:{from_node}:{to_node}
map:{map_id}:keyframe:{node_id}
map:{map_id}:semantic_tile:{tile_id}
```

这些 key 对应的是端侧 AI infra 的“状态层/存储层”，不是控制闭环：

```text
Camera / IMU -> Preprocess -> SLAM / Segmentation -> Fusion -> Nav2
                                               |
                                               v
                                      KVStore long-term state
```

## 运行方式

先启动你的 KVStore 服务端。当前 `kvstore.h` 里如果是：

```c
#define ENABLE_NETWORK_SELECT NETWORK_EPOLL
```

服务端默认监听 `2048` 起的一组端口，示例用 `2048`：

```bash
./kvstore
python3 sim/robotdog_slam_kv/robotdog_slam_kv_sim.py --host 127.0.0.1 --port 2048
```

如果你切到 NtyCo 版本并监听 `9096`：

```bash
python3 sim/robotdog_slam_kv/robotdog_slam_kv_sim.py --host 127.0.0.1 --port 9096
```

不启动服务端时，可以先用离线模式检查写入内容：

```bash
python3 sim/robotdog_slam_kv/robotdog_slam_kv_sim.py --offline --trace-out sim_trace.jsonl
```

## 示例查询

因为 value 是 base64url 编码，直接 `HGET` 会看到编码后的 JSON。

```text
HGET robot:go2_sim:state:pose:latest
HGET map:lab_loop:metadata
HGET map:lab_loop:pose_graph:node:000000
HGET map:lab_loop:semantic_tile:+04_+00
```

在真实 ROS2 适配时，可以把这个 Python 脚本里的 `BaseKVStore.upsert_json()` 保留下来，外层替换成 ROS2 node：

```text
/slam/pose                   -> robot:{id}:state:pose:latest
/slam/status                 -> robot:{id}:state:slam:status
/ai/segmentation/classes     -> robot:{id}:state:segmentation:latest
/semantic_map/tile_updates   -> map:{map}:semantic_tile:{tile}
/slam/keyframe               -> map:{map}:keyframe:{node}
```

## 当前 KVStore 限制

- 单条命令受 `BUFFER_LENGTH=512` 限制，payload 必须保持小而摘要化。
- 不适合存原始图像、深度图、完整 mask 或高频 IMU。
- 服务端目前按“一次 recv 等于一条命令”的教学假设处理，仿真客户端逐条发送命令。
- 如果后续要存大地图块，建议新增 length-prefixed 协议或 blob 文件路径索引。
