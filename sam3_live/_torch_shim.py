"""Torch device shim for running CUDA-only code on non-CUDA devices.

The vendored SAM 3 repo hardcodes device="cuda" / .cuda() in ~25 places
(model construction, position-encoding caches, tensor factories). When the
shim is enabled, torch tensor-factory `device="cuda"` arguments and
`.cuda()` calls are redirected to a fallback device ("cpu" or "xpu"), which
is enough to build and run the model there. The position-encoding cache and
all forwards already follow the input tensor's device, so correctness is
preserved.
"""

import torch

_ACTIVE = False
_TARGET = "cpu"
_ORIG_FACTORY = {}
_ORIG_CUDA = None


def _wrap_factory(fn, name):
    def wrapped(*args, **kwargs):
        if _ACTIVE and kwargs.get("device") == "cuda":
            kwargs["device"] = _TARGET
        return fn(*args, **kwargs)
    wrapped.__name__ = name
    return wrapped


def enable(target: str = "cpu") -> None:
    global _ACTIVE, _TARGET, _ORIG_CUDA
    _TARGET = target
    if not _ACTIVE:
        for name in ("zeros", "ones", "empty", "full", "tensor",
                     "randn", "rand", "arange", "linspace"):
            fn = getattr(torch, name, None)
            if fn is not None:
                _ORIG_FACTORY[name] = fn
                setattr(torch, name, _wrap_factory(fn, name))
        _ORIG_CUDA = torch.Tensor.cuda
        _fallback_to = target

        def _cuda(self, *args, **kwargs):  # noqa: ANN001, ANN002, ANN003
            return self.to(_fallback_to)

        torch.Tensor.cuda = _cuda
        # pin_memory() requires a CUDA host allocator; on accelerator-less
        # hosts it raises. It is only a transfer optimization — no-op it.
        torch.Tensor.pin_memory = lambda self, *a, **k: self

        # Without CUDA autocast the model runs fp32; explicit .to(bf16/fp16)
        # (mask-memory storage, image loader) would reintroduce dtype
        # mismatches — redirect those casts to float32.
        _orig_to = torch.Tensor.to

        def _to(self, *args, **kwargs):
            if _ACTIVE:
                for i, v in enumerate(args):
                    if v in (torch.bfloat16, torch.float16):
                        args = (*args[:i], torch.float32, *args[i + 1:])
                        break
                d = kwargs.get("dtype")
                if d in (torch.bfloat16, torch.float16):
                    kwargs["dtype"] = torch.float32
            return _orig_to(self, *args, **kwargs)

        torch.Tensor.to = _to

        # close_session() probes GPU memory; its fail-open only catches
        # RuntimeError, but CUDA-less torch raises AssertionError from
        # _lazy_init. Stub the probes so sessions close cleanly.
        def _mem_get_info(*_a, **_k):
            return (0, 0)

        def _zero_int(*_a, **_k):
            return 0

        torch.cuda.mem_get_info = _mem_get_info
        torch.cuda.memory_allocated = _zero_int
        torch.cuda.memory_reserved = _zero_int
        torch.cuda.max_memory_allocated = _zero_int
        torch.cuda.max_memory_reserved = _zero_int
    _ACTIVE = True


def disable() -> None:
    global _ACTIVE
    _ACTIVE = False


def is_enabled() -> bool:
    return _ACTIVE
