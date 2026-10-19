"""Proof-of-work solvers used by the sites' own anti-bot gates.

DeepSeek's ``DeepSeekHashV1`` is the one that matters in practice: it is *not*
plain SHA3-256 — it is SHA3-256 with the first Keccak-f[1600] round skipped —
and the challenge is solved by finding the nonce whose digest **equals** the
``challenge`` value::

    prefix = f"{salt}_{expire_at}_"
    DeepSeekHashV1(prefix + str(nonce)) == bytes.fromhex(challenge)

``nonce`` is searched over ``range(difficulty)`` (144000 today).  The digest is
implemented here in pure Python so no WASM runtime is needed; solving takes a
few seconds, well inside the challenge's 300-second window.

ChatGPT's sentinel puzzle is a different scheme: its requirements token,
proof-of-work and turnstile live in :mod:`ai.sentinel`.
"""

from __future__ import annotations

import base64
import json
import os
import struct

MAX_NONCE = 20_000_000


class PowError(Exception):
    """The challenge could not be solved within the nonce budget."""


# --------------------------------------------------------------- DeepSeekHashV1

_RC = (
    0x0000000000000001, 0x0000000000008082, 0x800000000000808A, 0x8000000080008000,
    0x000000000000808B, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
    0x000000000000008A, 0x0000000000000088, 0x0000000080008009, 0x000000008000000A,
    0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
    0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
    0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008,
)
_PI = (0, 6, 12, 18, 24, 3, 9, 10, 16, 22, 1, 7, 13, 19, 20,
       4, 5, 11, 17, 23, 2, 8, 14, 15, 21)
_RHO = (0, 44, 43, 21, 14, 28, 20, 3, 45, 61, 1, 6, 25, 8, 18,
        27, 36, 10, 15, 56, 62, 55, 39, 41, 2)
_PID = tuple(_PI[i] % 5 for i in range(25))
_PIV = tuple((_PI[i], _RHO[i]) for i in range(25))
_M64 = 0xFFFFFFFFFFFFFFFF


def _keccak_f23(state):
    """Keccak-f[1600] with round 0 skipped (rounds 1..23)."""
    a = list(state)
    mask = _M64
    for rnd in range(1, 24):
        c0 = a[0] ^ a[5] ^ a[10] ^ a[15] ^ a[20]
        c1 = a[1] ^ a[6] ^ a[11] ^ a[16] ^ a[21]
        c2 = a[2] ^ a[7] ^ a[12] ^ a[17] ^ a[22]
        c3 = a[3] ^ a[8] ^ a[13] ^ a[18] ^ a[23]
        c4 = a[4] ^ a[9] ^ a[14] ^ a[19] ^ a[24]
        d = ((c4 ^ ((c1 << 1) | (c1 >> 63))) & mask,
             (c0 ^ ((c2 << 1) | (c2 >> 63))) & mask,
             (c1 ^ ((c3 << 1) | (c3 >> 63))) & mask,
             (c2 ^ ((c4 << 1) | (c4 >> 63))) & mask,
             (c3 ^ ((c0 << 1) | (c0 >> 63))) & mask)
        b = []
        append = b.append
        for (src, rho), dx in zip(_PIV, _PID):
            x = (a[src] ^ d[dx]) & mask
            append(((x << rho) | (x >> (64 - rho))) & mask)
        for base in (0, 5, 10, 15, 20):
            b0 = b[base]
            b1 = b[base + 1]
            b2 = b[base + 2]
            b3 = b[base + 3]
            b4 = b[base + 4]
            a[base] = (b0 ^ ((~b1 & mask) & b2)) & mask
            a[base + 1] = (b1 ^ ((~b2 & mask) & b3)) & mask
            a[base + 2] = (b2 ^ ((~b3 & mask) & b4)) & mask
            a[base + 3] = (b3 ^ ((~b4 & mask) & b0)) & mask
            a[base + 4] = (b4 ^ ((~b0 & mask) & b1)) & mask
        a[0] = (a[0] ^ _RC[rnd]) & mask
    state[:] = a


_RATE = 136


def deepseek_hash_v1(data):
    """The site's custom hash: SHA3-256 with Keccak round 0 skipped."""
    state = [0] * 25
    offset = 0
    length = len(data)
    while offset + _RATE <= length:
        for i in range(_RATE // 8):
            state[i] ^= struct.unpack_from("<Q", data, offset + i * 8)[0]
        _keccak_f23(state)
        offset += _RATE
    buf = bytearray(_RATE)
    remainder = length - offset
    buf[:remainder] = data[offset:]
    buf[remainder] = 0x06
    buf[_RATE - 1] |= 0x80
    for i in range(_RATE // 8):
        state[i] ^= struct.unpack_from("<Q", buf, i * 8)[0]
    _keccak_f23(state)
    out = bytearray(32)
    for i in range(4):
        struct.pack_into("<Q", out, i * 8, state[i])
    return bytes(out)


def _search_range(job):
    """Worker: search ``[start, stop)`` for the nonce (module-level, picklable)."""
    prefix, target, start, stop = job
    for nonce in range(start, stop):
        if deepseek_hash_v1(prefix + str(nonce).encode()) == target:
            return nonce
    return -1


def _workers():
    override = os.environ.get("AI_POW_WORKERS")
    if override:
        try:
            return max(1, int(override))
        except ValueError:
            pass
    return max(1, min(os.cpu_count() or 1, 16))


def solve_deepseek(salt, expire_at, difficulty, challenge, workers=None):
    """Return the nonce whose digest equals *challenge* (the PoW ``answer``).

    The nonce space ``range(difficulty)`` is split across CPU cores; the answer
    is found in well under the challenge's 300-second window.
    """
    prefix = f"{salt}_{expire_at}_".encode()
    try:
        target = bytes.fromhex(challenge)
    except (TypeError, ValueError) as exc:
        raise PowError(f"malformed DeepSeek challenge: {exc}") from exc
    limit = int(difficulty or 1)
    count = workers if workers is not None else _workers()
    count = max(1, min(count, limit) if limit else 1)
    if count == 1 or limit < 2048:
        answer = _search_range((prefix, target, 0, limit))
        if answer < 0:
            raise PowError("DeepSeekHashV1: no solution within the difficulty range")
        return answer

    import multiprocessing as mp
    size = (limit + count - 1) // count
    jobs = [(prefix, target, start, min(start + size, limit))
            for start in range(0, limit, size)]
    try:
        # ``fork`` inherits the loaded module and skips re-importing the CLI
        # (Python 3.14 defaults to ``forkserver``); fall back to the default
        # context where ``fork`` is unavailable.
        context = mp.get_context("fork") if "fork" in mp.get_all_start_methods() else mp
        pool = context.Pool(processes=len(jobs))
    except (OSError, ValueError):
        pool = None
    if pool is None:
        answer = _search_range((prefix, target, 0, limit))
        if answer < 0:
            raise PowError("DeepSeekHashV1: no solution within the difficulty range")
        return answer
    try:
        for result in pool.imap_unordered(_search_range, jobs):
            if result >= 0:
                return result
    finally:
        pool.terminate()
        pool.join()
    raise PowError("DeepSeekHashV1: no solution within the difficulty range")


def deepseek_response(salt, challenge, signature, answer, target_path):
    """The ``x-ds-pow-response`` header value for a solved challenge."""
    payload = {
        "algorithm": "DeepSeekHashV1",
        "challenge": challenge,
        "salt": salt,
        "answer": answer,
        "signature": signature,
        "target_path": target_path,
    }
    return base64.b64encode(json.dumps(payload, separators=(",", ":")).encode()).decode()
