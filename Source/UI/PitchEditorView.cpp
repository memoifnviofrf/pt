#include "PitchEditorView.h"

namespace
{
const juce::Colour kBg(0xff14171d), kPanel(0xff1b1f27), kGrid(0xff2a303b), kAccent(0xfff0a030), kText(0xffc8d0dc);

bool isBlackKey(int midi) { const int pc = ((midi % 12) + 12) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

juce::String midiName(int midi)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(n[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
}

void computeFit(const vts::EditSession& s, double plotW, double plotH, double& pxPerSec, double& viewTop, double& pxPerSemi, double& totalSec)
{
    totalSec = juce::jmax(1.0, (double) s.original[0].size() / s.sr);
    pxPerSec = juce::jlimit(10.0, 2000.0, plotW / (totalSec * 1.02));
    double lo = 1e9, hi = -1e9;
    for (auto& e : s.editor.getNotes()) { lo = juce::jmin(lo, juce::jmin(e.targetMidi, (double) e.note.medianMidi)); hi = juce::jmax(hi, juce::jmax(e.targetMidi, (double) e.note.medianMidi)); }
    if (hi < lo) { lo = 55; hi = 75; }
    lo -= 3; hi += 3;
    if (hi - lo < 18) { const double c = (hi + lo) / 2; lo = c - 9; hi = c + 9; }
    pxPerSemi = juce::jlimit(6.0, 60.0, plotH / (hi - lo));
    viewTop = juce::jmin(127.0, hi);
}
} // namespace

PitchEditorView::PitchEditorView(VocalTuneProcessor& p) : proc(p)
{
    addAndMakeVisible(hBar); addAndMakeVisible(vBar);
    hBar.addListener(this); vBar.addListener(this);
    hBar.setAutoHide(false); vBar.setAutoHide(false);
    proc.addChangeListener(this);
    startTimerHz(30);
}

PitchEditorView::~PitchEditorView() { proc.removeChangeListener(this); stopTimer(); }

juce::Rectangle<int> PitchEditorView::getPlotArea() const
{
    return { keyboardW, rulerH, juce::jmax(1, getWidth() - keyboardW - barSize), juce::jmax(1, getHeight() - rulerH - barSize) };
}

void PitchEditorView::resized()
{
    hBar.setBounds(keyboardW, getHeight() - barSize, juce::jmax(1, getWidth() - keyboardW - barSize), barSize);
    vBar.setBounds(getWidth() - barSize, rulerH, barSize, juce::jmax(1, getHeight() - rulerH - barSize));
    updateScrollBars();
}

void PitchEditorView::updateScrollBars()
{
    const auto plot = getPlotArea();
    hBar.setRangeLimits(0.0, totalSec + 2.0);
    hBar.setCurrentRange(viewStart, plot.getWidth() / pxPerSec, juce::dontSendNotification);
    vBar.setRangeLimits(0.0, 128.0);
    vBar.setCurrentRange(128.0 - viewTopMidi, plot.getHeight() / pxPerSemi, juce::dontSendNotification);
}

void PitchEditorView::scrollBarMoved(juce::ScrollBar* bar, double newStart)
{
    if (bar == &hBar) viewStart = juce::jmax(0.0, newStart); else viewTopMidi = juce::jlimit(0.0, 127.0, 128.0 - newStart);
    repaint();
}

void PitchEditorView::timerCallback()
{
    const double pos = proc.getPlayPositionSec();
    const bool b = proc.isBusy();
    if (b != wasBusy || pos != lastPos || proc.getEditRevision() != cachedEdit || proc.getAudioRevision() != cachedAudio) repaint();
    wasBusy = b; lastPos = pos;
}

void PitchEditorView::fitAll()
{
    const juce::ScopedTryLock tl(proc.getSessionLock());
    if (! tl.isLocked() || ! proc.getSession().hasAudio()) return;
    const auto plot = getPlotArea();
    computeFit(proc.getSession(), plot.getWidth(), plot.getHeight(), pxPerSec, viewTopMidi, pxPerSemi, totalSec);
    viewStart = 0; updateScrollBars(); repaint();
}

void PitchEditorView::refreshCaches(const vts::EditSession& s)
{
    const auto plot = getPlotArea();
    if (proc.getAudioRevision() != cachedAudio)
    {
        const auto& x = s.original[0]; const size_t B = 128;
        peaks.assign(2 * (x.size() / B + 1), 0.f);
        for (size_t b = 0; b * B < x.size(); ++b)
        {
            float lo = 0, hi = 0; const size_t e = juce::jmin(x.size(), (b + 1) * B);
            for (size_t i = b * B; i < e; ++i) { lo = juce::jmin(lo, x[i]); hi = juce::jmax(hi, x[i]); }
            peaks[2 * b] = lo; peaks[2 * b + 1] = hi;
        }
        computeFit(s, plot.getWidth(), plot.getHeight(), pxPerSec, viewTopMidi, pxPerSemi, totalSec);
        viewStart = 0; selected.clear(); cachedAudio = proc.getAudioRevision(); cachedEdit = ~0u;
        updateScrollBars();
    }
    if (proc.getEditRevision() != cachedEdit)
    {
        offsets = s.editor.computeOffsets(s.frames);
        anchors.clear(); anchors.push_back({ 0, 0 });
        for (auto& a : s.editor.buildAnchors())
            if (a.inSec > anchors.back().inSec + 1e-4 && a.outSec > anchors.back().outSec + 1e-4) anchors.push_back(a);
        if (s.editor.getNotes().size() != cachedNoteCount) selected.clear();
        cachedNoteCount = s.editor.getNotes().size();
        cachedEdit = proc.getEditRevision();
    }
}

double PitchEditorView::mapTime(double t) const
{
    if (anchors.size() < 2) return t;
    if (t >= anchors.back().inSec) return anchors.back().outSec + (t - anchors.back().inSec);
    size_t k = 1; while (k + 1 < anchors.size() && t > anchors[k].inSec) ++k;
    const double u = (t - anchors[k - 1].inSec) / (anchors[k].inSec - anchors[k - 1].inSec);
    return anchors[k - 1].outSec + u * (anchors[k].outSec - anchors[k - 1].outSec);
}

double PitchEditorView::toInputTime(const vts::EditNote& e, double outTime) const
{
    if (e.newStartSec < 0) return outTime;
    const double u = (outTime - e.newStartSec) / juce::jmax(1e-6, e.newEndSec - e.newStartSec);
    return e.note.startSec + u * (e.note.endSec - e.note.startSec);
}

double PitchEditorView::snapTime(double t) const
{
    const double bpm = proc.getHostBpm();
    const double g = bpm > 20.0 ? 60.0 / bpm / 4.0 : 0.05;        // 1/16音符(テンポ不明時は50ms)
    return std::round(t / g) * g;
}

// ===================== 描画 =====================
void PitchEditorView::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    const auto plot = getPlotArea();
    const juce::ScopedTryLock tl(proc.getSessionLock());
    if (! tl.isLocked() || proc.isBusy() || ! proc.getSession().hasAudio())
    {
        g.setColour(kText.withAlpha(0.8f)); g.setFont(16.0f);
        g.drawFittedText(proc.isBusy() ? proc.getStatus() : "Open an audio file, or press Capture and play the host over the vocal.",
                         getLocalBounds().reduced(20), juce::Justification::centred, 3);
        return;
    }
    const auto& s = proc.getSession();
    refreshCaches(s);

    drawKeyboardAndGrid(g, plot, s);
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(plot);
        drawWaveform(g, plot, s);
        drawCurves(g, plot, s);
        drawNotes(g, plot, s);
        const float px = timeToX(proc.getPlayPositionSec());     // 再生位置
        g.setColour(juce::Colour(0xffff5050)); g.drawLine(px, (float) rulerH, px, (float) plot.getBottom(), 1.5f);
        if (drag.kind == Drag::Kind::Rubber) { g.setColour(kAccent.withAlpha(0.25f)); g.fillRect(drag.rubber); g.setColour(kAccent); g.drawRect(drag.rubber, 1.0f); }
    }
    drawRuler(g, plot);
}

