# Monocypher source provenance

Unmodified upstream version **4.0.2**, from
https://github.com/LoupVaillant/Monocypher/tree/4.0.2/src.
Core files come from `src/`; Ed25519 files come from `src/optional/`.
Retain the source copyright notices and `LICENCE.md` (BSD-2-Clause or CC0).

| File | SHA-256 |
| --- | --- |
| monocypher.c | 02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d |
| monocypher.h | fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3 |
| monocypher-ed25519.c | 97d581639dfa72be08a6d57deb7d79b736be001cb416819cab196d22559d242b |
| monocypher-ed25519.h | 3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce |

The outer loader uses the core AEAD implementation. The artifact signature
verifier uses the optional standard Ed25519 implementation plus the core curve
operations; it does not substitute the core Blake2b-based EdDSA variant.
API reference: https://monocypher.org/manual/ed25519.
