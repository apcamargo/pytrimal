# 🐍✂️ PytrimAl [![Stars](https://img.shields.io/github/stars/althonos/pytrimal.svg?style=social&maxAge=3600&label=Star)](https://github.com/althonos/pytrimal/stargazers)

*[Cython](https://cython.org/) bindings and Python interface to [trimAl](http://trimal.cgenomics.org/), a tool for automated alignment trimming.*

[![Actions](https://img.shields.io/github/workflow/status/althonos/pytrimal/Test/main?logo=github&style=flat-square&maxAge=300)](https://github.com/althonos/pytrimal/actions)
[![Coverage](https://img.shields.io/codecov/c/gh/althonos/pytrimal?style=flat-square&maxAge=3600&logo=codecov)](https://codecov.io/gh/althonos/pytrimal/)
[![License](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square&maxAge=2678400)](https://choosealicense.com/licenses/gpl-3.0/)
[![PyPI](https://img.shields.io/pypi/v/pytrimal.svg?style=flat-square&maxAge=3600&logo=PyPI)](https://pypi.org/project/pytrimal)
[![Bioconda](https://img.shields.io/conda/vn/bioconda/pytrimal?style=flat-square&maxAge=3600&logo=anaconda)](https://anaconda.org/bioconda/pytrimal)
[![AUR](https://img.shields.io/aur/version/python-pytrimal?logo=archlinux&style=flat-square&maxAge=3600)](https://aur.archlinux.org/packages/python-pytrimal)
[![Wheel](https://img.shields.io/pypi/wheel/pytrimal.svg?style=flat-square&maxAge=3600)](https://pypi.org/project/pytrimal/#files)
[![Python Versions](https://img.shields.io/pypi/pyversions/pytrimal.svg?style=flat-square&maxAge=600&logo=python)](https://pypi.org/project/pytrimal/#files)
[![Python Implementations](https://img.shields.io/pypi/implementation/pytrimal.svg?style=flat-square&maxAge=600&label=impl)](https://pypi.org/project/pytrimal/#files)
[![Source](https://img.shields.io/badge/source-GitHub-303030.svg?maxAge=2678400&style=flat-square)](https://github.com/althonos/pytrimal/)
[![Mirror](https://img.shields.io/badge/mirror-EMBL-009f4d?style=flat-square&maxAge=2678400)](https://git.embl.de/larralde/pytrimal/)
[![Issues](https://img.shields.io/github/issues/althonos/pytrimal.svg?style=flat-square&maxAge=600)](https://github.com/althonos/pytrimal/issues)
[![Docs](https://img.shields.io/readthedocs/pytrimal/latest?style=flat-square&maxAge=600)](https://pytrimal.readthedocs.io)
[![Changelog](https://img.shields.io/badge/keep%20a-changelog-8A0707.svg?maxAge=2678400&style=flat-square)](https://github.com/althonos/pytrimal/blob/main/CHANGELOG.md)
[![Downloads](https://img.shields.io/badge/dynamic/json?style=flat-square&color=303f9f&maxAge=86400&label=downloads&query=%24.total_downloads&url=https%3A%2F%2Fapi.pepy.tech%2Fapi%2Fprojects%2Fpytrimal)](https://pepy.tech/project/pytrimal)

***⚠️ This package is based on the development version of trimAl 2.0, and results
may not be consistent across versions or with the trimAl 1.4 results.***

## 🗺️ Overview

PytrimAl is a Python module that provides bindings to trimAl using
[Cython](https://cython.org/). It directly interacts with the trimAl
internals, which has the following advantages:

- **single dependency**: PytrimAl is distributed as a Python package, so you
  can add it as a dependency to your project, and stop worrying about the
  trimAl binary being present on the end-user machine.
- **no intermediate files**: Everything happens in memory, in a Python object
  you control, so you don't have to invoke the trimAl CLI using a
  sub-process and temporary files. `Alignment` objects can be created
  directly from Python code.
- **friendly interface**: The different trimming methods are implement as
  Python classes that can be configured independently.
- **error management**: Errors occuring in trimAl are converted
  transparently into Python exceptions, including an informative
  error message.


### 📋 Roadmap

The following features are available or considered for implementation:

- [x] **automatic trimming**
- [x] **manual trimming**
- [ ] **overlap trimming**
- [x] **alignment loading from disk**
- [ ] **alignment loading from a file-like object**
- [x] **aligment creation from Python**

<!-- ## 🔧 Installing

pytrimal can be installed directly from [PyPI](https://pypi.org/project/pytrimal/),
which hosts some pre-built wheels for the x86-64 architecture (Linux/OSX/Windows)
and the Aarch64 architecture (Linux only), as well as the code required to compile
from source with Cython:
```console
$ pip install pytrimal
```

Otherwise, pytrimal is also available as a [Bioconda](https://bioconda.github.io/)
package:
```console
$ conda install -c bioconda pytrimal
``` -->

## 💡 Example

Let's load an `Alignment` from a file on the disk, and use the *strictplus*
method to trim it, before printing the `TrimmedAlignment` as a Clustal block:
```python
from pytrimal import Alignment, AutomaticTrimmer

ali = Alignment.load("pytrimal/tests/data/example.001.AA.clw")
trimmer = AutomaticTrimmer(method="strictplus")

trimmed = trimmer.trim(ali)
for name, seq in zip(trimmed.names, trimmed.sequences):
    print(name.decode().rjust(6), seq)
```

This should output the following:
```
Sp8    GIVLVWLFPWNGLQIHMMGII
Sp10   VIMLEWFFAWLGLEINMMVII
Sp26   GLFLAAANAWLGLEINMMAQI
Sp6    GIYLSWYLAWLGLEINMMAII
Sp17   GFLLTWFQLWQGLDLNKMPVF
Sp33   GLHMAWFQAWGGLEINKQAIL
```

### 🧶 Thread-safety

Trimmer objects are thread-safe, and the `trim` method is re-entrant.
This means you can batch-process alignments in parallel using a [`ThreadPool`](https://docs.python.org/3/library/multiprocessing.html#multiprocessing.pool.ThreadPool)
with a single trimmer object:
```python
import glob
import multiprocessing.pool
from pytrimal import Alignment, AutomaticTrimmer

trimmer = AutomaticTrimmer()
alignments = map(Alignment.load, glob.iglob("pytrimal/tests/data/*.fasta"))

with multiprocessing.pool.ThreadPool() as pool:
    trimmed_alignments = pool.map(trimmer.trim, alignments)
```

## 💭 Feedback

### ⚠️ Issue Tracker

Found a bug ? Have an enhancement request ? Head over to the [GitHub issue tracker](https://github.com/althonos/pytrimal/issues)
if you need to report or ask something. If you are filing in on a bug,
please include as much information as you can about the issue, and try to
recreate the same bug in a simple, easily reproducible situation.


### 🏗️ Contributing

Contributions are more than welcome! See
[`CONTRIBUTING.md`](https://github.com/althonos/pytrimal/blob/main/CONTRIBUTING.md)
for more details.


## 📋 Changelog

This project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html)
and provides a [changelog](https://github.com/althonos/pytrimal/blob/main/CHANGELOG.md)
in the [Keep a Changelog](http://keepachangelog.com/en/1.0.0/) format.


## ⚖️ License

This library is provided under the [GNU General Public License v3.0](https://choosealicense.com/licenses/gpl-3.0/).
trimAl is developed by the [trimAl team](http://trimal.cgenomics.org/trimal_team) and is distributed under the
terms of the GPLv3 as well. See `vendor/trimal/LICENSE` for more information.

*This project is in no way not affiliated, sponsored, or otherwise endorsed
by the [trimAl authors](http://trimal.cgenomics.org/trimal_team). It was developed
by [Martin Larralde](https://github.com/althonos/) during his PhD project
at the [European Molecular Biology Laboratory](https://www.embl.de/) in
the [Zeller team](https://github.com/zellerlab).*
