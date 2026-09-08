# N64 Runtimeの第三者ソース

このディレクトリには、N64 Runtimeが使用するMupen64PlusおよびGLideN64のupstreamソースをvendored形式で収録しています。Git submoduleではありません。

## Upstream baseline

| コンポーネント | upstream repository | baseline commit |
| --- | --- | --- |
| `mupen64plus-core` | `https://github.com/mupen64plus/mupen64plus-core.git` | `b0d68c20f49b8f833afa21450e0e8874c87c13c4` |
| `mupen64plus-ui-console` | `https://github.com/mupen64plus/mupen64plus-ui-console.git` | `1a68327fddda71f1acbad8a63ef04288b1887d19` |
| `mupen64plus-input-sdl` | `https://github.com/mupen64plus/mupen64plus-input-sdl.git` | `f2ca3839415d45a547f79d21177dfe15a0ce6d8c` |
| `mupen64plus-audio-sdl` | `https://github.com/mupen64plus/mupen64plus-audio-sdl.git` | `6c2c3f8ae10b7f0f6dfe06f45ca7ca598a6b659a` |
| `mupen64plus-rsp-hle` | `https://github.com/mupen64plus/mupen64plus-rsp-hle.git` | `2798e65d6fc89d89aace0b0d779af6406809b940` |
| `GLideN64` | `https://github.com/gonetz/GLideN64.git` | `7c8cc44f92437a298c21b4262f48f83effb4d05d` |

tree hash、ライセンス、patch、公開ソースでの収録範囲は、プロジェクトルートの`THIRD_PARTY_LOCK.json`を参照してください。公開ソースZIPでは、各コンポーネントからビルドに必要な範囲だけを収録する場合があります。

`third_party/`以下のupstreamソースは直接変更しません。N64 Runtime固有の変更は`../patches/`へ収録し、ビルド時に作業用コピーへ適用します。

各componentに付属するREADME、LICENSE、著作権表示は変更しません。
