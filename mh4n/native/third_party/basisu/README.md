# Basis Universal transcoder (vendored)

- Library: Basis Universal by Binomial LLC; only its transcoder (`transcoder/`, every file of that folder) and the
  Zstandard decoder it uses for UASTC files supercompressed with Zstandard (`zstd/zstddeclib.c`, `zstd.h`,
  `zstd_errors.h`) are vendored, not the encoder, the tools or the other files of the repository.
- Upstream: https://github.com/BinomialLLC/basis_universal (the official repository)
- Tag `v2_1_0`, commit `45d5f41015eecd9570d5a3f89ab9cc0037a25063` (2026-03-09).
- Licences: Basis Universal is Apache-2.0 (`LICENSE`), with the attribution notices of `NOTICE`, which redistributions
  must carry (Apache-2.0 section 4(d)); Zstandard is BSD-3-Clause (`zstd/LICENSE`). Both are in the app's
  `THIRD_PARTY_NOTICES` and its licences folder.
- Used for: HD texture packs in KTX2 (`native/src/gpu/native/ktx2.cpp`, 1.0.2's S9b): ETC1S and UASTC files transcoded
  to a format the GPU samples.
- The files are unmodified, except one: `transcoder/basisu_astc_helpers.h`, where a tracking parameter was removed
  from a link in a comment (its first line says so; its blob here is `38cb21b00c952b1d9f026c0f516b20de5437e3d4`).
  Their upstream git blob ids (`git ls-tree v2_1_0`, independent of line endings):

```
94ea880430871f065fc4d9aef92fb5854003bd01  LICENSE
a53c09cdf93cea875919106ea78149eeebacf91d  NOTICE
d4e339e917c396ff9b37b362ddaae3bb2e3f4a70  transcoder/basisu.h
b9f138f3f55e26ccd31b2b63ed3dd0a85ae68e1b  transcoder/basisu_astc_cfgs.inl
9f6b69e669e1fcff999e85c4366c6845439251df  transcoder/basisu_astc_hdr_core.h
ef47285210a1aa4615408c35aef2878e32ccd0c6  transcoder/basisu_astc_helpers.h  (upstream; modified here, see above)
dc816f3d109a11056f5f8c425fd5dbc0d05449a8  transcoder/basisu_containers.h
2ac13029dc617272f3ab8d3697d1ba9b20f9ecc6  transcoder/basisu_containers_impl.h
572a816d005e1698f7139e64f81e0f3446a6fd03  transcoder/basisu_etc1_mods.inl
10462774f4fc7c6739721ab5c68a9a92c8b60688  transcoder/basisu_file_headers.h
33b77d00137e8353435c4343984c7e23d143eb8e  transcoder/basisu_idct.h
f2fa623cd6e89c077b2d9fd227d63908ca5973a1  transcoder/basisu_transcoder.cpp
df0e9cd3e880d793a5b08887317fb8b2ac3960a5  transcoder/basisu_transcoder.h
1cb3321fa20d7a53ec1efeede89b29a522c73800  transcoder/basisu_transcoder_internal.h
cd634c0df5d8549085d4f5cc3ea281412e3fe6af  transcoder/basisu_transcoder_tables_astc.inc
da4e7fee98356148a7ccafe50e274777a1c3b81f  transcoder/basisu_transcoder_tables_astc_0_255.inc
7acedd6a6f04ceb6efe5d6626e674de5b0df0084  transcoder/basisu_transcoder_tables_atc_55.inc
2b56c0944ca4d3a3833e2c4c13e78d012dd4cb52  transcoder/basisu_transcoder_tables_atc_56.inc
6669852923270db4ad0791826475d82d9d7dd402  transcoder/basisu_transcoder_tables_bc7_m5_alpha.inc
c0780988d8aaa947c97d8c143ded642adc3ae704  transcoder/basisu_transcoder_tables_bc7_m5_color.inc
205758b3d7ffabbddda70b42d5fae8ea990486e3  transcoder/basisu_transcoder_tables_dxt1_5.inc
f2d324fcc333d468b20027e63c3a9b850fd9f7ff  transcoder/basisu_transcoder_tables_dxt1_6.inc
fbaf988d78a2d148e15c7bdb6ee6303321215ab4  transcoder/basisu_transcoder_tables_pvrtc2_45.inc
3b9d7022e716051bdf44f9a8725e43a93c1bc690  transcoder/basisu_transcoder_tables_pvrtc2_alpha_33.inc
c1d01864f81a881edd36db018353a01419098e22  transcoder/basisu_transcoder_uastc.h
a793a802892567f17d464a831e2e531dc8833f55  zstd/LICENSE
b8c0644a7ec6533916e06aa8392a2727cbe55573  zstd/zstd.h
8ebc95cbb2ab882027a3e6cc92cdd5abc1a0dee4  zstd/zstd_errors.h
875d3b735adfddcf5e72e1112f10e022bd11b22e  zstd/zstddeclib.c
```

Fetched with `git clone --depth 1 --branch v2_1_0 https://github.com/BinomialLLC/basis_universal` and a copy of the
files above. Updating: replace them from the new tag (and remove the tracking parameter again if the new
`basisu_astc_helpers.h` still has it), change the tag, the commit and the ids here, run `ktx2_test`.
