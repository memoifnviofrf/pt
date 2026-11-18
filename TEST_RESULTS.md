# テスト結果

- 実行日: 2026-11-18(開発コンテナ内)
- 環境: Ubuntu 24.04 / g++ 13.3 (`-O2 -std=c++17 -Wall -Wextra`、警告なし) / `./run_tests.sh`
- 結果: PASS 68 / FAIL 0
- **すべて合成信号による検証です。実際の歌声・Windows・MSVC・JUCEプラグイン・DAWでの検証は未実施です。**
- Melodyneとの比較は実施していません。

## 実行ログ(生の出力)
```
=== PitchDetectorTest ===
sustained A3               voiced 244/244  octave errors 0  RMS 0.02 cents  PASS
vibrato C5 ±40c 5.5Hz     voiced 244/244  octave errors 0  RMS 9.42 cents  PASS
low E2                     voiced 244/244  octave errors 0  RMS 0.00 cents  PASS
high A5 breathy            voiced 244/244  octave errors 0  RMS 0.61 cents  PASS
silence voiced frames: 0 PASS
=== NoteSegmenterTest ===
detected 5 notes (expected 5)
  0.019 - 0.509  midi 60.00  dev +0.5 c  legato=1
  0.509 - 1.011  midi 62.00  dev +0.5 c  legato=1
  1.011 - 1.619  midi 64.05  dev +4.1 c  legato=0
  1.800 - 2.312  midi 67.06  dev +2.1 c  legato=1
  2.312 - 2.680  midi 66.00  dev +0.3 c  legato=0
note 0: start err 18.7 ms, end err 9.3 ms, pitch err 0.0 c  PASS
note 1: start err 9.3 ms, end err 10.7 ms, pitch err 0.0 c  PASS
note 2: start err 10.7 ms, end err 18.7 ms, pitch err 5.2 c  PASS
note 3: start err 0.0 ms, end err 12.0 ms, pitch err 6.4 c  PASS
note 4: start err 12.0 ms, end err 20.0 ms, pitch err 0.0 c  PASS
split/merge: PASS
=== PitchShifterTest ===
shift +3 semis: pitch RMS err 0.59 c | envelope err (preserved) 0.31 dB, (if moved) 2.62 dB  PASS
shift -4 semis: pitch RMS err 0.62 c | envelope err (preserved) 0.38 dB, (if moved) 3.06 dB  PASS
formant x1.2 (pitch fixed): err vs shifted env 0.91 dB, vs original env 2.26 dB  PASS
glide 0->+400c: pitch RMS err 2.41 c (262 frames)  PASS
noise passthrough max diff 0.00e+00, finite=1, length kept=1  PASS
=== PitchEditorTest ===
key detection (C major, D minor)             PASS  got root=0 minor=0 / root=2 minor=1
nearestScaleNote 61.4 in C major -> 62       PASS  
nearestScaleNote 65.2 in C pentatonic -> 64  PASS  
segmentation of detuned melody               PASS  4 notes
detected key of melody = C major             PASS  root=0 minor=0
  [Center/Drift/Mod 100%] center err vs target (cents): +0.2 +0.5 +0.1 +0.0 | residual std: 0.5 0.7 0.7 0.7
full correction: |center err|<8c, std<8c     PASS  
output finite                                PASS  
  [Mod 0%, Drift 0%] center err: +0.2 +0.5 -4.1 -0.1 | residual std: 0.5 0.7 20.3 4.0
vibrato kept at Modulation=0 (note 3 std>15c) PASS  
vibrato removed at Modulation=100% (note 3 std<8c) PASS  
Pitch Center 50% -> halfway                  PASS  note1 center err +17.7 c (expect ~+17)
manual target / undo / redo                  PASS  
=== VibratoTimingTest ===
vibrato analysis (true 5.5Hz, 40c, ramp 0.4-0.7s) PASS  rate 5.52 Hz, depth 44.1 c, onset 0.54 s, regularity 0.97
steady note -> no vibrato detected             PASS  depth 0.0 c, regularity 0.50
vibrato note stays one note                    PASS  1 notes
Vibrato Depth x0.5                             PASS  depth 19.9 c (orig 44.1), rate 5.51
Vibrato Rate x1.2                              PASS  rate 6.64 Hz (orig 5.52, expect 6.63), depth 50.0 c
Vibrato Onset +0.3s                            PASS  onset 0.87 s (orig 0.54, expect +0.30)
Vibrato Amount 0% removes vibrato              PASS  depth 3.6 c, detected=0
time stretch: first half x1.5 (steady note)    PASS  len 1.250 s (expect 1.250), pitch RMS 0.74 c, env err 0.07 dB
time stretch: first half x0.7 (steady note)    PASS  len 0.850 s (expect 0.850), pitch RMS 0.59 c, env err 0.07 dB
phrase: 2 notes before edit                    PASS  2 notes
note timing edit: output length                PASS  len 1.600 s (expect 1.600), notes 2
note timing edit: onset position & pitch kept  PASS  note2 start err 18.7 ms, note1 start/end err 0.0/0.0 ms, pitches 57.00 / 63.99 (expect 57/64)
WSOLA noise level within +-3 dB                PASS  stretched noise level -0.58 dB vs original
pitch +4 semis with time stretch combined      PASS  pitch RMS 0.72 c, len 1.500 s
=== SessionTest ===
WAV roundtrip 16-bit stereo                  PASS  max diff 3.03e-05, sr=44100 ch=2
WAV roundtrip 24-bit stereo                  PASS  max diff 8.94e-08, sr=44100 ch=2
WAV roundtrip float32 stereo                 PASS  max diff 0.00e+00, sr=44100 ch=2
reject missing file with error message       PASS  cannot open file
pipeline @ 44.1 kHz                          PASS  notes 2->2, center err +0.2 / +0.2 c, process 21 ms (audio 1.2 s)
pipeline @ 48.0 kHz                          PASS  notes 2->2, center err +0.1 / -0.0 c, process 23 ms (audio 1.2 s)
pipeline @ 88.2 kHz                          PASS  notes 2->2, center err +0.2 / +0.1 c, process 23 ms (audio 1.2 s)
pipeline @ 96.0 kHz                          PASS  notes 2->2, center err +0.1 / +1.5 c, process 26 ms (audio 1.2 s)
pipeline @ 192.0 kHz                         PASS  notes 2->2, center err +0.0 / +1.5 c, process 30 ms (audio 1.2 s)
project save/load roundtrip (notes, manual, timing) PASS  
original audio preserved (non-destructive)   PASS  original audio max diff 0.0e+00
render identical after project reload        PASS  render diff after reload 0.00e+00, len 62400
WAV export of edited result (timing edit)    PASS  len 1.300 s (expect 1.300 = 1.2+0.1)
A/B: original untouched by render            PASS  
split / merge notes                          PASS  
batch transpose = 1 undo step                PASS  
move note in time                            PASS  
plugin state blob roundtrip                  PASS  
corrupt state blob rejected                  PASS  truncated state
=== UtilTest ===
resample 48.0 -> 44.1 kHz                    PASS  max err 1.00e-05, len 22050 (expect 22050)
resample 44.1 -> 48.0 kHz                    PASS  max err 7.87e-06, len 24000 (expect 24000)
resample 96.0 -> 48.0 kHz                    PASS  max err 7.51e-06, len 24000 (expect 24000)
resample 48.0 -> 96.0 kHz                    PASS  max err 8.08e-06, len 48000 (expect 48000)
resample 192.0 -> 48.0 kHz                   PASS  max err 7.33e-06, len 24000 (expect 24000)
resample 44.1 -> 88.2 kHz                    PASS  max err 7.91e-06, len 44100 (expect 44100)
anti-alias on downsample (12kHz @ 16kHz)     PASS  alias level -96.4 dB
presets within parameter ranges              PASS  8 presets

```

## プラグイン層の構文チェック(スタブ使用)
`Tools/syntax_check/check.sh` — 最小JUCEスタブに対し g++ `-fsyntax-only -Wall -Wextra`。5ファイル(PluginProcessor / PluginEditor / PitchEditorView / AudioFileManager / ProjectManager)でエラー・警告なし(未使用変数の警告を除く)。
意図的にエラーを混入させ、チェッカーが検出することも確認済み。**これは本物のJUCEでのコンパイルの代わりにはなりません。**
