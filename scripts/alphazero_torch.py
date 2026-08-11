"""Compatibility imports for the AlphaZero Torch implementation.

The implementation lives in :mod:`scripts.alphazero.torch_impl`; this module keeps
the previous import path working for existing tools and tests.
"""

from scripts.alphazero.torch_impl import *  # noqa: F401,F403
from scripts.alphazero.torch_impl import _model_prior, _play_native_prior_game
