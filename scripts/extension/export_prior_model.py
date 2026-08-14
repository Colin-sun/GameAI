#!/usr/bin/env python3
"""Export the Torch prior model to a small browser-readable binary format.

The extension intentionally does not ship a Python or Torch runtime.  This
converter preserves the exact Float32 tensors used by
``TorchPolicyValueNetwork`` and writes them in the order-independent GAI1
format consumed by ``extension/model.js``.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import tempfile
from pathlib import Path
from typing import Any

import numpy as np
import torch


MAGIC = b"GAI1"
FORMAT_VERSION = 1
RULE_VERSION = "majority-utt-v1"
INPUT_PLANES = 10
ACTION_SIZE = 81
INPUT_SIZE = INPUT_PLANES * ACTION_SIZE


def align4(value: int) -> int:
    return (value + 3) & ~3


def write_u32(buffer: bytearray, value: int) -> None:
    buffer.extend(struct.pack("<I", int(value)))


def write_string(buffer: bytearray, value: str) -> None:
    encoded = value.encode("utf-8")
    write_u32(buffer, len(encoded))
    buffer.extend(encoded)


def expected_shapes(channels: int, blocks: int) -> dict[str, tuple[int, ...]]:
    shapes: dict[str, tuple[int, ...]] = {
        "stem.weight": (channels, INPUT_PLANES, 3, 3),
        "stem.bias": (channels,),
        "policy_conv.weight": (32, channels, 1, 1),
        "policy_conv.bias": (32,),
        "policy_fc.weight": (ACTION_SIZE, 32 * ACTION_SIZE),
        "policy_fc.bias": (ACTION_SIZE,),
        "value_conv.weight": (32, channels, 1, 1),
        "value_conv.bias": (32,),
        "value_fc1.weight": (128, 32 * ACTION_SIZE),
        "value_fc1.bias": (128,),
        "value_fc2.weight": (1, 128),
        "value_fc2.bias": (1,),
    }
    for block in range(blocks):
        shapes[f"residual.{block}.conv1.weight"] = (channels, channels, 3, 3)
        shapes[f"residual.{block}.conv1.bias"] = (channels,)
        shapes[f"residual.{block}.conv2.weight"] = (channels, channels, 3, 3)
        shapes[f"residual.{block}.conv2.bias"] = (channels,)
    return shapes


def load_state(path: Path) -> tuple[dict[str, Any], dict[str, torch.Tensor]]:
    checkpoint = torch.load(path, map_location="cpu", weights_only=False)
    if not isinstance(checkpoint, dict):
        raise ValueError("checkpoint must contain a metadata/model_state dictionary")
    metadata = checkpoint.get("metadata")
    state = checkpoint.get("model_state")
    if not isinstance(metadata, dict) or not isinstance(state, dict):
        raise ValueError("checkpoint is missing metadata or model_state")
    if metadata.get("format_version") != FORMAT_VERSION:
        raise ValueError(f"unsupported checkpoint format: {metadata.get('format_version')!r}")
    if metadata.get("rule_version") != RULE_VERSION:
        raise ValueError(
            f"checkpoint rule {metadata.get('rule_version')!r} != {RULE_VERSION!r}"
        )
    if int(metadata.get("input_planes", -1)) != INPUT_PLANES:
        raise ValueError("checkpoint input_planes must be 10")
    if int(metadata.get("input_size", -1)) != INPUT_SIZE:
        raise ValueError("checkpoint input_size must be 810")
    if int(metadata.get("action_size", -1)) != ACTION_SIZE:
        raise ValueError("checkpoint action_size must be 81")
    channels = int(metadata.get("channels", 0))
    blocks = int(metadata.get("blocks", 0))
    if channels <= 0 or blocks <= 0:
        raise ValueError("checkpoint channels and blocks must be positive")

    shapes = expected_shapes(channels, blocks)
    missing = sorted(set(shapes) - set(state))
    extra = sorted(set(state) - set(shapes))
    if missing:
        raise ValueError(f"missing tensors: {', '.join(missing)}")
    if extra:
        raise ValueError(f"unexpected tensors: {', '.join(extra)}")
    for name, shape in shapes.items():
        tensor = state[name]
        if not isinstance(tensor, torch.Tensor):
            raise ValueError(f"tensor {name} is not a torch.Tensor")
        if tensor.dtype != torch.float32:
            raise ValueError(f"tensor {name} must be Float32, got {tensor.dtype}")
        if tuple(tensor.shape) != shape:
            raise ValueError(f"tensor {name} shape {tuple(tensor.shape)} != {shape}")
        if not torch.isfinite(tensor).all().item():
            raise ValueError(f"tensor {name} contains NaN or infinity")
    return metadata, state


def make_binary(metadata: dict[str, Any], state: dict[str, torch.Tensor], checkpoint: Path) -> bytes:
    channels = int(metadata["channels"])
    blocks = int(metadata["blocks"])
    names = list(expected_shapes(channels, blocks))
    output_metadata = {
        "inputPlanes": INPUT_PLANES,
        "actionSize": ACTION_SIZE,
        "channels": channels,
        "blocks": blocks,
        "ruleVersion": RULE_VERSION,
        "checkpoint": str(metadata.get("checkpoint") or checkpoint.name),
    }

    buffer = bytearray(MAGIC)
    write_u32(buffer, FORMAT_VERSION)
    write_u32(buffer, output_metadata["inputPlanes"])
    write_u32(buffer, output_metadata["actionSize"])
    write_u32(buffer, output_metadata["channels"])
    write_u32(buffer, output_metadata["blocks"])
    write_string(buffer, output_metadata["ruleVersion"])
    write_string(buffer, output_metadata["checkpoint"])
    write_u32(buffer, len(names))

    for name in names:
        write_string(buffer, name)
        values = state[name].detach().cpu().contiguous().numpy().astype("<f4", copy=False)
        write_u32(buffer, values.size)
        aligned = align4(len(buffer))
        buffer.extend(b"\x00" * (aligned - len(buffer)))
        buffer.extend(values.tobytes(order="C"))
    return bytes(buffer)


def write_atomic(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[2]
    default_checkpoint = root / "models/alphazero/utt_majority_v1_torch_teacher6000_512_hard.pt"
    default_output = root / "extension/models/utt_majority_v1_torch_teacher6000_512_hard.bin"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", type=Path, default=default_checkpoint)
    parser.add_argument("--output", type=Path, default=default_output)
    parser.add_argument("--force", action="store_true", help="replace an existing output")
    parser.add_argument("--json", action="store_true", help="print export metadata as JSON")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    checkpoint = args.checkpoint.resolve()
    output = args.output.resolve()
    if not checkpoint.is_file():
        raise FileNotFoundError(checkpoint)
    if output.exists() and not args.force:
        raise FileExistsError(f"output exists, pass --force: {output}")
    metadata, state = load_state(checkpoint)
    payload = make_binary(metadata, state, checkpoint)
    write_atomic(output, payload)
    summary = {
        "output": str(output),
        "bytes": len(payload),
        "rule_version": RULE_VERSION,
        "channels": int(metadata["channels"]),
        "blocks": int(metadata["blocks"]),
        "tensors": len(state),
    }
    if args.json:
        print(json.dumps(summary, ensure_ascii=True, sort_keys=True))
    else:
        print(f"wrote {output} ({len(payload)} bytes, {len(state)} tensors)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
