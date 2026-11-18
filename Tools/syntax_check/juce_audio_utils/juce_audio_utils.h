#pragma once
// ===== 最小JUCEスタブ(構文チェック専用) =====
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <initializer_list>

#define JUCE_CALLTYPE
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(C) C(const C&) = delete; C& operator=(const C&) = delete;

namespace juce {
using int64 = long long; using uint32 = unsigned int; using uint8 = unsigned char;
template <class T> constexpr T jmax(T a, T b) { return a > b ? a : b; }
template <class T> constexpr T jmin(T a, T b) { return a < b ? a : b; }
template <class T> constexpr T jlimit(T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }
enum NotificationType { dontSendNotification, sendNotification };

class String {
public:
    String() = default; String(const char* s) : s_(s) {} String(const std::string& s) : s_(s) {}
    String(int v) : s_(std::to_string(v)) {} String(double v, int dp) { char b[64]; std::snprintf(b, 64, "%.*f", dp, v); s_ = b; }
    bool isEmpty() const { return s_.empty(); } std::string toStdString() const { return s_; } double getDoubleValue() const { return std::atof(s_.c_str()); }
    String operator+(const String& o) const { return String(s_ + o.s_); } String& operator+=(const String& o) { s_ += o.s_; return *this; }
    bool operator==(const String& o) const { return s_ == o.s_; }
    static String formatted(const char* f, ...) { char b[512]; va_list a; va_start(a, f); std::vsnprintf(b, 512, f, a); va_end(a); return String(b); }
    std::string s_;
};
inline String operator+(const char* a, const String& b) { return String(a) + b; }
class StringArray { public: StringArray() = default; StringArray(std::initializer_list<const char*>) {} };
class Identifier { public: Identifier(const char*) {} };
class var { public: var(int) {} var(int64) {} operator int64() const { return 0; } };
class XmlElement { public: String toString() const { return {}; } };
inline std::unique_ptr<XmlElement> parseXML(const String&) { return nullptr; }
class UndoManager {};
class ValueTree { public: std::unique_ptr<XmlElement> createXml() const { return nullptr; } static ValueTree fromXml(const XmlElement&) { return {}; }
    ValueTree& setProperty(const Identifier&, const var&, UndoManager*) { return *this; } var getProperty(const Identifier&, const var& d = var(0)) const { return d; } };

template <class T> struct Point { T x{}, y{}; Point() = default; Point(T a, T b) : x(a), y(b) {} };
template <class T> struct Rectangle {
    T x_{}, y_{}, w_{}, h_{}; Rectangle() = default; Rectangle(T x, T y, T w, T h) : x_(x), y_(y), w_(w), h_(h) {}
    template <class U> Rectangle(const Rectangle<U>& o) : x_((T) o.x_), y_((T) o.y_), w_((T) o.w_), h_((T) o.h_) {}
    T getX() const { return x_; } T getY() const { return y_; } T getWidth() const { return w_; } T getHeight() const { return h_; }
    T getRight() const { return x_ + w_; } T getBottom() const { return y_ + h_; } T getCentreX() const { return x_ + w_ / 2; } T getCentreY() const { return y_ + h_ / 2; }
    Rectangle withLeft(T v) const { return { v, y_, x_ + w_ - v, h_ }; } Rectangle withRight(T v) const { return { x_, y_, v - x_, h_ }; }
    Rectangle withTrimmedLeft(T v) const { return { x_ + v, y_, w_ - v, h_ }; } Rectangle withTrimmedRight(T v) const { return { x_, y_, w_ - v, h_ }; }
    Rectangle withTrimmedBottom(T v) const { return { x_, y_, w_, h_ - v }; }
    Rectangle reduced(T a, T b) const { return { x_ + a, y_ + b, w_ - 2 * a, h_ - 2 * b }; } Rectangle reduced(T a) const { return reduced(a, a); }
    Rectangle removeFromTop(T v) { Rectangle r{ x_, y_, w_, v }; y_ += v; h_ -= v; return r; } Rectangle removeFromBottom(T v) { Rectangle r{ x_, y_ + h_ - v, w_, v }; h_ -= v; return r; }
    Rectangle removeFromLeft(T v) { Rectangle r{ x_, y_, v, h_ }; x_ += v; w_ -= v; return r; } Rectangle removeFromRight(T v) { Rectangle r{ x_ + w_ - v, y_, v, h_ }; w_ -= v; return r; }
    bool contains(Point<T> p) const { return p.x >= x_ && p.x < x_ + w_ && p.y >= y_ && p.y < y_ + h_; } bool intersects(const Rectangle& o) const { return x_ < o.x_ + o.w_ && o.x_ < x_ + w_ && y_ < o.y_ + o.h_ && o.y_ < y_ + h_; }
    static Rectangle leftTopRightBottom(T l, T t, T r, T b) { return { l, t, r - l, b - t }; }
};
class Colour { public: Colour() = default; Colour(uint32 argb) : v(argb) {} Colour withAlpha(float) const { return *this; } Colour brighter(float = 0.4f) const { return *this; } uint32 v = 0; };
struct Colours { static const Colour white, black, lightgrey; };
inline const Colour Colours::white{ 0xffffffff }; inline const Colour Colours::black{ 0xff000000 }; inline const Colour Colours::lightgrey{ 0xffd3d3d3 };
struct Justification { enum Flags { centredLeft = 1, centredRight = 2, centred = 4 }; Justification(int) {} };
struct FontOptions { FontOptions(float, int) {} FontOptions(float) {} };
struct Font { enum FontStyleFlags { plain = 0, bold = 1 }; Font(const FontOptions&) {} };
class Graphics {
public:
    void setColour(Colour) {} void fillAll(Colour) {}
    void fillRect(int, int, int, int) {} void fillRect(float, float, float, float) {} void fillRect(Rectangle<float>) {} void fillRect(Rectangle<int>) {}
    void drawRect(Rectangle<float>, float) {} void drawLine(float, float, float, float, float) {}
    void drawVerticalLine(int, float, float) {} void drawHorizontalLine(int, float, float) {}
    void drawText(const String&, int, int, int, int, Justification) {} void drawText(const String&, Rectangle<float>, Justification, bool = true) {}
    void drawFittedText(const String&, Rectangle<int>, Justification, int) {}
    void setFont(float) {} void setFont(const Font&) {}
    void fillRoundedRectangle(Rectangle<float>, float) {} void drawRoundedRectangle(Rectangle<float>, float, float) {} void drawRoundedRectangle(float, float, float, float, float, float) {}
    bool reduceClipRegion(Rectangle<int>) { return true; }
    struct ScopedSaveState { ScopedSaveState(Graphics&) {} };
};
struct ModifierKeys { bool isShiftDown() const { return false; } bool isCtrlDown() const { return false; } bool isCommandDown() const { return false; } bool isAltDown() const { return false; } };
struct MouseEvent { Point<float> position; ModifierKeys mods; };
struct MouseWheelDetails { float deltaY = 0; };
class MouseCursor { public: enum StandardCursorType { NormalCursor, CrosshairCursor, PointingHandCursor, DraggingHandCursor, LeftRightResizeCursor, UpDownResizeCursor }; MouseCursor(StandardCursorType = NormalCursor) {} };
struct KeyPress { static const int spaceKey = 32, returnKey = 13, escapeKey = 27, deleteKey = 127, backspaceKey = 8;
    KeyPress(int = 0) {} int getKeyCode() const { return 0; } ModifierKeys getModifiers() const { return {}; } };
class LookAndFeel { public: virtual ~LookAndFeel() = default; };
class LookAndFeel_V4 : public LookAndFeel { public: struct ColourScheme {}; static ColourScheme getMidnightColourScheme() { return {}; } void setColourScheme(ColourScheme) {} void setColour(int, Colour) {} Colour findColour(int) const { return {}; } };

class Component {
public:
    virtual ~Component() = default;
    virtual void paint(Graphics&) {} virtual void resized() {}
    virtual void mouseDown(const MouseEvent&) {} virtual void mouseDrag(const MouseEvent&) {} virtual void mouseUp(const MouseEvent&) {}
    virtual void mouseMove(const MouseEvent&) {} virtual void mouseWheelMove(const MouseEvent&, const MouseWheelDetails&) {}
    virtual bool keyPressed(const KeyPress&) { return false; }
    void setBounds(Rectangle<int>) {} void setBounds(int, int, int, int) {} Rectangle<int> getLocalBounds() const { return {}; } int getWidth() const { return 0; } int getHeight() const { return 0; }
    void addAndMakeVisible(Component&) {} void addAndMakeVisible(Component*) {} void repaint() {} void setWantsKeyboardFocus(bool) {}
    void setLookAndFeel(LookAndFeel*) {} void setMouseCursor(const MouseCursor&) {} void setEnabled(bool) {} void setSize(int, int) {}
    void setColour(int, Colour) {} Colour findColour(int) const { return {}; }
    void enterModalState(bool, void* = nullptr, bool = false) {}
};
class Label : public Component { public: enum ColourIds { textColourId = 1 }; Label() = default; Label(const String&, const String&) {} void setText(const String&, NotificationType) {} void setFont(const Font&) {} void setJustificationType(Justification) {} };
class Button : public Component { public: std::function<void()> onClick; void setButtonText(const String&) {} void setClickingTogglesState(bool) {} void setRadioGroupId(int) {}
    void setToggleState(bool, NotificationType) {} bool getToggleState() const { return false; } };
class TextButton : public Button { public: enum ColourIds { buttonColourId = 2 }; TextButton() = default; TextButton(const String&) {} };
class ToggleButton : public Button { public: ToggleButton() = default; ToggleButton(const String&) {} };
class ComboBox : public Component { public: std::function<void()> onChange; void addItem(const String&, int) {} void addItemList(const StringArray&, int) {}
    int getSelectedItemIndex() const { return 0; } void setTextWhenNothingSelected(const String&) {} };
class Slider : public Component { public: enum SliderStyle { LinearHorizontal }; enum TextEntryBoxPosition { TextBoxRight }; enum ColourIds { thumbColourId = 3 }; Slider() = default; Slider(SliderStyle, TextEntryBoxPosition) {} };
class ScrollBar : public Component { public: struct Listener { virtual ~Listener() = default; virtual void scrollBarMoved(ScrollBar*, double) = 0; };
    ScrollBar(bool) {} void addListener(Listener*) {} void setRangeLimits(double, double) {} void setCurrentRange(double, double, NotificationType) {} void setAutoHide(bool) {} };
class PopupMenu { public: struct Options { Options withTargetComponent(Component*) const { return *this; } };
    void addItem(int, const String&) {} void addSectionHeader(const String&) {} void showMenuAsync(const Options&, std::function<void(int)>) {} };
class AlertWindow : public Component { public: enum Dummy {}; AlertWindow(const String&, const String&, int) {}
    void addTextEditor(const String&, const String&, const String& = {}) {} void addButton(const String&, int, const KeyPress& = {}) {} String getTextEditorContents(const String&) const { return {}; } };
struct MessageBoxIconType { static const int NoIcon = 0; };
struct ModalCallbackFunction { template <class F> static void* create(F) { return nullptr; } };

class Timer { public: virtual ~Timer() = default; virtual void timerCallback() = 0; void startTimerHz(int) {} void stopTimer() {} };
class ChangeListener; class ChangeBroadcaster { public: virtual ~ChangeBroadcaster() = default; void addChangeListener(ChangeListener*) {} void removeChangeListener(ChangeListener*) {} void sendChangeMessage() {} };
class ChangeListener { public: virtual ~ChangeListener() = default; virtual void changeListenerCallback(ChangeBroadcaster*) = 0; };
class AsyncUpdater { public: virtual ~AsyncUpdater() = default; virtual void handleAsyncUpdate() = 0; void triggerAsyncUpdate() {} void cancelPendingUpdate() {} };
class Thread { public: explicit Thread(const String&) {} virtual ~Thread() = default; virtual void run() = 0; void startThread() {} bool stopThread(int) { return true; } bool threadShouldExit() const { return false; }
    bool wait(int) const { return true; } void notify() const {} static void sleep(int) {} };
class CriticalSection { public: void enter() const {} void exit() const {} bool tryEnter() const { return true; } };
template <class L> struct GenericScopedLock { explicit GenericScopedLock(const L& l) : l_(l) { l_.enter(); } ~GenericScopedLock() { l_.exit(); } const L& l_; };
template <class L> struct GenericScopedTryLock { explicit GenericScopedTryLock(const L& l) : l_(l), ok(l.tryEnter()) {} ~GenericScopedTryLock() { if (ok) l_.exit(); } bool isLocked() const { return ok; } const L& l_; bool ok; };
using ScopedLock = GenericScopedLock<CriticalSection>; using ScopedTryLock = GenericScopedTryLock<CriticalSection>;
class SpinLock { public: void enter() const {} void exit() const {} bool tryEnter() const { return true; } using ScopedLockType = GenericScopedLock<SpinLock>; using ScopedTryLockType = GenericScopedTryLock<SpinLock>; };
struct MessageManager { static void callAsync(std::function<void()>) {} };
struct ScopedNoDenormals {};
class File { public: File() = default; String getFullPathName() const { return {}; } bool existsAsFile() const { return false; } String getFileName() const { return {}; }
    File withFileExtension(const String&) const { return *this; } bool operator!=(const File&) const { return false; } };
struct FileBrowserComponent { static const int openMode = 1, saveMode = 2, canSelectFiles = 4, warnAboutOverwriting = 8; };
class FileChooser { public: FileChooser(const String&, const File&, const String&) {} void launchAsync(int, std::function<void(const FileChooser&)>) {} File getResult() const { return {}; } };
class MemoryBlock {};
class MemoryOutputStream { public: MemoryOutputStream(MemoryBlock&, bool) {} bool writeString(const String&) { return true; } bool writeInt64(int64) { return true; } bool write(const void*, size_t) { return true; } };
class MemoryInputStream { public: MemoryInputStream(const void*, size_t, bool) {} String readString() { return {}; } int64 readInt64() { return 0; } int64 getNumBytesRemaining() const { return 0; } int read(void*, int) { return 0; } };
template <class T> class Array { public: void add(const T&) {} };

template <class T> class AudioBuffer { public: AudioBuffer(int, int) {} int getNumSamples() const { return 0; } int getNumChannels() const { return 0; }
    const T* getReadPointer(int) const { return nullptr; } T* getWritePointer(int) { return nullptr; } void clear() {} };
class MidiBuffer {};
class AudioChannelSet { public: static AudioChannelSet stereo() { return {}; } static AudioChannelSet mono() { return {}; } bool operator==(const AudioChannelSet&) const { return true; } };
class AudioFormatReader { public: int64 lengthInSamples = 0; double sampleRate = 0; unsigned numChannels = 0;
    bool read(AudioBuffer<float>*, int, int, int64, bool, bool) { return true; } };
class AudioFormatManager { public: void registerBasicFormats() {} AudioFormatReader* createReaderFor(const File&) { return nullptr; } };
template <class T> class NormalisableRange { public: NormalisableRange(T, T, T) {} };
struct ParameterID { ParameterID(const char*, int) {} };
struct AudioParameterFloatAttributes { AudioParameterFloatAttributes withLabel(const String&) const { return *this; } };
class RangedAudioParameter { public: virtual ~RangedAudioParameter() = default; void setValueNotifyingHost(float) {} float convertTo0to1(float v) const { return v; } };
class AudioParameterFloat : public RangedAudioParameter { public: AudioParameterFloat(const ParameterID&, const String&, NormalisableRange<float>, float, const AudioParameterFloatAttributes& = {}) {} };
class AudioParameterChoice : public RangedAudioParameter { public: AudioParameterChoice(const ParameterID&, const String&, const StringArray&, int) {} StringArray choices; };
class AudioParameterBool : public RangedAudioParameter { public: AudioParameterBool(const ParameterID&, const String&, bool) {} };
class AudioPlayHead { public: struct PositionInfo { bool getIsPlaying() const { return false; } std::optional<int64> getTimeInSamples() const { return {}; } std::optional<double> getBpm() const { return {}; } };
    std::optional<PositionInfo> getPosition() const { return {}; } };

class AudioProcessorEditor;
class AudioProcessor {
public:
    struct BusesProperties { BusesProperties withInput(const String&, const AudioChannelSet&, bool) const { return *this; } BusesProperties withOutput(const String&, const AudioChannelSet&, bool) const { return *this; } };
    struct BusesLayout { AudioChannelSet getMainOutputChannelSet() const { return {}; } AudioChannelSet getMainInputChannelSet() const { return {}; } };
    AudioProcessor(const BusesProperties&) {} virtual ~AudioProcessor() = default;
    virtual void prepareToPlay(double, int) = 0; virtual void releaseResources() = 0; virtual bool isBusesLayoutSupported(const BusesLayout&) const { return true; }
    virtual void processBlock(AudioBuffer<float>&, MidiBuffer&) = 0; virtual AudioProcessorEditor* createEditor() = 0; virtual bool hasEditor() const = 0;
    virtual const String getName() const = 0; virtual bool acceptsMidi() const = 0; virtual bool producesMidi() const = 0; virtual double getTailLengthSeconds() const = 0;
    virtual int getNumPrograms() = 0; virtual int getCurrentProgram() = 0; virtual void setCurrentProgram(int) = 0; virtual const String getProgramName(int) = 0; virtual void changeProgramName(int, const String&) = 0;
    virtual void getStateInformation(MemoryBlock&) = 0; virtual void setStateInformation(const void*, int) = 0;
    AudioPlayHead* getPlayHead() const { return nullptr; } int getTotalNumInputChannels() const { return 2; }
};
class AudioProcessorEditor : public Component { public: explicit AudioProcessorEditor(AudioProcessor*) {} void setResizable(bool, bool) {} void setResizeLimits(int, int, int, int) {} };

class AudioProcessorValueTreeState {
public:
    class ParameterLayout { public: template <class... P> void add(std::unique_ptr<P>...) {} };
    struct Listener { virtual ~Listener() = default; virtual void parameterChanged(const String&, float) = 0; };
    AudioProcessorValueTreeState(AudioProcessor&, UndoManager*, const Identifier&, ParameterLayout) {}
    std::atomic<float>* getRawParameterValue(const String&) const { static std::atomic<float> a; return &a; }
    void addParameterListener(const String&, Listener*) {} void removeParameterListener(const String&, Listener*) {}
    RangedAudioParameter* getParameter(const String&) const { return nullptr; } ValueTree copyState() { return {}; } void replaceState(const ValueTree&) {}
    struct SliderAttachment { SliderAttachment(AudioProcessorValueTreeState&, const String&, Slider&) {} };
    struct ComboBoxAttachment { ComboBoxAttachment(AudioProcessorValueTreeState&, const String&, ComboBox&) {} };
    struct ButtonAttachment { ButtonAttachment(AudioProcessorValueTreeState&, const String&, Button&) {} };
};
} // namespace juce