void PitchEditorView::drawKeyboardAndGrid(juce::Graphics& g, juce::Rectangle<int> plot, const vts::EditSession& s)
{
    const auto mask = vts::ScaleDetector::scaleMask(s.editor.settings.scale, s.editor.settings.root, s.editor.settings.customMask);
    const int top = (int) std::ceil(viewTopMidi + 1), bottom = (int) std::floor(viewTopMidi - plot.getHeight() / pxPerSemi - 1);
    for (int m = top; m >= bottom; --m)
    {
        const float y1 = midiToY(m + 0.5), y2 = midiToY(m - 0.5);
        const juce::Rectangle<float> row(0, y1, (float) getWidth(), y2 - y1);
        if (y2 < rulerH || y1 > plot.getBottom()) continue;
        const bool inScale = mask[(size_t) (((m % 12) + 12) % 12)];
        g.setColour(isBlackKey(m) ? juce::Colour(0xff161a21) : juce::Colour(0xff1d222b));
        g.fillRect(row.withLeft((float) keyboardW).withRight((float) plot.getRight()));
        if (inScale) { g.setColour(juce::Colour(0x1840a0ff)); g.fillRect(row.withLeft((float) keyboardW).withRight((float) plot.getRight())); }
        g.setColour(kGrid); g.drawHorizontalLine((int) y2, (float) keyboardW, (float) plot.getRight());
        g.setColour(isBlackKey(m) ? juce::Colour(0xff2a2a2a) : juce::Colour(0xffe6e6e6));
        g.fillRect(0.f, y1, (float) keyboardW - 1, y2 - y1 - 0.5f);
        if (m % 12 == 0 && pxPerSemi > 8) { g.setColour(juce::Colours::black); g.setFont(11.f); g.drawText(midiName(m), 2, (int) y1, keyboardW - 6, (int) (y2 - y1), juce::Justification::centredRight); }
    }
}

