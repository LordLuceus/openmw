# Third-party licences

Project Hortator (accessible OpenMW) is distributed under the **GNU General
Public License v3**; see `LICENSE` for the full text. That covers OpenMW itself
and the accessibility work built into it.

The accessibility features additionally depend on the components listed below,
which carry their own licences. Their notices are reproduced here as those
licences require.

---

## Prism — screen-reader / text-to-speech output

**Licence: Mozilla Public License 2.0 (MPL-2.0)**
Source: <https://github.com/ethindp/prism>

Prism ("Platform-agnostic Reader Interface for Speech and Messages") is the
library that speaks text through your screen reader or the system speech engine.
It is linked into the game and is what makes spoken output possible.

MPL-2.0 §3.2 requires that this notice accompany the distributed binary, and
that the source form of Prism remains available. The complete MPL-2.0 text and
the corresponding source can be obtained from the repository above.

### Components bundled inside Prism

Prism itself incorporates the following, each under its own licence:

| Component | Licence | Purpose |
|---|---|---|
| concurrentqueue | BSD (2-clause) / Boost | Lock-free queueing |
| Djinni support library | Apache-2.0 | Cross-language interface glue |
| dr_wav | Public domain (Unlicense) or MIT-0 | WAV decoding |
| moderncom | MIT | Windows COM helpers |
| NVDA Controller Client | **LGPL-2.1** | Speaking through NVDA |
| NVGT (NonVisual Gaming Toolkit) portions | zlib-style permissive | Speech backend support |
| simdutf | Apache-2.0 / MIT | Unicode conversion |

The full text of each of these licences ships with the Prism source in its
`LICENSES/` directory.

**Note on the NVDA Controller Client (LGPL-2.1):** this component is used to send
text to NVDA. LGPL-2.1 permits its use in a larger work provided users can
replace that component; it is dynamically loaded via `nvdaControllerClient.dll`,
which can be substituted by replacing that file in the installation folder.

---

## Sound cues

The accessibility audio cues in `Data Files/sounds/a11y/` are original to this
project and are covered by the same GPLv3 licence as the rest of the work. Some
are currently placeholder tones and are expected to be replaced.

---

## Morrowind game data

Project Hortator is an **engine only**. It contains no Morrowind game assets. You
must own a copy of *The Elder Scrolls III: Morrowind*; its data files remain the
property of Bethesda Softworks and are covered by their own licence terms, not by
anything in this package.
