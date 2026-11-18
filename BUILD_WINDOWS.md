# VOCAL TUNE STUDIO — VST3 のビルドと導入手順(Windows)

> **正直な現状:** プラグイン層(`Source/PluginProcessor.*`, `PluginEditor.*`, `UI/*`, `AudioFileManager.*`, `ProjectManager.*`)と `CMakeLists.txt` は、
> 開発環境にJUCEもネットワークもないため**本物のJUCEではまだ一度もコンパイルしていません**。代わりに、使用APIだけを模した最小スタブ(`Tools/syntax_check/`)で
> 自分のコードの構文チェックを行い、エラーなしを確認しています(JUCE本体のAPIとの食い違いは検出できません)。
> **本物のJUCEでの初回ビルドでは、エラーが出る可能性があります。** その場合は、出力の**最初のエラー数行**を共有してください(すぐ修正します)。
> DSPコア(`Source/Core/*`)は g++ でビルドし、68項目のテストがすべて通っています(`TEST_RESULTS.md`)。

VST3にする方法は3通りあります。**A が最も簡単**です。

---

## 方法A: GitHub Actions で自動ビルド(Visual Studio不要・推奨)
1. GitHubで新しいリポジトリを作り、このフォルダの中身をすべてpushする(`.github/workflows/build-vst3.yml` を含めること)。
2. リポジトリの **Actions** タブ → **build-vst3** → 実行が終わるのを待つ(約10〜20分。JUCEの取得とビルドを含む)。
3. 成功すると、実行結果ページ下部の **Artifacts** に `VOCAL-TUNE-STUDIO-vst3-windows-x64` が出るのでダウンロード。中に `VOCAL TUNE STUDIO.vst3` があります。
4. 失敗したら、赤いステップのログの最初の `error` を共有してください。

## 方法B: ローカルで1コマンドビルド
必要なもの: Windows 10/11 64bit、**Visual Studio 2022**(ワークロード「C++によるデスクトップ開発」)、**CMake 3.22以上**、Git、インターネット接続。

「x64 Native Tools Command Prompt for VS 2022」または通常のコマンドプロンプトで:
```bat
cd VocalTuneStudio
build_vst3.bat
```
このスクリプトは ①CMake設定(JUCE 8.0.6を自動取得)→ ②DSPテスト・CLIのビルド → ③**DSPテスト実行(68項目)** → ④VST3ビルド を順に行います。
どこかで失敗するとそこで止まり、原因の段階が分かります。

完成品:
```
build\VocalTuneStudio_artefacts\Release\VST3\VOCAL TUNE STUDIO.vst3
```

### オフライン/自前のJUCEを使う場合
JUCEを https://github.com/juce-framework/JUCE (8.0.x) から手動でダウンロード・展開し、
```bat
build_vst3.bat C:\path\to\JUCE
```

## 方法C: 手動でCMakeを実行
```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target VocalTuneStudio_VST3
```
DSPテストだけ試す(JUCE不要):
```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DVTS_BUILD_PLUGIN=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
Visual Studioで開きたい場合は `build\VocalTuneStudio.sln` を開き、`VocalTuneStudio_VST3` をビルドします。

---

## VST3のインストールとDAWでの確認
1. `VOCAL TUNE STUDIO.vst3` **フォルダごと** `C:\Program Files\Common Files\VST3\` にコピー(管理者権限が必要)。
2. DAWでプラグインを再スキャン。
   - **REAPER**: Options → Preferences → Plug-ins → VST → Re-scan → "Clear cache and re-scan"。
   - **Cubase Pro**: Studio → VST Plug-in Manager → 右上メニューから「Update」(または再スキャン)。
   - **Studio One**: Options → Locations → VST Plug-ins → 「Reset Blocklist」→ 再起動。
3. ボーカルトラックのインサートに **VOCAL TUNE STUDIO** を挿す。
4. **Open Audio** でWAVを読み込むか、**Capture** を押してホストを再生し、終わったらもう一度 **Capture**(詳細は `MANUAL_ja.md`)。

### まずREAPERで試すのがおすすめ
軽量で再スキャンが簡単です。Cubase/Studio Oneは、REAPERで読み込めてから確認すると原因の切り分けが楽です。

### 任意: プラグインの規格チェック
[pluginval](https://github.com/Tracktion/pluginval)(無料)でストレステストをかけられます。
```bat
pluginval.exe --strictness-level 5 --validate "C:\Program Files\Common Files\VST3\VOCAL TUNE STUDIO.vst3"
```
(まだ実行していません。結果は未確認です。)

---

## ビルドで詰まったとき
| 症状 | 原因と対処 |
|---|---|
| `CMake Error: could not find git` / JUCE取得失敗 | Gitを入れる。社内ネットワーク等でGitHubに繋がらない場合は、JUCEを手動DLして `build_vst3.bat C:\JUCE` |
| `Visual Studio 17 2022` が見つからない | Visual Studio 2022をインストール(Build Tools単体でも可)。2019の場合は `-G "Visual Studio 16 2019"` |
| C++の `error C****` | **最初のエラー数行**(ファイル名と行番号つき)を共有してください。JUCE層は未コンパイルのため、ここが最も起きやすい箇所です |
| `M_PI` が未定義 | `CMakeLists.txt` が `_USE_MATH_DEFINES` を設定済み。IDEで独自にビルドした場合は、プリプロセッサ定義に追加 |
| DAWにプラグインが出ない | `.vst3` を**フォルダごと**コピーしたか、DAW側の再スキャン/ブロックリストを確認 |
| DAWでクラッシュする | DAWとJUCEの組み合わせを問わず重要な情報です。DAW名・バージョン・操作手順を共有してください |

## CLI(DAWなしで 読み込み→解析→補正→書き出し)
```bat
build\Release\vts_cli.exe in.wav out.wav --scale auto --center 100 --drift 100 --mod 0
```
オプション: `--scale auto|chromatic|major|minor|hminor|mminor|pent`, `--root 0-11`, `--center/--drift/--mod/--transition 0-100`,
`--formant 半音`, `--min/--max Hz`, `--bits 16|24|32`, `--project file.vtsproj`
**VST3を作る前に、まずCLIで手持ちの歌声を処理して音質を確認することをおすすめします**(DSPの出来を、GUIやプラグインの問題と切り離して評価できます)。