void PitchEditorView::drawRuler(juce::Graphics& g, juce::Rectangle<int> plot)
{
    g.setColour(kPanel); g.fillRect(0, 0, getWidth(), rulerH);
    static const double steps[] = { 0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120 };
    double step = 120; for (double st : steps) if (st * pxPerSec >= 80) { step = st; break; }
    g.setFont(11.f);
    for (double t = std::floor(viewStart / step) * step; t < xToTime((float) plot.getRight()); t += step)
    {
        const float x = timeToX(t); if (x < keyboardW) continue;
        g.setColour(kGrid.brighter(0.2f)); g.drawVerticalLine((int) x, (float) rulerH - 6, (float) rulerH);
        g.setColour(kText.withAlpha(0.8f));
        g.drawText(juce::String::formatted("%d:%05.2f", (int) (t / 60), std::fmod(t, 60.0)), (int) x + 3, 0, 70, rulerH - 2, juce::Justification::centredLeft);
        g.setColour(kGrid); g.drawVerticalLine((int) x, (float) rulerH, (float) plot.getBottom());
    }
    g.setColour(kBg); g.fillRect(0, 0, keyboardW, rulerH);
}

void PitchEditorView::drawWaveform(juce::Graphics& g, juce::Rectangle<int> plot, const vts::EditSession& s)
{
    const float y0 = (float) plot.getBottom() - waveH, mid = y0 + waveH / 2.0f;
    g.setColour(juce::Colour(0x66000000)); g.fillRect((float) plot.getX(), y0, (float) plot.getWidth(), (float) waveH);
    g.setColour(juce::Colour(0xff4b6a8a));
    const double sr = s.sr; const size_t nb = peaks.size() / 2;
    for (int x = plot.getX(); x < plot.getRight(); ++x)
    {
        const double t0 = xToTime((float) x), t1 = xToTime((float) x + 1);
        if (t1 < 0) continue;
        long b0 = (long) std::floor(t0 * sr / 128.0), b1 = (long) std::floor(t1 * sr / 128.0);
        if (b0 < 0) b0 = 0;
        if (b0 >= (long) nb) break;
        b1 = juce::jlimit(b0, (long) nb - 1, b1);
        float lo = 0, hi = 0; for (long b = b0; b <= b1; ++b) { lo = juce::jmin(lo, peaks[(size_t) (2 * b)]); hi = juce::jmax(hi, peaks[(size_t) (2 * b + 1)]); }
        g.drawVerticalLine(x, mid - juce::jlimit(0.f, 1.f, hi) * waveH * 0.5f, mid - juce::jlimit(-1.f, 0.f, lo) * waveH * 0.5f);
    }
}

