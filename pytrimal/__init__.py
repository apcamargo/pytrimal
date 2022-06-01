# noqa: D104
from ._version import __version__  # isort: skip

from . import _trimal
from ._trimal import (
    Alignment,
    AutomaticTrimmer,
    BaseTrimmer,
    ManualTrimmer,
    SimilarityMatrix,
    TrimmedAlignment,
)

__doc__ = _trimal.__doc__
__all__ = [
    "Alignment",
    "TrimmedAlignment",
    "BaseTrimmer",
    "AutomaticTrimmer",
    "ManualTrimmer",
    "SimilarityMatrix"
]

__author__ = "Martin Larralde <martin.larralde@embl.de>"
__license__ = "GPLv3"
