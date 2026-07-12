#!/usr/bin/env python3
"""
Robot-dog SLAM long-term state simulation for the teaching KVStore.

The KVStore protocol splits by whitespace, so this client stores JSON payloads
as compact URL-safe base64 strings. It also hides the SET-vs-MOD difference
behind an upsert helper because this KVStore's SET does not overwrite.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import socket
import sys
import time
from dataclasses import dataclass
from typing import Any, Dict, Iterable, List, Optional, Tuple


MAX_COMMAND_BYTES = 500


ENGINE_COMMANDS = {
    "array": ("SET", "GET", "MOD", "DEL"),
    "rbtree": ("RSET", "RGET", "RMOD", "RDEL"),
    "hash": ("HSET", "HGET", "HMOD", "HDEL"),
    "skiplist": ("ZSET", "ZGET", "ZMOD", "ZDEL"),
}


def encode_json(payload: Dict[str, Any]) -> str:
    raw = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
    return base64.urlsafe_b64encode(raw).decode("ascii")


def decode_json(value: str) -> Dict[str, Any]:
    padded = value + "=" * (-len(value) % 4)
    return json.loads(base64.urlsafe_b64decode(padded.encode("ascii")).decode("utf-8"))


class KVStoreError(RuntimeError):
    pass


class BaseKVStore:
    def set(self, key: str, value: str) -> str:
        raise NotImplementedError

    def get(self, key: str) -> str:
        raise NotImplementedError

    def mod(self, key: str, value: str) -> str:
        raise NotImplementedError

    def upsert_json(self, key: str, payload: Dict[str, Any]) -> str:
        value = encode_json(payload)
        response = self.set(key, value)
        if response == "SUCCESS":
            return response
        if response == "FAIL":
            response = self.mod(key, value)
            if response == "SUCCESS":
                return response
        raise KVStoreError(f"upsert failed: key={key!r}, response={response!r}")


class TcpKVStore(BaseKVStore):
    def __init__(self, host: str, port: int, engine: str, timeout: float) -> None:
        if engine not in ENGINE_COMMANDS:
            raise ValueError(f"unknown engine: {engine}")
        self.commands = ENGINE_COMMANDS[engine]
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)

    def close(self) -> None:
        self.sock.close()

    def _request(self, command: str, key: str, value: Optional[str] = None) -> str:
        if value is None:
            line = f"{command} {key}\n"
        else:
            line = f"{command} {key} {value}\n"
        data = line.encode("ascii")
        if len(data) > MAX_COMMAND_BYTES:
            raise KVStoreError(f"command too long for BUFFER_LENGTH=512: {len(data)} bytes")
        self.sock.sendall(data)
        response = self.sock.recv(512).decode("utf-8", errors="replace").strip()
        return response

    def set(self, key: str, value: str) -> str:
        return self._request(self.commands[0], key, value)

    def get(self, key: str) -> str:
        return self._request(self.commands[1], key)

    def mod(self, key: str, value: str) -> str:
        return self._request(self.commands[2], key, value)


class MemoryKVStore(BaseKVStore):
    def __init__(self) -> None:
        self.data: Dict[str, str] = {}

    def set(self, key: str, value: str) -> str:
        if key in self.data:
            return "FAIL"
        self.data[key] = value
        return "SUCCESS"

    def get(self, key: str) -> str:
        return self.data.get(key, "NO EXIST")

    def mod(self, key: str, value: str) -> str:
        if key not in self.data:
            return "NO EXIST"
        self.data[key] = value
        return "SUCCESS"


@dataclass
class SimConfig:
    robot_id: str
    map_id: str
    steps: int
    rate_hz: float
    keyframe_interval: int
    radius_m: float


class RobotDogSlamSimulator:
    def __init__(self, kv: BaseKVStore, cfg: SimConfig) -> None:
        self.kv = kv
        self.cfg = cfg
        self.trace: List[Tuple[str, Dict[str, Any]]] = []
        self.semantic_tiles: Dict[str, Dict[str, int]] = {}
        self.last_node_id: Optional[str] = None

    def put(self, key: str, payload: Dict[str, Any]) -> None:
        self.kv.upsert_json(key, payload)
        self.trace.append((key, payload))

    def run(self) -> None:
        self._write_static_config()
        delay = 1.0 / self.cfg.rate_hz if self.cfg.rate_hz > 0 else 0.0

        for seq in range(self.cfg.steps):
            now_ms = int(time.time() * 1000)
            pose = self._pose(seq)
            seg = self._segmentation(seq, pose)
            tile_key, tile = self._update_tile(seq, pose, seg)

            self.put(
                f"robot:{self.cfg.robot_id}:state:pose:latest",
                {
                    "ts": now_ms,
                    "seq": seq,
                    "x": pose["x"],
                    "y": pose["y"],
                    "z": pose["z"],
                    "yaw": pose["yaw"],
                    "map": self.cfg.map_id,
                },
            )
            self.put(
                f"robot:{self.cfg.robot_id}:state:slam:status",
                {
                    "ts": now_ms,
                    "seq": seq,
                    "tracking": "OK",
                    "lost": 0,
                    "keyframes": seq // self.cfg.keyframe_interval + 1,
                    "loop": 1 if seq == self.cfg.steps - 1 else 0,
                },
            )
            self.put(
                f"robot:{self.cfg.robot_id}:state:segmentation:latest",
                {
                    "ts": now_ms,
                    "seq": seq,
                    "fps": 10,
                    "floor": seg["floor"],
                    "wall": seg["wall"],
                    "person": seg["person"],
                    "stairs": seg["stairs"],
                    "tile": tile_key,
                },
            )
            self.put(
                f"map:{self.cfg.map_id}:semantic_tile:{tile_key}",
                {
                    "ts": now_ms,
                    "tile": tile_key,
                    "visits": tile["visits"],
                    "floor": tile["floor"],
                    "wall": tile["wall"],
                    "person": tile["person"],
                    "stairs": tile["stairs"],
                },
            )

            if seq % self.cfg.keyframe_interval == 0:
                self._write_keyframe(seq, now_ms, pose, seg, tile_key)

            if delay:
                time.sleep(delay)

    def _write_static_config(self) -> None:
        now_ms = int(time.time() * 1000)
        self.put(
            f"robot:{self.cfg.robot_id}:config:camera:front_rgbd",
            {
                "ts": now_ms,
                "width": 640,
                "height": 480,
                "fps": 30,
                "fx": 525.0,
                "fy": 525.0,
                "cx": 319.5,
                "cy": 239.5,
            },
        )
        self.put(
            f"robot:{self.cfg.robot_id}:config:model:segmentation",
            {
                "ts": now_ms,
                "name": "semantic-lite-sim",
                "version": "0.1",
                "classes": ["floor", "wall", "person", "stairs"],
                "runtime": "simulated",
            },
        )
        self.put(
            f"robot:{self.cfg.robot_id}:mission:current",
            {
                "ts": now_ms,
                "id": "mission-demo-001",
                "mode": "semantic_slam_patrol",
                "goal": "loop_and_update_map",
            },
        )
        self.put(
            f"map:{self.cfg.map_id}:metadata",
            {
                "ts": now_ms,
                "frame": "map",
                "robot": self.cfg.robot_id,
                "type": "semantic_pose_graph",
                "resolution_m": 0.5,
                "storage": "kvstore",
            },
        )

    def _pose(self, seq: int) -> Dict[str, float]:
        angle = (seq / max(self.cfg.steps - 1, 1)) * 2.0 * math.pi
        return {
            "x": round(self.cfg.radius_m * math.cos(angle), 3),
            "y": round(self.cfg.radius_m * math.sin(angle), 3),
            "z": 0.42,
            "yaw": round(angle + math.pi / 2.0, 3),
        }

    def _segmentation(self, seq: int, pose: Dict[str, float]) -> Dict[str, int]:
        wall = 1 if abs(pose["x"]) > self.cfg.radius_m * 0.75 else 0
        stairs = 1 if pose["y"] > self.cfg.radius_m * 0.82 else 0
        person = 1 if seq % 11 in (5, 6) else 0
        floor = 1
        return {"floor": floor, "wall": wall, "person": person, "stairs": stairs}

    def _tile_id(self, pose: Dict[str, float]) -> str:
        tx = int(math.floor(pose["x"] / 1.0))
        ty = int(math.floor(pose["y"] / 1.0))
        return f"{tx:+03d}_{ty:+03d}"

    def _update_tile(
        self, seq: int, pose: Dict[str, float], seg: Dict[str, int]
    ) -> Tuple[str, Dict[str, int]]:
        tile_key = self._tile_id(pose)
        tile = self.semantic_tiles.setdefault(
            tile_key, {"visits": 0, "floor": 0, "wall": 0, "person": 0, "stairs": 0}
        )
        tile["visits"] += 1
        for label in ("floor", "wall", "person", "stairs"):
            tile[label] += seg[label]
        return tile_key, tile

    def _write_keyframe(
        self,
        seq: int,
        now_ms: int,
        pose: Dict[str, float],
        seg: Dict[str, int],
        tile_key: str,
    ) -> None:
        node_id = f"{seq:06d}"
        self.put(
            f"map:{self.cfg.map_id}:pose_graph:node:{node_id}",
            {
                "ts": now_ms,
                "seq": seq,
                "x": pose["x"],
                "y": pose["y"],
                "yaw": pose["yaw"],
                "tile": tile_key,
                "dyn": seg["person"],
            },
        )
        self.put(
            f"map:{self.cfg.map_id}:keyframe:{node_id}",
            {
                "ts": now_ms,
                "seq": seq,
                "rgb": f"frame_{node_id}.jpg",
                "mask": f"mask_{node_id}.rle",
                "tile": tile_key,
            },
        )
        if self.last_node_id is not None:
            self.put(
                f"map:{self.cfg.map_id}:pose_graph:edge:{self.last_node_id}:{node_id}",
                {
                    "ts": now_ms,
                    "from": self.last_node_id,
                    "to": node_id,
                    "type": "odom",
                    "score": 0.98,
                },
            )
        self.last_node_id = node_id


def write_trace(path: str, trace: Iterable[Tuple[str, Dict[str, Any]]]) -> None:
    with open(path, "w", encoding="utf-8") as fp:
        for key, payload in trace:
            fp.write(json.dumps({"key": key, "value": payload}, ensure_ascii=False) + "\n")


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Simulate long-term robot-dog SLAM and semantic-map writes to KVStore."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2048)
    parser.add_argument("--engine", choices=sorted(ENGINE_COMMANDS), default="hash")
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--offline", action="store_true", help="use an in-memory KVStore")
    parser.add_argument("--robot-id", default="go2_sim")
    parser.add_argument("--map-id", default="lab_loop")
    parser.add_argument("--steps", type=int, default=40)
    parser.add_argument("--rate-hz", type=float, default=0.0)
    parser.add_argument("--keyframe-interval", type=int, default=5)
    parser.add_argument("--radius-m", type=float, default=4.0)
    parser.add_argument("--trace-out", default="")
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    cfg = SimConfig(
        robot_id=args.robot_id,
        map_id=args.map_id,
        steps=args.steps,
        rate_hz=args.rate_hz,
        keyframe_interval=args.keyframe_interval,
        radius_m=args.radius_m,
    )

    kv: BaseKVStore
    tcp_kv: Optional[TcpKVStore] = None
    if args.offline:
        kv = MemoryKVStore()
    else:
        tcp_kv = TcpKVStore(args.host, args.port, args.engine, args.timeout)
        kv = tcp_kv

    try:
        sim = RobotDogSlamSimulator(kv, cfg)
        sim.run()
        if args.trace_out:
            write_trace(args.trace_out, sim.trace)
        print(
            f"wrote {len(sim.trace)} kv records "
            f"for robot={args.robot_id} map={args.map_id} engine={args.engine}"
        )
        return 0
    finally:
        if tcp_kv is not None:
            tcp_kv.close()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