void PitchEditorView::drawCurves(juce::Graphics& g, juce::Rectangle<int> plot, const vts::EditSession& s)
{
    const auto& f = s.frames; if (f.size() < 2) return;
    const double hop = f[1].timeSec - f[0].timeSec;
    const long i0 = juce::jmax(0L, (long) std::floor((viewStart - f[0].timeSec) / hop) - 1);
    const long i1 = juce::jmin((long) f.size() - 1, (long) std::ceil((xToTime((float) plot.getRight()) - f[0].timeSec) / hop) + 1);
    for (long k = i0; k < i1; ++k)
    {
        if (! f[(size_t) k].voiced || ! f[(size_t) k + 1].voiced) continue;
        const float conf = juce::jlimit(0.f, 1.f, f[(size_t) k].confidence);
        g.setColour(juce::Colour(0xff9aa5b5).withAlpha(0.25f + 0.55f * conf));         // 元のピッチ(濃さ=信頼度)
        g.drawLine(timeToX(f[(size_t) k].timeSec), midiToY(f[(size_t) k].midi), timeToX(f[(size_t) k + 1].timeSec), midiToY(f[(size_t) k + 1].midi), 1.4f);
    }
    if (offsets.size() == f.size())
        for (long k = i0; k < i1; ++k)
        {
            if (! f[(size_t) k].voiced || ! f[(size_t) k + 1].voiced) continue;
            g.setColour(kAccent);                                                      // 編集後のピッチ
            g.drawLine(timeToX(mapTime(f[(size_t) k].timeSec)), midiToY(f[(size_t) k].midi + offsets[(size_t) k]),
                       timeToX(mapTime(f[(size_t) k + 1].timeSec)), midiToY(f[(size_t) k + 1].midi + offsets[(size_t) k + 1]), 2.0f);
        }
}

void PitchEditorView::drawNotes(juce::Graphics& g, juce::Rectangle<int>, const vts::EditSession& s)
{
    const auto& ns = s.editor.getNotes();
    auto rectFor = [&](size_t i, bool withDrag)
    {
        double a, b; displaySpan(ns[i], a, b); double target = ns[i].targetMidi;
        if (withDrag && drag.kind != Drag::Kind::None && drag.kind != Drag::Kind::Rubber && selected.count(i))
        {
            if (tool == Tool::Pitch) target += drag.deltaSemis;
            else if (drag.kind == Drag::Kind::Move) { a += drag.deltaSec; b += drag.deltaSec; }
            else if (i == drag.anchor) { a = drag.newStart; b = drag.newEnd; }
        }
        const float h = (float) (pxPerSemi * 0.8);
        return juce::Rectangle<float>(timeToX(a), midiToY(target) - h / 2, juce::jmax(3.0f, timeToX(b) - timeToX(a)), h);
    };

    for (size_t i = 0; i < ns.size(); ++i)                                            // 元の位置(ゴースト)
    {
        const auto& e = ns[i];
        if (std::abs(e.targetMidi - e.note.medianMidi) < 0.02 && e.newStartSec < 0) continue;
        const float h = (float) (pxPerSemi * 0.8);
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.drawRoundedRectangle(timeToX(e.note.startSec), midiToY(e.note.medianMidi) - h / 2, juce::jmax(3.0f, timeToX(e.note.endSec) - timeToX(e.note.startSec)), h, 3.f, 1.f);
    }
    for (size_t i = 0; i + 1 < ns.size(); ++i)                                         // 接続状態(レガート)
        if (ns[i].note.legatoToNext)
        {
            const auto a = rectFor(i, true), b = rectFor(i + 1, true);
            g.setColour(juce::Colours::white.withAlpha(0.5f)); g.drawLine(a.getRight(), a.getCentreY(), b.getX(), b.getCentreY(), 1.5f);
        }
    for (size_t i = 0; i < ns.size(); ++i)
    {
        const auto r = rectFor(i, true); const bool sel = selected.count(i) > 0;
        g.setColour((sel ? juce::Colour(0xffffb347) : ns[i].manualTarget ? juce::Colour(0xff5aa9e6) : juce::Colour(0xff3d7ea6)).withAlpha(0.88f));
        g.fillRoundedRectangle(r, 3.f);
        g.setColour(sel ? juce::Colours::white : juce::Colours::black.withAlpha(0.5f)); g.drawRoundedRectangle(r, 3.f, sel ? 1.8f : 1.f);
        if (r.getWidth() > 64 && r.getHeight() > 10)
        {
            g.setColour(juce::Colours::black); g.setFont(juce::jmin(13.f, r.getHeight() - 2));
            const double dev = (ns[i].note.medianMidi - ns[i].targetMidi) * 100.0;   // 元の音高と目標とのズレ[cent]
            g.drawText(midiName((int) std::lround(ns[i].targetMidi)) + juce::String::formatted("  %+.0fc", dev), r.reduced(4, 0), juce::Justification::centredLeft);
        }
    }
}

// ===================== 操作 =====================
int PitchEditorView::hitNote(const vts::EditSession& s, juce::Point<float> p) const
{
    const auto& ns = s.editor.getNotes(); const float h = (float) (pxPerSemi * 0.8);
    for (int i = (int) ns.size() - 1; i >= 0; --i)
    {
        double a, b; displaySpan(ns[(size_t) i], a, b);
        const juce::Rectangle<float> r(timeToX(a) - 2, midiToY(ns[(size_t) i].targetMidi) - h / 2 - 2, juce::jmax(3.0f, timeToX(b) - timeToX(a)) + 4, h + 4);
        if (r.contains(p)) return i;
    }
    return -1;
}

void PitchEditorView::mouseDown(const juce::MouseEvent& e)
{
    if (e.position.y < rulerH && e.position.x >= keyboardW) { proc.setInternalPositionSec(xToTime(e.position.x)); repaint(); return; }
    const juce::ScopedTryLock tl(proc.getSessionLock());
    if (! tl.isLocked() || proc.isBusy() || ! proc.getSession().hasAudio()) return;
    const auto& s = proc.getSession();
    const int idx = hitNote(s, e.position);
    drag = Drag(); drag.origin = e.position;

    if (tool == Tool::Split || tool == Tool::Merge)
    {
        if (idx < 0) return;
        const auto ed = s.editor.getNotes()[(size_t) idx];
        const double tin = toInputTime(ed, xToTime(e.position.x)); selected.clear();
        if (tool == Tool::Split) proc.tryEdit([idx, tin](vts::EditSession& se) { se.editor.splitNote((size_t) idx, tin, se.frames); });
        else proc.tryEdit([idx](vts::EditSession& se) { se.editor.mergeNotes((size_t) idx, se.frames); });
        return;
    }
    if (idx >= 0)
    {
        const size_t i = (size_t) idx;
        if (e.mods.isShiftDown()) { if (selected.count(i)) selected.erase(i); else selected.insert(i); }
        else if (! selected.count(i)) { selected.clear(); selected.insert(i); }
        const auto& en = s.editor.getNotes()[i]; double a, b; displaySpan(en, a, b);
        drag.anchor = i; drag.anchorTarget = en.targetMidi; drag.anchorStart = a; drag.anchorEnd = b; drag.newStart = a; drag.newEnd = b;
        drag.kind = Drag::Kind::Move;
        if (tool == Tool::Time)
        {
            if (std::abs(e.position.x - timeToX(a)) <= 5) drag.kind = Drag::Kind::ResizeL;
            else if (std::abs(e.position.x - timeToX(b)) <= 5) drag.kind = Drag::Kind::ResizeR;
        }
    }
    else { if (! e.mods.isShiftDown()) selected.clear(); drag.kind = Drag::Kind::Rubber; drag.rubber = {}; }
    repaint();
}

void PitchEditorView::mouseDrag(const juce::MouseEvent& e)
{
    if (drag.kind == Drag::Kind::None) return;
    const double dx = (e.position.x - drag.origin.x) / pxPerSec, dy = -(e.position.y - drag.origin.y) / pxPerSemi;
    switch (drag.kind)
    {
        case Drag::Kind::Move:
            if (tool == Tool::Pitch) { drag.deltaSemis = snap ? std::round(drag.anchorTarget + dy) - drag.anchorTarget : dy; }
            else { double ns = drag.anchorStart + dx; if (snap) ns = snapTime(ns); drag.deltaSec = juce::jmax(-drag.anchorStart, ns - drag.anchorStart); }
            break;
        case Drag::Kind::ResizeL: { double ns = drag.anchorStart + dx; if (snap) ns = snapTime(ns); drag.newStart = juce::jlimit(0.0, drag.anchorEnd - 0.03, ns); break; }
        case Drag::Kind::ResizeR: { double ne = drag.anchorEnd + dx; if (snap) ne = snapTime(ne); drag.newEnd = juce::jmax(drag.anchorStart + 0.03, ne); break; }
        case Drag::Kind::Rubber: drag.rubber = juce::Rectangle<float>::leftTopRightBottom(juce::jmin(drag.origin.x, e.position.x), juce::jmin(drag.origin.y, e.position.y),
                                                                                         juce::jmax(drag.origin.x, e.position.x), juce::jmax(drag.origin.y, e.position.y)); break;
        default: break;
    }
    repaint();
}

void PitchEditorView::mouseUp(const juce::MouseEvent&)
{
    commitDrag();
    drag = Drag();
    repaint();
}

void PitchEditorView::commitDrag()
{
    std::vector<size_t> sel(selected.begin(), selected.end());
    switch (drag.kind)
    {
        case Drag::Kind::Move:
            if (tool == Tool::Pitch && std::abs(drag.deltaSemis) > 1e-6) { const double d = drag.deltaSemis; proc.tryEdit([sel, d](vts::EditSession& s) { s.editor.transposeNotes(sel, d); }); }
            else if (tool == Tool::Time && std::abs(drag.deltaSec) > 1e-6) { const double d = drag.deltaSec; proc.tryEdit([sel, d](vts::EditSession& s) { s.editor.moveNotes(sel, d); }); }
            break;
        case Drag::Kind::ResizeL: case Drag::Kind::ResizeR:
        {
            const size_t a = drag.anchor; const double ns = drag.newStart, ne = drag.newEnd;
            if (std::abs(ns - drag.anchorStart) > 1e-6 || std::abs(ne - drag.anchorEnd) > 1e-6) proc.tryEdit([a, ns, ne](vts::EditSession& s) { s.editor.setNoteTiming(a, ns, ne); });
            break;
        }
        case Drag::Kind::Rubber:
        {
            const juce::ScopedTryLock tl(proc.getSessionLock());
            if (! tl.isLocked()) break;
            const auto& ns = proc.getSession().editor.getNotes(); const float h = (float) (pxPerSemi * 0.8);
            for (size_t i = 0; i < ns.size(); ++i)
            {
                double a, b; displaySpan(ns[i], a, b);
                if (drag.rubber.intersects(juce::Rectangle<float>(timeToX(a), midiToY(ns[i].targetMidi) - h / 2, juce::jmax(3.0f, timeToX(b) - timeToX(a)), h))) selected.insert(i);
            }
            break;
        }
        default: break;
    }
}

void PitchEditorView::mouseMove(const juce::MouseEvent& e)
{
    juce::MouseCursor c = juce::MouseCursor::NormalCursor;
    const juce::ScopedTryLock tl(proc.getSessionLock());
    if (tl.isLocked() && ! proc.isBusy() && proc.getSession().hasAudio())
    {
        const auto& s = proc.getSession(); const int idx = hitNote(s, e.position);
        if (tool == Tool::Split) c = juce::MouseCursor::CrosshairCursor;
        else if (idx >= 0 && tool == Tool::Time)
        {
            double a, b; displaySpan(s.editor.getNotes()[(size_t) idx], a, b);
            c = (std::abs(e.position.x - timeToX(a)) <= 5 || std::abs(e.position.x - timeToX(b)) <= 5) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
        }
        else if (idx >= 0) c = tool == Tool::Pitch ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::PointingHandCursor;
    }
    setMouseCursor(c);
}

void PitchEditorView::zoomTimeAt(double f, float ax)
{
    const double t = xToTime(ax); pxPerSec = juce::jlimit(8.0, 4000.0, pxPerSec * f);
    viewStart = juce::jmax(0.0, t - (ax - keyboardW) / pxPerSec); updateScrollBars(); repaint();
}

void PitchEditorView::zoomPitchAt(double f, float ay)
{
    const double m = yToMidi(ay); pxPerSemi = juce::jlimit(6.0, 80.0, pxPerSemi * f);
    viewTopMidi = juce::jlimit(0.0, 127.0, m + (ay - rulerH) / pxPerSemi); updateScrollBars(); repaint();
}

void PitchEditorView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) zoomTimeAt(std::exp(w.deltaY * 1.2), e.position.x);
    else if (e.mods.isAltDown()) zoomPitchAt(std::exp(w.deltaY * 1.2), e.position.y);
    else if (e.mods.isShiftDown()) { viewStart = juce::jmax(0.0, viewStart - w.deltaY * 400.0 / pxPerSec); updateScrollBars(); repaint(); }
    else { viewTopMidi = juce::jlimit(0.0, 127.0, viewTopMidi + w.deltaY * 8.0); updateScrollBars(); repaint(); }
}

void PitchEditorView::deleteSelectionTargets()
{
    std::vector<size_t> sel(selected.begin(), selected.end());
    proc.tryEdit([sel](vts::EditSession& s) { for (auto i : sel) s.editor.resetNote(i); });
}

void PitchEditorView::quantizeTiming(double grid)
{
    proc.tryEdit([grid](vts::EditSession& s) { s.editor.quantizeTiming(grid, 1.0f); });
}
