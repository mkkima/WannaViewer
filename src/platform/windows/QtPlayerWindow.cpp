#include "wannaviewer/platform/windows/QtPlayerWindow.hpp"

#ifdef _WIN32

#include "wannaviewer/core/Url.hpp"
#include "wannaviewer/resolvers/AniBoomResolver.hpp"
#include "wannaviewer/resolvers/AnimeGoResolver.hpp"
#include "wannaviewer/resolvers/BrowserEmbedResolver.hpp"
#include "wannaviewer/resolvers/CdnVideoHubResolver.hpp"
#include "wannaviewer/resolvers/DirectMediaResolver.hpp"
#include "wannaviewer/resolvers/GenericResolver.hpp"
#include "wannaviewer/resolvers/YummyAnimeResolver.hpp"
#include "wannaviewer/ui/ControlLayout.hpp"

#include <QAbstractAnimation>
#include <QApplication>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QParallelAnimationGroup>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVariantAnimation>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <fstream>
#include <ranges>

#include <dxgi1_6.h>
#include <nlohmann/json.hpp>
#include <windows.h>

namespace wannaviewer {
namespace {

constexpr int kControlsHeight = 92;
constexpr int kControlsMargin = 12;
constexpr int kControlsHideDelayMs = 1100;
constexpr int kUiIntervalMs = 50;
constexpr int kPanelEnterDurationMs = 190;
constexpr int kPanelExitDurationMs = 140;
constexpr int kFeedbackDurationMs = 950;
constexpr int kProgressSaveIntervalMs = 10000;
constexpr int kSettingsSaveDelayMs = 500;
constexpr UINT kBackgroundTestQueryMessage = WM_APP + 0x250;
constexpr UINT kBackgroundTestActionMessage = WM_APP + 0x251;

QString ToQString(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string ToUtf8(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

bool SystemAnimationsEnabled() noexcept {
    BOOL enabled = TRUE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || enabled != FALSE;
}

QString TimeText(double seconds) {
    const auto total = std::max<std::int64_t>(0, static_cast<std::int64_t>(seconds));
    const auto hours = total / 3600;
    const auto minutes = (total / 60) % 60;
    const auto remaining = total % 60;
    return hours > 0 ? QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'))
                                                .arg(remaining, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0'))
                                                .arg(remaining, 2, 10, QLatin1Char('0'));
}

QString DisplayLabel(std::string_view value, const char* fallback) {
    return value.empty() ? QString::fromUtf8(fallback) : ToQString(value);
}

QString SettingChoice(bool active, QString label) {
    return (active ? QStringLiteral("✓  ") : QStringLiteral("    ")) + std::move(label);
}

std::string PlaybackIdentity(std::string_view value) {
    if (const auto parsed = Url::Parse(value)) return "url\n" + parsed->Value();
    std::error_code error;
    const std::filesystem::path path(ToQString(value).toStdWString());
    if (std::filesystem::is_regular_file(path, error) && !error) {
        auto canonical = std::filesystem::weakly_canonical(path, error);
        if (error) {
            error.clear();
            canonical = std::filesystem::absolute(path, error);
        }
        const std::string normalized = error ? ToUtf8(ToQString(value).toCaseFolded())
                                             : ToUtf8(QString::fromStdWString(canonical.wstring()).toCaseFolded());
        error.clear();
        const auto size = std::filesystem::file_size(path, error);
        const auto safeSize = error ? std::uintmax_t{0} : size;
        error.clear();
        const auto modified = std::filesystem::last_write_time(path, error);
        const auto modifiedTicks = error ? std::int64_t{0} :
            static_cast<std::int64_t>(modified.time_since_epoch().count());
        return std::format("file\n{}\n{}\n{}", normalized, safeSize, modifiedTicks);
    }
    return "value\n" + std::string(value);
}

std::string PlaybackStateKey(std::string_view identity) {
    const QByteArray bytes(identity.data(), static_cast<qsizetype>(identity.size()));
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

struct StreamLocation final {
    int season{-1};
    int voice{-1};
    int episode{-1};
    int stream{-1};
};

std::vector<StreamLocation> StreamLocations(const ResolveResult& result) {
    std::vector<StreamLocation> locations;
    for (std::size_t seasonIndex = 0; seasonIndex < result.entry.seasons.size(); ++seasonIndex) {
        const auto& season = result.entry.seasons[seasonIndex];
        for (std::size_t voiceIndex = 0; voiceIndex < season.voiceTracks.size(); ++voiceIndex) {
            const auto& voice = season.voiceTracks[voiceIndex];
            for (std::size_t episodeIndex = 0; episodeIndex < voice.episodes.size(); ++episodeIndex) {
                const auto& episode = voice.episodes[episodeIndex];
                for (std::size_t streamIndex = 0; streamIndex < episode.streams.size(); ++streamIndex) {
                    locations.push_back({static_cast<int>(seasonIndex), static_cast<int>(voiceIndex),
                                         static_cast<int>(episodeIndex), static_cast<int>(streamIndex)});
                }
            }
        }
    }
    return locations;
}

const StreamVariant* StreamAt(const ResolveResult& result, const StreamLocation& location) {
    if (location.season < 0 || location.voice < 0 || location.episode < 0 || location.stream < 0) return nullptr;
    const auto& seasons = result.entry.seasons;
    if (static_cast<std::size_t>(location.season) >= seasons.size()) return nullptr;
    const auto& voices = seasons[static_cast<std::size_t>(location.season)].voiceTracks;
    if (static_cast<std::size_t>(location.voice) >= voices.size()) return nullptr;
    const auto& episodes = voices[static_cast<std::size_t>(location.voice)].episodes;
    if (static_cast<std::size_t>(location.episode) >= episodes.size()) return nullptr;
    const auto& streams = episodes[static_cast<std::size_t>(location.episode)].streams;
    return static_cast<std::size_t>(location.stream) < streams.size()
        ? &streams[static_cast<std::size_t>(location.stream)] : nullptr;
}

std::optional<StreamLocation> FindRecentLocation(const ResolveResult& result,
                                                  const RecentMediaSelection& wanted) {
    const auto match = MatchRecentMediaSelection(result.entry, wanted);
    return match ? std::optional<StreamLocation>(StreamLocation{
        static_cast<int>(match->season), static_cast<int>(match->voice),
        static_cast<int>(match->episode), static_cast<int>(match->stream)}) : std::nullopt;
}

std::optional<StreamLocation> FindPreferredStream(const ResolveResult& result,
                                                   const RecentMediaSelection& wanted) {
    std::optional<StreamLocation> best;
    int bestScore = -1;
    for (const auto& location : StreamLocations(result)) {
        const auto* stream = StreamAt(result, location);
        if (!stream || stream->protectedStream) continue;
        int score = 0;
        if (!wanted.quality.empty() && stream->quality == wanted.quality) score += 2;
        if (!wanted.protocol.empty() && stream->protocol == wanted.protocol) score += 1;
        if (score > bestScore) {
            best = location;
            bestScore = score;
        }
    }
    return best;
}

std::string ComponentIdentity(const std::string& id, const std::string& title) {
    return id.empty() ? title : id;
}

enum class Glyph {
    Play, Pause, Back, Forward, Volume, Muted, Audio, Subtitles, Hd, Sparkle,
    Statistics, Settings, Fullscreen
};

QIcon MakeIcon(Glyph glyph, QColor color = QColor(244, 244, 246)) {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const QPointF center(32.0, 32.0);
    switch (glyph) {
    case Glyph::Play: {
        QPainterPath path;
        path.moveTo(25.0, 19.0); path.lineTo(25.0, 45.0); path.lineTo(44.0, 32.0); path.closeSubpath();
        painter.drawPath(path);
        break;
    }
    case Glyph::Pause:
        painter.drawLine(QPointF(25.0, 20.0), QPointF(25.0, 44.0));
        painter.drawLine(QPointF(39.0, 20.0), QPointF(39.0, 44.0));
        break;
    case Glyph::Back:
    case Glyph::Forward: {
        const bool forward = glyph == Glyph::Forward;
        painter.drawArc(QRectF(18.0, 18.0, 28.0, 28.0), forward ? 35 * 16 : 145 * 16, 285 * 16);
        QPainterPath arrow;
        if (forward) { arrow.moveTo(43.0, 16.0); arrow.lineTo(48.0, 22.0); arrow.lineTo(40.0, 23.0); }
        else { arrow.moveTo(21.0, 16.0); arrow.lineTo(16.0, 22.0); arrow.lineTo(24.0, 23.0); }
        painter.drawPath(arrow);
        QFont font(QStringLiteral("Segoe UI"), 9, QFont::DemiBold);
        painter.setFont(font);
        painter.drawText(QRectF(18.0, 21.0, 28.0, 25.0), Qt::AlignCenter, QStringLiteral("10"));
        break;
    }
    case Glyph::Volume:
    case Glyph::Muted: {
        QPainterPath speaker;
        speaker.moveTo(17.0, 27.0); speaker.lineTo(24.0, 27.0); speaker.lineTo(32.0, 20.0);
        speaker.lineTo(32.0, 44.0); speaker.lineTo(24.0, 37.0); speaker.lineTo(17.0, 37.0); speaker.closeSubpath();
        painter.drawPath(speaker);
        if (glyph == Glyph::Muted) {
            painter.drawLine(QPointF(40.0, 25.0), QPointF(51.0, 39.0));
            painter.drawLine(QPointF(51.0, 25.0), QPointF(40.0, 39.0));
        } else {
            painter.drawArc(QRectF(27.0, 23.0, 18.0, 18.0), -55 * 16, 110 * 16);
            painter.drawArc(QRectF(25.0, 17.0, 30.0, 30.0), -50 * 16, 100 * 16);
        }
        break;
    }
    case Glyph::Audio:
        painter.drawLine(QPointF(34.0, 17.0), QPointF(34.0, 40.0));
        painter.drawLine(QPointF(34.0, 17.0), QPointF(46.0, 21.0));
        painter.drawEllipse(QRectF(22.0, 37.0, 13.0, 10.0));
        break;
    case Glyph::Subtitles:
        painter.drawRoundedRect(QRectF(14.0, 18.0, 36.0, 28.0), 5.0, 5.0);
        painter.drawLine(QPointF(20.0, 29.0), QPointF(30.0, 29.0));
        painter.drawLine(QPointF(35.0, 29.0), QPointF(44.0, 29.0));
        painter.drawLine(QPointF(20.0, 37.0), QPointF(27.0, 37.0));
        painter.drawLine(QPointF(32.0, 37.0), QPointF(44.0, 37.0));
        break;
    case Glyph::Hd:
        painter.drawRoundedRect(QRectF(13.0, 19.0, 38.0, 26.0), 5.0, 5.0);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 10, QFont::Bold));
        painter.drawText(QRectF(13.0, 18.0, 38.0, 27.0), Qt::AlignCenter, QStringLiteral("HD"));
        break;
    case Glyph::Sparkle: {
        QPainterPath path;
        path.moveTo(32.0, 13.0); path.lineTo(36.0, 28.0); path.lineTo(51.0, 32.0);
        path.lineTo(36.0, 36.0); path.lineTo(32.0, 51.0); path.lineTo(28.0, 36.0);
        path.lineTo(13.0, 32.0); path.lineTo(28.0, 28.0); path.closeSubpath();
        painter.drawPath(path);
        break;
    }
    case Glyph::Statistics:
        painter.drawLine(QPointF(15.0, 47.0), QPointF(49.0, 47.0));
        painter.drawLine(QPointF(22.0, 45.0), QPointF(22.0, 34.0));
        painter.drawLine(QPointF(32.0, 45.0), QPointF(32.0, 27.0));
        painter.drawLine(QPointF(42.0, 45.0), QPointF(42.0, 18.0));
        break;
    case Glyph::Settings:
        painter.drawEllipse(QRectF(20.0, 20.0, 24.0, 24.0));
        painter.drawEllipse(QRectF(28.0, 28.0, 8.0, 8.0));
        for (int index = 0; index < 8; ++index) {
            const double angle = 3.14159265358979323846 * static_cast<double>(index) / 4.0;
            painter.drawLine(center + QPointF(std::cos(angle) * 15.0, std::sin(angle) * 15.0),
                             center + QPointF(std::cos(angle) * 21.0, std::sin(angle) * 21.0));
        }
        break;
    case Glyph::Fullscreen:
        painter.drawLine(QPointF(15.0, 27.0), QPointF(15.0, 15.0)); painter.drawLine(QPointF(15.0, 15.0), QPointF(27.0, 15.0));
        painter.drawLine(QPointF(37.0, 15.0), QPointF(49.0, 15.0)); painter.drawLine(QPointF(49.0, 15.0), QPointF(49.0, 27.0));
        painter.drawLine(QPointF(49.0, 37.0), QPointF(49.0, 49.0)); painter.drawLine(QPointF(49.0, 49.0), QPointF(37.0, 49.0));
        painter.drawLine(QPointF(27.0, 49.0), QPointF(15.0, 49.0)); painter.drawLine(QPointF(15.0, 49.0), QPointF(15.0, 37.0));
        break;
    }
    return QIcon(pixmap);
}

void SetButtonActive(QPushButton* button, bool active) {
    if (!button || button->property("active").toBool() == active) return;
    button->setProperty("active", active);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}

class PolishedSlider final : public QSlider {
public:
    explicit PolishedSlider(QWidget* parent, bool motionEnabled)
        : QSlider(Qt::Horizontal, parent), motionEnabled_(motionEnabled) {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        handleAnimation_.setEasingCurve(QEasingCurve::OutCubic);
        connect(&handleAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            handleEmphasis_ = value.toReal();
            update();
        });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);

        const qreal channelLeft = kHandleRadius + 1.0;
        const qreal channelRight = std::max(channelLeft, static_cast<qreal>(width()) - channelLeft);
        const qreal channelWidth = channelRight - channelLeft;
        const qreal centerY = static_cast<qreal>(height()) / 2.0;
        const QRectF channel(channelLeft, centerY - kTrackHeight / 2.0, channelWidth, kTrackHeight);
        const qreal fraction = maximum() > minimum()
            ? std::clamp(static_cast<qreal>(sliderPosition() - minimum()) /
                             static_cast<qreal>(maximum() - minimum()),
                         0.0, 1.0)
            : 0.0;
        const qreal handleX = channelLeft + channelWidth * fraction;

        painter.setBrush(isEnabled() ? QColor(QStringLiteral("#303036"))
                                     : QColor(QStringLiteral("#242428")));
        painter.drawRoundedRect(channel, kTrackHeight / 2.0, kTrackHeight / 2.0);
        if (handleX > channelLeft) {
            const QRectF progress(channelLeft, channel.top(), handleX - channelLeft, channel.height());
            painter.setBrush(isEnabled() ? QColor(QStringLiteral("#f1f1f3"))
                                         : QColor(QStringLiteral("#66666c")));
            painter.drawRoundedRect(progress, kTrackHeight / 2.0, kTrackHeight / 2.0);
        }

        const qreal radius = kHandleRadius + (kHoveredHandleRadius - kHandleRadius) * handleEmphasis_;
        painter.setPen(QPen(QColor(QStringLiteral("#111114")), 1.0));
        painter.setBrush(isEnabled() ? QColor(QStringLiteral("#ffffff"))
                                     : QColor(QStringLiteral("#77777e")));
        painter.drawEllipse(QPointF(handleX, centerY), radius, radius);
    }

    void enterEvent(QEnterEvent* event) override {
        QSlider::enterEvent(event);
        AnimateHandle(true);
    }

    void leaveEvent(QEvent* event) override {
        QSlider::leaveEvent(event);
        AnimateHandle(isSliderDown());
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) { QSlider::mousePressEvent(event); return; }
        setSliderDown(true);
        AnimateHandle(true);
        SetFromMouse(event->position().x());
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!isSliderDown()) { QSlider::mouseMoveEvent(event); return; }
        SetFromMouse(event->position().x());
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !isSliderDown()) { QSlider::mouseReleaseEvent(event); return; }
        SetFromMouse(event->position().x());
        setSliderDown(false);
        AnimateHandle(underMouse());
        event->accept();
    }

private:
    static constexpr qreal kTrackHeight = 4.0;
    static constexpr qreal kHandleRadius = 6.0;
    static constexpr qreal kHoveredHandleRadius = 8.0;

    void AnimateHandle(bool emphasized) {
        const qreal target = isEnabled() && emphasized ? 1.0 : 0.0;
        if (qAbs(handleEmphasis_ - target) < 0.001) return;
        handleAnimation_.stop();
        if (!motionEnabled_) {
            handleEmphasis_ = target;
            update();
            return;
        }
        handleAnimation_.setDuration(std::max(1, qRound(120.0 * qAbs(handleEmphasis_ - target))));
        handleAnimation_.setStartValue(handleEmphasis_);
        handleAnimation_.setEndValue(target);
        handleAnimation_.start();
    }

    void SetFromMouse(qreal x) {
        const int channelLeft = static_cast<int>(kHandleRadius + 1.0);
        const int channelRight = std::max(channelLeft, width() - channelLeft);
        const int sliderValue = ui::TimelineValueFromPoint(
            static_cast<int>(std::lround(x)), channelLeft, channelRight, minimum(), maximum());
        setValue(sliderValue);
        emit sliderMoved(sliderValue);
    }

    QVariantAnimation handleAnimation_;
    qreal handleEmphasis_{0.0};
    bool motionEnabled_{true};
};

QPushButton* MakeIconButton(QWidget* parent, Glyph glyph, const QString& tooltip) {
    auto* button = new QPushButton(parent);
    button->setProperty("chrome", true);
    button->setIcon(MakeIcon(glyph));
    button->setIconSize(QSize(22, 22));
    button->setFixedSize(38, 38);
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

} // namespace

class UiTransition final {
public:
    UiTransition(QWidget* target, QObject* owner, int slideOffset, std::function<void()> hiddenCallback = {})
        : widget(target), effect(new QGraphicsOpacityEffect(target)), group(new QParallelAnimationGroup(owner)),
          opacity(new QPropertyAnimation(effect, "opacity")), position(new QPropertyAnimation(target, "pos")),
          offset(slideOffset), onHidden(std::move(hiddenCallback)) {
        effect->setOpacity(1.0);
        widget->setGraphicsEffect(effect);
        group->addAnimation(opacity);
        group->addAnimation(position);
    }

    QWidget* widget;
    QGraphicsOpacityEffect* effect;
    QParallelAnimationGroup* group;
    QPropertyAnimation* opacity;
    QPropertyAnimation* position;
    QRect restingGeometry;
    int offset;
    bool targetVisible{false};
    std::function<void()> onHidden;
};

class MpvVideoWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit MpvVideoWidget(QWidget* parent, bool verifyRenderedFrames)
        : QOpenGLWidget(parent), verifyRenderedFrames_(verifyRenderedFrames) {
        setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
        setAutoFillBackground(false);
        setMouseTracking(true);
    }

    ~MpvVideoWidget() override { DetachEngine(); }

    void AttachEngine(MpvEngine& engine) {
        engine_ = &engine;
        presentedFrame_.store(false);
        renderedFrame_.store(false);
        if (!engine_->UsesOpenGlRenderApi() || rendererReady_ || !isValid()) return;
        makeCurrent();
        try {
            CreateRenderer();
        } catch (...) {
            doneCurrent();
            engine_ = nullptr;
            throw;
        }
        doneCurrent();
        update();
    }

    void DetachEngine() noexcept {
        if (!engine_ || !rendererReady_) { engine_ = nullptr; return; }
        makeCurrent();
        engine_->DestroyOpenGlRenderContext();
        rendererReady_ = false;
        doneCurrent();
        engine_ = nullptr;
    }

    void ResetPresentedFrame() noexcept { presentedFrame_.store(false); renderedFrame_.store(false); }
    [[nodiscard]] bool HasPresentedFrame() const noexcept { return presentedFrame_.load(); }
    [[nodiscard]] bool HasRenderedFrame() const noexcept { return renderedFrame_.load(); }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
            if (engine_ && rendererReady_) {
                makeCurrent();
                engine_->DestroyOpenGlRenderContext();
                rendererReady_ = false;
                doneCurrent();
            }
        }, Qt::DirectConnection);
        if (engine_ && engine_->UsesOpenGlRenderApi()) CreateRenderer();
    }

    void paintGL() override {
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!engine_ || !rendererReady_) return;
        while (glGetError() != GL_NO_ERROR) {}
        const qreal scale = devicePixelRatioF();
        const int pixelWidth = std::max(1, qRound(static_cast<qreal>(width()) * scale));
        const int pixelHeight = std::max(1, qRound(static_cast<qreal>(height()) * scale));
        try {
            if (engine_->RenderOpenGl(static_cast<int>(defaultFramebufferObject()),
                                      pixelWidth, pixelHeight)) presentedFrame_.store(true);
            if (verifyRenderedFrames_ && !renderedFrame_.load()) {
                glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
                const std::array<QPoint, 5> samples{
                    QPoint(pixelWidth / 2, pixelHeight / 2),
                    QPoint(pixelWidth / 4, pixelHeight / 4),
                    QPoint(pixelWidth * 3 / 4, pixelHeight / 4),
                    QPoint(pixelWidth / 4, pixelHeight * 3 / 4),
                    QPoint(pixelWidth * 3 / 4, pixelHeight * 3 / 4)
                };
                for (const auto& sample : samples) {
                    std::array<GLubyte, 4> pixel{};
                    glReadPixels(sample.x(), sample.y(), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
                    if (pixel[0] > 4 || pixel[1] > 4 || pixel[2] > 4) {
                        renderedFrame_.store(true);
                        break;
                    }
                }
            }
        } catch (...) {
            // Rendering errors are surfaced by the libmpv event/log path. Do
            // not throw through Qt's paint dispatch.
        }
    }

private:
    static void* ResolveOpenGlFunction(void*, const char* name) {
        auto* current = QOpenGLContext::currentContext();
        if (!current || !name) return nullptr;
        const auto address = current->getProcAddress(QByteArray(name));
        static_assert(sizeof(address) == sizeof(void*));
        return std::bit_cast<void*>(address);
    }

    static void RequestUpdate(void* context) {
        auto* widget = static_cast<MpvVideoWidget*>(context);
        if (!widget) return;
        QMetaObject::invokeMethod(widget, [widget] { widget->update(); }, Qt::QueuedConnection);
    }

    void CreateRenderer() {
        if (!engine_ || rendererReady_) return;
        mpv_opengl_init_params initialization{&ResolveOpenGlFunction, nullptr};
        engine_->CreateOpenGlRenderContext(&initialization, &RequestUpdate, this);
        rendererReady_ = true;
    }

    MpvEngine* engine_{nullptr};
    bool rendererReady_{false};
    bool verifyRenderedFrames_{false};
    std::atomic_bool presentedFrame_{false};
    std::atomic_bool renderedFrame_{false};
};

QtPlayerWindow::QtPlayerWindow(AppPaths paths, Config config, Logger& logger,
                               bool backgroundTest, bool motionTest)
    : paths_(std::move(paths)), config_(std::move(config)), logger_(logger),
      shaders_(paths_.shaders, paths_.presets / "shaders.json"),
      ytDlp_(paths_.tools / "yt-dlp.exe"), resolvers_(&ytDlp_), engine_(paths_, config_, logger_),
      backgroundTest_(backgroundTest),
      motionEnabled_(motionTest ||
                     (!backgroundTest && config_.GetBool("ui.animations", true) && SystemAnimationsEnabled())),
      motionTest_(motionTest) {
    shaders_.Reload();
    const auto configuredShader = config_.GetString("shader.preset", "off");
    if (const auto* preset = shaders_.Find(configuredShader)) {
        const auto iterator = std::ranges::find_if(shaders_.Presets(), [preset](const ShaderPreset& item) {
            return item.id == preset->id;
        });
        shaderPresetIndex_ = iterator == shaders_.Presets().end()
            ? 0U : static_cast<std::size_t>(std::distance(shaders_.Presets().begin(), iterator));
    } else {
        logger_.Write(LogLevel::Error, "config", "unknown shader preset; using off");
    }
    resumeEnabled_ = config_.GetBool("playback.resume", true);
    if (resumeEnabled_) {
        try {
            playbackState_ = PlaybackStateStore::Load(paths_.config / "playback-state.json");
        } catch (const std::exception& error) {
            playbackStateAvailable_ = false;
            resumeEnabled_ = false;
            logger_.Write(LogLevel::Error, "playback", std::string("resume state disabled: ") + error.what());
        }
    }
    try {
        recentMedia_ = RecentMediaStore::Load(paths_.config / "recent-media.json");
    } catch (const std::exception& error) {
        recentMediaAvailable_ = false;
        logger_.Write(LogLevel::Error, "playback", std::string("recent media disabled: ") + error.what());
    }
    resolvers_.Add(std::make_unique<DirectMediaResolver>());
    resolvers_.Add(std::make_unique<AnimeGoResolver>());
    resolvers_.Add(std::make_unique<BrowserEmbedResolver>(paths_.cache / "webview2"));
    resolvers_.Add(std::make_unique<CdnVideoHubResolver>());
    resolvers_.Add(std::make_unique<AniBoomResolver>());
    resolvers_.Add(std::make_unique<YummyAnimeResolver>());
    resolvers_.Add(std::make_unique<GenericResolver>());

    setObjectName(QStringLiteral("playerRoot"));
    setWindowTitle(QStringLiteral("WannaViewer"));
    setMinimumSize(780, 500);
    resize(1280, 760);
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    if (backgroundTest_) {
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
    }
    BuildUi();
    ConnectUi();
    ApplyTheme();
    qApp->installEventFilter(this);
    LogHardwareInformation();
}

QtPlayerWindow::~QtPlayerWindow() {
    closing_.store(true);
    qApp->removeEventFilter(this);
    if (resolverThread_.joinable()) { resolverThread_.request_stop(); resolverThread_.join(); }
    if (engineInitializationThread_.joinable()) engineInitializationThread_.join();
    videoSurface_->DetachEngine();
    engine_.Shutdown();
}

void QtPlayerWindow::BuildUi() {
    videoSurface_ = new MpvVideoWidget(this, backgroundTest_);
    videoSurface_->setObjectName(QStringLiteral("videoSurface"));

    controls_ = new QFrame(this);
    controls_->setObjectName(QStringLiteral("controls"));
    controls_->setMouseTracking(true);
    auto* controlsLayout = new QVBoxLayout(controls_);
    controlsLayout->setContentsMargins(18, 10, 18, 10);
    controlsLayout->setSpacing(5);
    timeline_ = new PolishedSlider(controls_, motionEnabled_);
    timeline_->setObjectName(QStringLiteral("timeline"));
    timeline_->setRange(0, 10000);
    timeline_->setFixedHeight(22);
    controlsLayout->addWidget(timeline_);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(5);
    playButton_ = MakeIconButton(controls_, Glyph::Play, QStringLiteral("Play / Pause"));
    rewindButton_ = MakeIconButton(controls_, Glyph::Back, QStringLiteral("Back 10 seconds"));
    forwardButton_ = MakeIconButton(controls_, Glyph::Forward, QStringLiteral("Forward 10 seconds"));
    muteButton_ = MakeIconButton(controls_, Glyph::Volume, QStringLiteral("Mute"));
    volume_ = new PolishedSlider(controls_, motionEnabled_);
    volume_->setObjectName(QStringLiteral("volume"));
    volume_->setRange(0, 100);
    volume_->setValue(static_cast<int>(config_.GetInt("playback.volume", 80, 0, 100)));
    volume_->setFixedWidth(86);
    volume_->setFixedHeight(22);
    timeLabel_ = new QLabel(QStringLiteral("00:00  /  00:00"), controls_);
    timeLabel_->setObjectName(QStringLiteral("timeLabel"));
    timeLabel_->setMinimumWidth(116);
    audioButton_ = MakeIconButton(controls_, Glyph::Audio, QStringLiteral("Audio track"));
    subtitleButton_ = MakeIconButton(controls_, Glyph::Subtitles, QStringLiteral("Subtitles"));
    videoButton_ = MakeIconButton(controls_, Glyph::Hd, QStringLiteral("Video track"));
    shaderButton_ = MakeIconButton(controls_, Glyph::Sparkle, QStringLiteral("Shaders"));
    statsButton_ = MakeIconButton(controls_, Glyph::Statistics, QStringLiteral("Statistics"));
    settingsButton_ = MakeIconButton(controls_, Glyph::Settings, QStringLiteral("Settings"));
    fullscreenButton_ = MakeIconButton(controls_, Glyph::Fullscreen, QStringLiteral("Full screen"));
    const std::array<QWidget*, 13> playbackControls{
        playButton_, rewindButton_, forwardButton_, muteButton_, volume_, audioButton_, subtitleButton_,
        videoButton_, shaderButton_, statsButton_, settingsButton_, fullscreenButton_, timeline_};
    for (QWidget* widget : playbackControls) widget->setEnabled(false);
    row->addWidget(playButton_); row->addWidget(rewindButton_); row->addWidget(forwardButton_);
    row->addWidget(muteButton_); row->addWidget(volume_); row->addSpacing(8); row->addWidget(timeLabel_);
    row->addStretch(1);
    row->addWidget(audioButton_); row->addWidget(subtitleButton_); row->addWidget(videoButton_);
    row->addWidget(shaderButton_); row->addWidget(statsButton_); row->addWidget(settingsButton_);
    row->addWidget(fullscreenButton_);
    controlsLayout->addLayout(row);

    controlsOpacity_ = new QGraphicsOpacityEffect(controls_);
    controlsOpacity_->setOpacity(1.0);
    controls_->setGraphicsEffect(controlsOpacity_);
    controlsAnimation_ = new QPropertyAnimation(controlsOpacity_, "opacity", this);
    controlsAnimation_->setEasingCurve(QEasingCurve::OutCubic);

    emptyState_ = new QFrame(this);
    emptyState_->setObjectName(QStringLiteral("emptyState"));
    auto* emptyLayout = new QVBoxLayout(emptyState_);
    emptyLayout->setContentsMargins(34, 28, 34, 28);
    emptyLayout->setSpacing(9);
    emptyPlayIcon_ = MakeIconButton(emptyState_, Glyph::Play, QStringLiteral("Open a video"));
    emptyPlayIcon_->setObjectName(QStringLiteral("emptyPlay"));
    emptyPlayIcon_->setIconSize(QSize(32, 32));
    emptyPlayIcon_->setFixedSize(58, 58);
    auto* emptyTitle = new QLabel(QStringLiteral("Open a video"), emptyState_);
    emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto* emptyHint = new QLabel(QStringLiteral("Drop a media file here, or open a local file or link"), emptyState_);
    emptyHint->setObjectName(QStringLiteral("mutedText"));
    emptyHint->setAlignment(Qt::AlignCenter);
    openFileButton_ = new QPushButton(QStringLiteral("Open file"), emptyState_);
    openFileButton_->setObjectName(QStringLiteral("primaryButton"));
    openUrlButton_ = new QPushButton(QStringLiteral("Open link"), emptyState_);
    auto* emptyButtons = new QHBoxLayout();
    emptyButtons->setSpacing(10);
    emptyButtons->addWidget(openFileButton_); emptyButtons->addWidget(openUrlButton_);
    recentTitle_ = new QLabel(QStringLiteral("Recent"), emptyState_);
    recentTitle_->setObjectName(QStringLiteral("recentTitle"));
    recentList_ = new QListWidget(emptyState_);
    recentList_->setObjectName(QStringLiteral("recentList"));
    recentList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    recentList_->setSelectionMode(QAbstractItemView::SingleSelection);
    recentOpen_ = new QPushButton(QStringLiteral("Open"), emptyState_);
    recentOpen_->setObjectName(QStringLiteral("primaryButton"));
    recentRemove_ = new QPushButton(QStringLiteral("Remove"), emptyState_);
    recentClear_ = new QPushButton(QStringLiteral("Clear recent"), emptyState_);
    auto* recentButtons = new QHBoxLayout();
    recentButtons->setSpacing(8);
    recentButtons->addWidget(recentOpen_);
    recentButtons->addWidget(recentRemove_);
    recentButtons->addStretch(1);
    recentButtons->addWidget(recentClear_);
    emptyLayout->addWidget(emptyPlayIcon_, 0, Qt::AlignHCenter);
    emptyLayout->addWidget(emptyTitle); emptyLayout->addWidget(emptyHint); emptyLayout->addSpacing(8);
    emptyLayout->addLayout(emptyButtons);
    emptyLayout->addSpacing(7);
    emptyLayout->addWidget(recentTitle_);
    emptyLayout->addWidget(recentList_, 1);
    emptyLayout->addLayout(recentButtons);

    openingState_ = new QFrame(this);
    openingState_->setObjectName(QStringLiteral("openingState"));
    auto* openingLayout = new QVBoxLayout(openingState_);
    openingLayout->setContentsMargins(28, 22, 28, 22);
    openingLayout->setSpacing(8);
    openingTitle_ = new QLabel(QStringLiteral("Opening video"), openingState_);
    openingTitle_->setObjectName(QStringLiteral("openingTitle"));
    openingStatus_ = new QLabel(QStringLiteral("Preparing playback…"), openingState_);
    openingStatus_->setObjectName(QStringLiteral("mutedText"));
    openingStatus_->setWordWrap(true);
    openingProgress_ = new QProgressBar(openingState_);
    openingProgress_->setObjectName(QStringLiteral("openingProgress"));
    openingProgress_->setRange(0, 0);
    openingProgress_->setTextVisible(false);
    openingProgress_->setFixedHeight(4);
    openingProgress_->setVisible(motionEnabled_);
    openingLayout->addWidget(openingTitle_);
    openingLayout->addWidget(openingStatus_);
    openingLayout->addSpacing(2);
    openingLayout->addWidget(openingProgress_);

    statistics_ = new QLabel(this);
    statistics_->setObjectName(QStringLiteral("statistics"));
    statistics_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    statistics_->setWordWrap(false);
    statistics_->hide();

    playbackFeedback_ = new QLabel(this);
    playbackFeedback_->setObjectName(QStringLiteral("playbackFeedback"));
    playbackFeedback_->setAlignment(Qt::AlignCenter);
    playbackFeedback_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    playbackFeedback_->hide();

    modalScrim_ = new QFrame(this);
    modalScrim_->setObjectName(QStringLiteral("modalScrim"));
    modalScrim_->setFocusPolicy(Qt::NoFocus);
    modalScrim_->hide();

    overlay_ = new QFrame(this);
    overlay_->setObjectName(QStringLiteral("overlayPanel"));
    auto* overlayLayout = new QVBoxLayout(overlay_);
    overlayLayout->setContentsMargins(28, 26, 28, 24);
    overlayLayout->setSpacing(11);
    overlayTitle_ = new QLabel(overlay_);
    overlayTitle_->setObjectName(QStringLiteral("panelTitle"));
    overlayBody_ = new QLabel(overlay_);
    overlayBody_->setObjectName(QStringLiteral("mutedText"));
    overlayBody_->setWordWrap(true);
    overlayEdit_ = new QLineEdit(overlay_);
    overlayEdit_->setPlaceholderText(QStringLiteral("https://"));
    overlayList_ = new QListWidget(overlay_);
    overlayList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    overlayPrimary_ = new QPushButton(QStringLiteral("Apply"), overlay_);
    overlayPrimary_->setObjectName(QStringLiteral("primaryButton"));
    overlaySecondary_ = new QPushButton(QStringLiteral("Cancel"), overlay_);
    auto* overlayButtons = new QHBoxLayout();
    overlayButtons->addStretch(1); overlayButtons->addWidget(overlaySecondary_); overlayButtons->addWidget(overlayPrimary_);
    overlayLayout->addWidget(overlayTitle_); overlayLayout->addWidget(overlayBody_);
    overlayLayout->addWidget(overlayEdit_); overlayLayout->addWidget(overlayList_, 1);
    overlayLayout->addLayout(overlayButtons);
    overlay_->hide();

    sourcePanel_ = new QFrame(this);
    sourcePanel_->setObjectName(QStringLiteral("sourcePanel"));
    auto* sourceLayout = new QVBoxLayout(sourcePanel_);
    sourceLayout->setContentsMargins(30, 26, 30, 24);
    sourceLayout->setSpacing(13);
    sourceTitle_ = new QLabel(QStringLiteral("Choose an episode"), sourcePanel_);
    sourceTitle_->setObjectName(QStringLiteral("panelTitle"));
    auto* sourceForm = new QFormLayout();
    sourceForm->setHorizontalSpacing(18); sourceForm->setVerticalSpacing(12);
    sourceSeason_ = new QComboBox(sourcePanel_); sourceVoice_ = new QComboBox(sourcePanel_);
    sourceEpisode_ = new QComboBox(sourcePanel_); sourceStream_ = new QComboBox(sourcePanel_);
    sourceForm->addRow(QStringLiteral("Season"), sourceSeason_);
    sourceForm->addRow(QStringLiteral("Voice"), sourceVoice_);
    sourceForm->addRow(QStringLiteral("Episode"), sourceEpisode_);
    sourceForm->addRow(QStringLiteral("Source"), sourceStream_);
    sourceStatus_ = new QLabel(sourcePanel_);
    sourceStatus_->setObjectName(QStringLiteral("mutedText"));
    sourceStatus_->setWordWrap(true);
    sourceOpen_ = new QPushButton(QStringLiteral("Open"), sourcePanel_);
    sourceOpen_->setObjectName(QStringLiteral("primaryButton"));
    sourceCancel_ = new QPushButton(QStringLiteral("Cancel"), sourcePanel_);
    auto* sourceButtons = new QHBoxLayout();
    sourceButtons->addStretch(1); sourceButtons->addWidget(sourceCancel_); sourceButtons->addWidget(sourceOpen_);
    sourceLayout->addWidget(sourceTitle_); sourceLayout->addLayout(sourceForm); sourceLayout->addWidget(sourceStatus_);
    sourceLayout->addStretch(1); sourceLayout->addLayout(sourceButtons);
    sourcePanel_->hide();

    emptyTransition_ = std::make_unique<UiTransition>(emptyState_, this, 10);
    openingTransition_ = std::make_unique<UiTransition>(openingState_, this, 10);
    scrimTransition_ = std::make_unique<UiTransition>(modalScrim_, this, 0);
    overlayTransition_ = std::make_unique<UiTransition>(overlay_, this, 16,
                                                        [this] { ClearOverlayWidgets(); });
    sourceTransition_ = std::make_unique<UiTransition>(sourcePanel_, this, 16,
                                                       [this] { ClearSourceSelectorWidgets(); });
    statisticsTransition_ = std::make_unique<UiTransition>(statistics_, this, 10);
    feedbackTransition_ = std::make_unique<UiTransition>(playbackFeedback_, this, 8);
    const std::array<UiTransition*, 7> transitions{
        emptyTransition_.get(), openingTransition_.get(), scrimTransition_.get(),
        overlayTransition_.get(), sourceTransition_.get(), statisticsTransition_.get(), feedbackTransition_.get()};
    for (UiTransition* transition : transitions) {
        connect(transition->group, &QParallelAnimationGroup::finished, this, [transition] {
            transition->widget->setGeometry(transition->restingGeometry);
            if (transition->targetVisible) {
                transition->effect->setOpacity(1.0);
                transition->widget->show();
            } else {
                transition->effect->setOpacity(0.0);
                transition->widget->hide();
                if (transition->onHidden) transition->onHidden();
            }
        });
    }

    hideTimer_ = new QTimer(this);
    hideTimer_->setSingleShot(true);
    hideTimer_->setInterval(kControlsHideDelayMs);
    feedbackTimer_ = new QTimer(this);
    feedbackTimer_->setSingleShot(true);
    feedbackTimer_->setInterval(kFeedbackDurationMs);
    progressSaveTimer_ = new QTimer(this);
    progressSaveTimer_->setInterval(kProgressSaveIntervalMs);
    progressSaveTimer_->setTimerType(Qt::VeryCoarseTimer);
    settingsSaveTimer_ = new QTimer(this);
    settingsSaveTimer_->setSingleShot(true);
    settingsSaveTimer_->setInterval(kSettingsSaveDelayMs);
    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(kUiIntervalMs);
    uiTimer_->setTimerType(Qt::PreciseTimer);
    LayoutOverlays();
    RefreshRecentMedia();
    UpdateVisibility();
}

void QtPlayerWindow::ConnectUi() {
    connect(openFileButton_, &QPushButton::clicked, this, [this] { OpenFileDialog(); });
    connect(emptyPlayIcon_, &QPushButton::clicked, this, [this] { OpenFileDialog(); });
    connect(openUrlButton_, &QPushButton::clicked, this, [this] { ShowUrlOverlay(); });
    connect(recentOpen_, &QPushButton::clicked, this, [this] { OpenSelectedRecent(); });
    connect(recentRemove_, &QPushButton::clicked, this, [this] { RemoveSelectedRecent(); });
    connect(recentClear_, &QPushButton::clicked, this, [this] { ClearRecentMedia(); });
    connect(recentList_, &QListWidget::currentRowChanged, this, [this](int) { UpdateRecentActions(); });
    connect(recentList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        OpenSelectedRecent();
    });
    connect(playButton_, &QPushButton::clicked, this, [this] { TogglePlayback(); });
    connect(rewindButton_, &QPushButton::clicked, this, [this] { SeekRelative(-10.0); });
    connect(forwardButton_, &QPushButton::clicked, this, [this] { SeekRelative(10.0); });
    connect(muteButton_, &QPushButton::clicked, this, [this] { ToggleMute(); });
    connect(volume_, &QSlider::valueChanged, this, [this](int value) {
        if (engineReady_ && mediaLoaded_) engine_.SetVolume(static_cast<double>(value));
        if (mediaLoaded_) ShowPlaybackFeedback(QStringLiteral("Volume  ·  %1%").arg(value));
        settingsDirty_ = true;
        settingsSaveTimer_->start();
        RecordInteraction();
    });
    connect(timeline_, &QSlider::sliderPressed, this, [this] { timelineDragging_ = true; RecordInteraction(); });
    connect(timeline_, &QSlider::sliderMoved, this, [this](int) { SeekFromSlider(false); });
    connect(timeline_, &QSlider::sliderReleased, this, [this] { SeekFromSlider(true); });
    connect(audioButton_, &QPushButton::clicked, this, [this] { ShowAudioMenu(); });
    connect(subtitleButton_, &QPushButton::clicked, this, [this] { ShowSubtitleMenu(); });
    connect(videoButton_, &QPushButton::clicked, this, [this] { ShowVideoMenu(); });
    connect(shaderButton_, &QPushButton::clicked, this, [this] { ShowShaderMenu(); });
    connect(statsButton_, &QPushButton::clicked, this, [this] { ToggleStatistics(); });
    connect(settingsButton_, &QPushButton::clicked, this, [this] { ShowSettingsMenu(); });
    connect(fullscreenButton_, &QPushButton::clicked, this, [this] { ToggleFullscreen(); });
    connect(overlayPrimary_, &QPushButton::clicked, this, [this] { ApplyOverlaySelection(); });
    connect(overlaySecondary_, &QPushButton::clicked, this, [this] { HideOverlay(); });
    connect(overlayEdit_, &QLineEdit::returnPressed, this, [this] { ApplyOverlaySelection(); });
    connect(overlayList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { ApplyOverlaySelection(); });
    connect(overlayList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (overlayAction_ == OverlayAction::Resume)
            overlayPrimary_->setText(row == 1 ? QStringLiteral("Start over") : QStringLiteral("Continue"));
    });
    connect(sourceCancel_, &QPushButton::clicked, this, [this] { HideSourceSelector(); });
    connect(sourceOpen_, &QPushButton::clicked, this, [this] { OpenSelectedSource(); });
    connect(sourceSeason_, &QComboBox::currentIndexChanged, this, [this](int value) {
        if (!sourceSelection_) return;
        sourceSeasonIndex_ = value; sourceVoiceIndex_ = sourceEpisodeIndex_ = sourceStreamIndex_ = -1;
        PopulateSourceVoices(); PopulateSourceEpisodes(); PopulateSourceStreams();
    });
    connect(sourceVoice_, &QComboBox::currentIndexChanged, this, [this](int value) {
        if (!sourceSelection_) return;
        sourceVoiceIndex_ = value; sourceEpisodeIndex_ = sourceStreamIndex_ = -1;
        PopulateSourceEpisodes(); PopulateSourceStreams();
    });
    connect(sourceEpisode_, &QComboBox::currentIndexChanged, this, [this](int value) {
        if (!sourceSelection_) return;
        sourceEpisodeIndex_ = value; sourceStreamIndex_ = -1; PopulateSourceStreams();
    });
    connect(sourceStream_, &QComboBox::currentIndexChanged, this, [this](int value) {
        if (!sourceSelection_) return;
        sourceStreamIndex_ = value;
        UpdateSourceSelectionUi();
    });
    connect(hideTimer_, &QTimer::timeout, this, [this] {
        if (CursorOverControls() || overlayMode_ != OverlayMode::None || sourceSelection_) {
            hideTimer_->start(250);
            return;
        }
        ShowControls(false);
    });
    connect(uiTimer_, &QTimer::timeout, this, [this] { UpdateUi(); });
    connect(feedbackTimer_, &QTimer::timeout, this, [this] { HidePlaybackFeedback(); });
    connect(progressSaveTimer_, &QTimer::timeout, this, [this] { SavePlaybackProgress(); });
    connect(settingsSaveTimer_, &QTimer::timeout, this, [this] { SaveUserSettings(); });
    connect(controlsAnimation_, &QPropertyAnimation::finished, this, [this] {
        if (!controlsVisible_) controls_->hide();
    });
}

void QtPlayerWindow::ApplyTheme() {
    setStyleSheet(QStringLiteral(R"(
        QWidget#playerRoot, QWidget#videoSurface { background: #000000; color: #f4f4f6; }
        QWidget { font-family: "Segoe UI Variable Text", "Segoe UI"; font-size: 14px; }
        QFrame#controls { background: rgba(5, 5, 6, 246); border: 1px solid #1c1c20; border-radius: 18px; }
        QPushButton { min-height: 34px; padding: 0 16px; color: #f4f4f6; background: #121216;
                      border: 1px solid #34343a; border-radius: 17px; }
        QPushButton:hover { background: #24242a; border-color: #55555d; }
        QPushButton:pressed { background: #303037; }
        QPushButton:disabled { color: #66666d; background: transparent; border-color: transparent; }
        QPushButton[chrome="true"] { padding: 0; background: transparent; border: none; border-radius: 19px; }
        QPushButton[chrome="true"][active="true"] { background: #3a3a42; }
        QPushButton[chrome="true"][active="true"]:hover { background: #4a4a54; }
        QPushButton[chrome="true"]:hover { background: #24242a; }
        QPushButton[chrome="true"]:pressed { background: #34343b; }
        QPushButton#primaryButton { background: #f4f4f6; color: #070708; border-color: #f4f4f6; font-weight: 600; }
        QPushButton#primaryButton:hover { background: #ffffff; }
        QPushButton#emptyPlay { padding: 0; border-radius: 29px; background: #17171b; border-color: #424248; }
        QLabel#emptyTitle, QLabel#panelTitle { font-size: 24px; font-weight: 600; }
        QLabel#recentTitle { font-size: 16px; font-weight: 600; }
        QLabel#mutedText { color: #a2a2aa; }
        QLabel#timeLabel { color: #ededf0; font-variant-numeric: tabular-nums; }
        QFrame#emptyState, QFrame#openingState, QFrame#overlayPanel, QFrame#sourcePanel {
            background: #0b0b0e; border: 1px solid #36363d; border-radius: 22px;
        }
        QFrame#modalScrim { background: rgba(0, 0, 0, 176); border: none; }
        QLabel#openingTitle { font-size: 18px; font-weight: 600; }
        QProgressBar#openingProgress { background: #242429; border: none; border-radius: 2px; }
        QProgressBar#openingProgress::chunk { background: #ececf0; border-radius: 2px; }
        QLabel#statistics { background: rgba(7, 7, 9, 230); border: 1px solid #34343a;
                            border-radius: 14px; padding: 14px; color: #e8e8ec;
                            font-family: "Cascadia Mono", "Consolas"; font-size: 12px; }
        QLabel#playbackFeedback { background: rgba(12, 12, 15, 235); border: 1px solid #42424a;
                                  border-radius: 18px; padding: 10px 18px; color: #f4f4f6;
                                  font-size: 15px; font-weight: 600; }
        QLineEdit, QComboBox, QListWidget { color: #f2f2f4; background: #121216; border: 1px solid #383840;
                                           border-radius: 12px; padding: 8px 11px; selection-background-color: #33333a; }
        QLineEdit:focus, QComboBox:focus, QListWidget:focus { border-color: #73737d; }
        QComboBox { min-height: 28px; }
        QComboBox::drop-down { border: none; width: 28px; }
        QComboBox QAbstractItemView { background: #101014; border: 1px solid #3b3b42;
                                     selection-background-color: #2b2b31; outline: none; padding: 6px; }
        QListWidget { outline: none; padding: 7px; }
        QListWidget::item { min-height: 34px; border-radius: 10px; padding: 2px 10px; }
        QListWidget::item:hover { background: #202025; }
        QListWidget::item:selected { background: #303037; }
        QListWidget#recentList::item { min-height: 46px; padding: 5px 10px; }
        QToolTip { color: #f4f4f6; background: #151519; border: 1px solid #3b3b42; border-radius: 8px; padding: 6px; }
    )"));
}

void QtPlayerWindow::LayoutOverlays() {
    videoSurface_->setGeometry(rect());
    const int controlsWidth = std::max(0, width() - kControlsMargin * 2);
    controls_->setGeometry(kControlsMargin, std::max(kControlsMargin, height() - kControlsHeight - kControlsMargin),
                           controlsWidth, kControlsHeight);
    const bool hasRecentMedia = recentMediaAvailable_ && recentMedia_.Size() != 0;
    const int preferredEmptyHeight = hasRecentMedia ? 520 : 252;
    const QSize emptySize(std::min(hasRecentMedia ? 620 : 520, std::max(320, width() - 48)),
                          std::min(preferredEmptyHeight, std::max(252, height() - 48)));
    const auto updateTransitionGeometry = [](UiTransition& transition, const QRect& geometry) {
        transition.restingGeometry = geometry;
        if (transition.group->state() != QAbstractAnimation::Running) transition.widget->setGeometry(geometry);
    };
    updateTransitionGeometry(*emptyTransition_,
                             QRect((width() - emptySize.width()) / 2, (height() - emptySize.height()) / 2,
                                   emptySize.width(), emptySize.height()));
    const QSize openingSize(std::min(440, std::max(320, width() - 48)), 112);
    updateTransitionGeometry(*openingTransition_,
                             QRect((width() - openingSize.width()) / 2, (height() - openingSize.height()) / 2,
                                   openingSize.width(), openingSize.height()));
    updateTransitionGeometry(*scrimTransition_, rect());
    const int preferredOverlayHeight = overlayMode_ == OverlayMode::Url ? 270 :
                                       overlayMode_ == OverlayMode::Message ? 260 : 540;
    const int preferredOverlayWidth = overlayMode_ == OverlayMode::Choice ? 720 : 660;
    const QSize overlaySize(std::min(preferredOverlayWidth, std::max(420, width() - 64)),
                            std::min(preferredOverlayHeight, std::max(240, height() - 80)));
    updateTransitionGeometry(*overlayTransition_,
                             QRect((width() - overlaySize.width()) / 2, (height() - overlaySize.height()) / 2,
                                   overlaySize.width(), overlaySize.height()));
    const QSize sourceSize(std::min(760, std::max(440, width() - 64)),
                           std::min(430, std::max(360, height() - 80)));
    updateTransitionGeometry(*sourceTransition_,
                             QRect((width() - sourceSize.width()) / 2, (height() - sourceSize.height()) / 2,
                                   sourceSize.width(), sourceSize.height()));
    updateTransitionGeometry(*statisticsTransition_,
                             QRect(18, 18, std::min(560, std::max(320, width() - 36)),
                                    std::min(350, std::max(220, height() - 36))));
    const QSize feedbackHint = playbackFeedback_->sizeHint().expandedTo(QSize(150, 48));
    const QSize feedbackSize(std::min(std::max(150, feedbackHint.width()), std::max(150, width() - 48)),
                             feedbackHint.height());
    updateTransitionGeometry(*feedbackTransition_,
                             QRect((width() - feedbackSize.width()) / 2,
                                   std::max(24, (height() - feedbackSize.height()) / 2),
                                   feedbackSize.width(), feedbackSize.height()));
    videoSurface_->lower();
    if (emptyState_->isVisible()) emptyState_->raise();
    if (openingState_->isVisible()) openingState_->raise();
    if (controls_->isVisible()) controls_->raise();
    if (statistics_->isVisible()) statistics_->raise();
    if (playbackFeedback_->isVisible()) playbackFeedback_->raise();
    if (modalScrim_->isVisible()) modalScrim_->raise();
    if (sourcePanel_->isVisible()) sourcePanel_->raise();
    if (overlay_->isVisible()) overlay_->raise();
}

void QtPlayerWindow::UpdateVisibility() {
    LayoutOverlays();
    const bool modalVisible = sourceSelection_.has_value() || overlayMode_ != OverlayMode::None;
    if (modalVisible && feedbackVisible_) {
        feedbackVisible_ = false;
        feedbackTimer_->stop();
    }
    SetTransitionVisible(*emptyTransition_,
                         !mediaLoaded_ && !mediaOpening_ && !sourceSelection_ && overlayMode_ == OverlayMode::None);
    SetTransitionVisible(*openingTransition_, mediaOpening_ && !sourceSelection_ && overlayMode_ == OverlayMode::None);
    controls_->setVisible(mediaLoaded_ && controlsVisible_ && !sourceSelection_ && overlayMode_ == OverlayMode::None);
    SetTransitionVisible(*statisticsTransition_,
                         statisticsVisible_ && mediaLoaded_ && !sourceSelection_ && overlayMode_ == OverlayMode::None);
    SetTransitionVisible(*feedbackTransition_,
                         feedbackVisible_ && (mediaLoaded_ || backgroundTest_) && !modalVisible);
    SetTransitionVisible(*scrimTransition_, modalVisible);
    SetTransitionVisible(*sourceTransition_, sourceSelection_.has_value());
    SetTransitionVisible(*overlayTransition_, overlayMode_ != OverlayMode::None);
    LayoutOverlays();
}

void QtPlayerWindow::SetTransitionVisible(UiTransition& transition, bool visible) {
    const bool settled = transition.group->state() != QAbstractAnimation::Running &&
                         transition.widget->isVisible() == visible &&
                         qAbs(transition.effect->opacity() - (visible ? 1.0 : 0.0)) < 0.001;
    if (transition.targetVisible == visible &&
        (transition.group->state() == QAbstractAnimation::Running || settled)) return;

    transition.targetVisible = visible;
    transition.group->stop();
    if (!motionEnabled_ || !isVisible()) {
        transition.widget->setGeometry(transition.restingGeometry);
        transition.effect->setOpacity(visible ? 1.0 : 0.0);
        transition.widget->setVisible(visible);
        if (!visible && transition.onHidden) transition.onHidden();
        return;
    }

    const bool wasVisible = transition.widget->isVisible();
    const qreal startOpacity = wasVisible ? transition.effect->opacity() : 0.0;
    const QPoint restingPosition = transition.restingGeometry.topLeft();
    const QPoint startPosition = wasVisible ? transition.widget->pos()
                                            : restingPosition + QPoint(0, transition.offset);
    const QPoint endPosition = visible ? restingPosition
                                       : restingPosition + QPoint(0, transition.offset);
    if (visible) {
        transition.widget->setGeometry(transition.restingGeometry);
        transition.widget->move(startPosition);
        transition.widget->show();
        transition.widget->raise();
    } else if (!wasVisible) {
        transition.effect->setOpacity(0.0);
        transition.widget->setGeometry(transition.restingGeometry);
        if (transition.onHidden) transition.onHidden();
        return;
    }

    const int duration = visible ? kPanelEnterDurationMs : kPanelExitDurationMs;
    transition.opacity->setDuration(duration);
    transition.opacity->setEasingCurve(visible ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    transition.opacity->setStartValue(startOpacity);
    transition.opacity->setEndValue(visible ? 1.0 : 0.0);
    transition.position->setDuration(duration);
    transition.position->setEasingCurve(visible ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    transition.position->setStartValue(startPosition);
    transition.position->setEndValue(endPosition);
    transition.group->start();
}

void QtPlayerWindow::StopUiAnimations() {
    controlsAnimation_->stop();
    const std::array<UiTransition*, 7> transitions{
        emptyTransition_.get(), openingTransition_.get(), scrimTransition_.get(),
        overlayTransition_.get(), sourceTransition_.get(), statisticsTransition_.get(), feedbackTransition_.get()};
    for (UiTransition* transition : transitions) {
        transition->group->stop();
        transition->widget->setGeometry(transition->restingGeometry);
        transition->effect->setOpacity(transition->targetVisible ? 1.0 : 0.0);
        transition->widget->setVisible(transition->targetVisible);
    }
}

void QtPlayerWindow::ClearOverlayWidgets() {
    if (overlayMode_ != OverlayMode::None) return;
    overlayList_->clear();
    overlayEdit_->clear();
}

void QtPlayerWindow::ClearSourceSelectorWidgets() {
    if (sourceSelection_) return;
    sourceSeason_->clear();
    sourceVoice_->clear();
    sourceEpisode_->clear();
    sourceStream_->clear();
    sourceStatus_->clear();
    sourceOpen_->setEnabled(false);
}

bool QtPlayerWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    if (!backgroundTest_ || eventType != QByteArrayLiteral("windows_generic_MSG"))
        return QWidget::nativeEvent(eventType, message, result);
    const auto* nativeMessage = static_cast<const MSG*>(message);
    if (nativeMessage->message == kBackgroundTestQueryMessage) {
        bool value = false;
        switch (nativeMessage->wParam) {
        case 1:
            value = emptyState_->isVisible() && openFileButton_->isVisible() && openUrlButton_->isVisible();
            break;
        case 2:
            value = overlayMode_ == OverlayMode::Url && overlay_->isVisible() && overlayEdit_->isVisible() &&
                    overlayPrimary_->isVisible() && overlaySecondary_->isVisible();
            break;
        case 3:
            value = sourceSelection_.has_value() && sourcePanel_->isVisible();
            break;
        case 4:
            value = mediaLoaded_;
            break;
        case 5:
            value = playbackStarted_;
            break;
        case 6:
            value = controlsVisible_ && controls_->isVisible();
            break;
        case 7:
            *result = timeline_->value();
            return true;
        case 8:
            *result = static_cast<qintptr>(std::lround(std::max(0.0, engine_.Position()) * 100.0));
            return true;
        case 9: {
            int providers = 0;
            if (sourceSelection_) {
                for (const auto& season : sourceSelection_->entry.seasons) {
                    for (const auto& voice : season.voiceTracks) {
                        for (const auto& episode : voice.episodes) {
                            for (const auto& stream : episode.streams) {
                                const QString identity = (ToQString(stream.quality) + QLatin1Char(' ') +
                                                          ToQString(stream.url)).toLower();
                                if (identity.contains(QStringLiteral("cvh")) ||
                                    identity.contains(QStringLiteral("cdnvideohub"))) providers |= 1;
                                if (identity.contains(QStringLiteral("kodik"))) providers |= 2;
                                if (identity.contains(QStringLiteral("alloha"))) providers |= 4;
                                if (identity.contains(QStringLiteral("aniboom"))) providers |= 8;
                            }
                        }
                    }
                }
            }
            *result = providers;
            return true;
        }
        case 10:
            *result = videoSurface_->HasRenderedFrame() ? 1 : 0;
            return true;
        case 11:
            *result = qRound(controlsOpacity_->opacity() * 1000.0);
            return true;
        case 12:
            *result = qRound(overlayTransition_->effect->opacity() * 1000.0);
            return true;
        case 13:
            *result = qRound(emptyTransition_->effect->opacity() * 1000.0);
            return true;
        case 14:
            *result = qRound(scrimTransition_->effect->opacity() * 1000.0);
            return true;
        case 15:
            *result = overlay_->y() - overlayTransition_->restingGeometry.y();
            return true;
        case 16:
            value = playbackFeedback_->isVisible();
            break;
        case 17:
            *result = qRound(feedbackTransition_->effect->opacity() * 1000.0);
            return true;
        case 18:
            *result = playbackFeedback_->y() - feedbackTransition_->restingGeometry.y();
            return true;
        case 19: {
            int states = 0;
            if (muteButton_->property("active").toBool()) states |= 1;
            if (subtitleButton_->property("active").toBool()) states |= 2;
            if (shaderButton_->property("active").toBool()) states |= 4;
            if (statsButton_->property("active").toBool()) states |= 8;
            if (fullscreenButton_->property("active").toBool()) states |= 16;
            *result = states;
            return true;
        }
        case 20:
            *result = volume_->value();
            return true;
        case 21:
            value = !currentResumeKey_.empty() && playbackState_.Find(currentResumeKey_).has_value();
            break;
        case 22:
            value = overlayMode_ == OverlayMode::Choice && overlayAction_ == OverlayAction::Resume &&
                    pendingResumePosition_.has_value() && overlay_->isVisible();
            break;
        case 23:
            value = overlayMode_ == OverlayMode::Choice && overlayAction_ == OverlayAction::Settings &&
                    overlay_->isVisible();
            break;
        case 24: {
            int active = 0;
            for (int index = 0; index < overlayList_->count(); ++index) {
                if (overlayList_->item(index)->text().startsWith(QStringLiteral("✓"))) ++active;
            }
            *result = (overlayList_->count() << 8) | active;
            return true;
        }
        case 25:
            value = resumeEnabled_;
            break;
        case 26:
            value = config_.GetBool("ui.animations", true);
            break;
        case 27:
            *result = static_cast<qintptr>(shaderPresetIndex_);
            return true;
        case 28:
            *result = recentList_->count();
            return true;
        case 29:
            value = recentList_->currentItem() &&
                    recentList_->currentItem()->data(Qt::UserRole + 1).toBool();
            break;
        default:
            break;
        }
        *result = value ? 1 : 0;
        return true;
    }
    if (nativeMessage->message == kBackgroundTestActionMessage) {
        switch (nativeMessage->wParam) {
        case 1: ShowUrlOverlay(); break;
        case 2: HideOverlay(); break;
        case 3: ShowControls(true, false); break;
        case 4: ShowControls(false, false); break;
        case 5:
            // This command is delivered with SendMessage from the background
            // smoke process. Closing synchronously from inside nativeEvent()
            // lets QApplication tear down the Windows platform plugin before
            // qwindows has returned from its native-message dispatch. Queue
            // the close so the native callback can unwind first.
            QTimer::singleShot(0, this, [this] { close(); });
            break;
        case 6:
            timeline_->setValue(5000);
            SeekFromSlider(true);
            break;
        case 7: {
            if (!sourceSelection_) { *result = 0; return true; }
            std::optional<StreamLocation> cvh;
            for (const auto& location : StreamLocations(*sourceSelection_)) {
                const auto* stream = StreamAt(*sourceSelection_, location);
                if (!stream) continue;
                const QString identity = (ToQString(stream->quality) + QLatin1Char(' ') +
                                          ToQString(stream->url)).toLower();
                if (!stream->protectedStream &&
                    (identity.contains(QStringLiteral("cvh")) ||
                     identity.contains(QStringLiteral("cdnvideohub")))) {
                    cvh = location;
                    break;
                }
            }
            if (!cvh) { *result = 0; return true; }
            sourceSeasonIndex_ = cvh->season;
            sourceVoiceIndex_ = cvh->voice;
            sourceEpisodeIndex_ = cvh->episode;
            sourceStreamIndex_ = cvh->stream;
            OpenSelectedSource();
            break;
        }
        case 8:
            if (!SelectedSource()) { *result = 0; return true; }
            OpenSelectedSource();
            break;
        case 9: {
            ShowUrlOverlay();
            const std::array<UiTransition*, 3> transitions{
                emptyTransition_.get(), scrimTransition_.get(), overlayTransition_.get()};
            for (UiTransition* transition : transitions) {
                if (transition->group->state() == QAbstractAnimation::Running) {
                    transition->group->setCurrentTime(transition->group->duration() / 2);
                    transition->group->pause();
                }
            }
            break;
        }
        case 10:
            emptyTransition_->group->resume();
            scrimTransition_->group->resume();
            overlayTransition_->group->resume();
            break;
        case 11: {
            HideOverlay();
            const std::array<UiTransition*, 3> transitions{
                emptyTransition_.get(), scrimTransition_.get(), overlayTransition_.get()};
            for (UiTransition* transition : transitions) {
                if (transition->group->state() == QAbstractAnimation::Running) {
                    transition->group->setCurrentTime(transition->group->duration() / 2);
                    transition->group->pause();
                }
            }
            break;
        }
        case 12:
            emptyTransition_->group->resume();
            scrimTransition_->group->resume();
            overlayTransition_->group->resume();
            break;
        case 13:
            ShowPlaybackFeedback(QStringLiteral("Paused"));
            feedbackTimer_->stop();
            if (feedbackTransition_->group->state() == QAbstractAnimation::Running) {
                feedbackTransition_->group->setCurrentTime(feedbackTransition_->group->duration() / 2);
                feedbackTransition_->group->pause();
            }
            break;
        case 14:
            feedbackTransition_->group->resume();
            break;
        case 15:
            feedbackVisible_ = false;
            UpdateVisibility();
            if (feedbackTransition_->group->state() == QAbstractAnimation::Running) {
                feedbackTransition_->group->setCurrentTime(feedbackTransition_->group->duration() / 2);
                feedbackTransition_->group->pause();
            }
            break;
        case 16:
            feedbackTransition_->group->resume();
            break;
        case 17:
            ToggleStatistics();
            break;
        case 18:
            ToggleMute();
            break;
        case 19:
            volume_->setValue(37);
            break;
        case 20:
            if (overlayAction_ != OverlayAction::Resume) { *result = 0; return true; }
            overlayList_->setCurrentRow(0);
            ApplyOverlaySelection();
            break;
        case 21:
            if (overlayAction_ != OverlayAction::Resume) { *result = 0; return true; }
            overlayList_->setCurrentRow(1);
            ApplyOverlaySelection();
            break;
        case 22:
            ShowSettingsMenu();
            break;
        case 23:
            ShowSettingsMenu();
            overlayList_->setCurrentRow(8);
            ApplyOverlaySelection();
            break;
        case 24:
            ShowSettingsMenu();
            overlayList_->setCurrentRow(7);
            ApplyOverlaySelection();
            break;
        case 25:
            ShowSettingsMenu();
            overlayList_->setCurrentRow(10);
            ApplyOverlaySelection();
            break;
        case 26:
            ShowSettingsMenu();
            overlayList_->setCurrentRow(9);
            ApplyOverlaySelection();
            break;
        case 27:
            ShowSettingsMenu();
            overlayList_->setCurrentRow(11);
            ApplyOverlaySelection();
            break;
        case 28:
            ApplyShaderPreset(1);
            break;
        case 29:
            if (!recentList_->currentItem() || !recentList_->currentItem()->data(Qt::UserRole + 1).toBool()) {
                *result = 0;
                return true;
            }
            OpenSelectedRecent();
            break;
        case 30:
            if (!recentList_->currentItem()) { *result = 0; return true; }
            RemoveSelectedRecent();
            break;
        case 31:
            if (recentList_->count() == 0) { *result = 0; return true; }
            ClearRecentMedia();
            break;
        default: *result = 0; return true;
        }
        *result = 1;
        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

bool QtPlayerWindow::eventFilter(QObject* watched, QEvent* event) {
    Q_UNUSED(watched);
    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
        RecordInteraction();
        break;
    default:
        break;
    }
    return false;
}

void QtPlayerWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    LayoutOverlays();
    // Defer libmpv initialization until the first shown event-loop turn so
    // QOpenGLWidget has created its GUI-thread-owned context and framebuffer.
    QTimer::singleShot(0, this, [this] {
        if (!closing_.load() && !motionTest_) StartEngineInitialization();
    });
}

void QtPlayerWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    LayoutOverlays();
}

void QtPlayerWindow::closeEvent(QCloseEvent* event) {
    SavePlaybackProgress();
    SaveUserSettings();
    closing_.store(true);
    if (resolverThread_.joinable()) resolverThread_.request_stop();
    // The Render API belongs to QOpenGLWidget's context. Release it while the
    // widget is still shown and the GUI event loop is active; after this there
    // is no foreign child HWND, so teardown cannot re-enter qwindows.
    hideTimer_->stop();
    feedbackTimer_->stop();
    progressSaveTimer_->stop();
    settingsSaveTimer_->stop();
    uiTimer_->stop();
    StopUiAnimations();
    if (engineReady_) {
        videoSurface_->DetachEngine();
        engine_.Shutdown();
        engineReady_ = false;
    }
    QWidget::closeEvent(event);
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void QtPlayerWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void QtPlayerWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QUrl url = urls.front();
    if (url.isLocalFile()) {
        const QFileInfo info(url.toLocalFile());
        const QString extension = info.suffix().toLower();
        if (mediaLoaded_ && (extension == QStringLiteral("srt") || extension == QStringLiteral("ass") ||
                             extension == QStringLiteral("ssa") || extension == QStringLiteral("vtt") ||
                             extension == QStringLiteral("sup"))) {
            engine_.AddSubtitle(ToUtf8(info.absoluteFilePath()));
        } else {
            const auto path = ToUtf8(info.absoluteFilePath());
            OpenLocalFile(path);
        }
    } else {
        ResolveUrl(ToUtf8(url.toString()));
    }
    event->acceptProposedAction();
}

void QtPlayerWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && overlayMode_ == OverlayMode::None && !sourceSelection_) {
        ToggleFullscreen();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void QtPlayerWindow::keyPressEvent(QKeyEvent* event) {
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    if (event->key() == Qt::Key_Escape && overlayMode_ != OverlayMode::None) HideOverlay();
    else if (event->key() == Qt::Key_Escape && sourceSelection_) HideSourceSelector();
    else if (event->key() == Qt::Key_Escape && fullscreen_) ToggleFullscreen();
    else if (control && event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9)
        ApplyShaderHotkey(event->key() - Qt::Key_0);
    else if (control && event->key() == Qt::Key_O) OpenFileDialog();
    else if (control && event->key() == Qt::Key_U) ShowUrlOverlay();
    else if (event->key() == Qt::Key_Space) TogglePlayback();
    else if (event->key() == Qt::Key_Left) SeekRelative(shift ? -30.0 : -5.0);
    else if (event->key() == Qt::Key_Right) SeekRelative(shift ? 30.0 : 5.0);
    else if (event->key() == Qt::Key_Period) {
        if (mediaLoaded_) { engine_.FrameStep(); ShowPlaybackFeedback(QStringLiteral("Next frame")); }
    }
    else if (event->key() == Qt::Key_PageUp) {
        if (mediaLoaded_) { engine_.ChangeChapter(-1); ShowPlaybackFeedback(QStringLiteral("Previous chapter")); }
    }
    else if (event->key() == Qt::Key_PageDown) {
        if (mediaLoaded_) { engine_.ChangeChapter(1); ShowPlaybackFeedback(QStringLiteral("Next chapter")); }
    }
    else if (event->key() == Qt::Key_F) ToggleFullscreen();
    else if (event->key() == Qt::Key_M) ToggleMute();
    else if (event->key() == Qt::Key_S) CycleSubtitleTrack();
    else if (event->key() == Qt::Key_A) CycleAudioTrack();
    else if (event->key() == Qt::Key_I || event->key() == Qt::Key_F10) ToggleStatistics();
    else { QWidget::keyPressEvent(event); return; }
    event->accept();
    if (!(event->key() == Qt::Key_F && fullscreen_)) RecordInteraction();
}

void QtPlayerWindow::StartEngineInitialization() {
    if (engineReady_ || engineInitializationThread_.joinable()) return;
    if (!isVisible()) return;
    LayoutOverlays();
    {
        std::scoped_lock lock(engineInitializationMutex_);
        engineInitializationError_.reset();
    }
    engineInitializationThread_ = std::jthread([this] {
        try {
            engine_.Initialize(0, [this](PlaybackEvent event) {
                if (closing_.load()) return;
                QMetaObject::invokeMethod(this, [this, event = std::move(event)]() mutable {
                    if (!closing_.load()) HandlePlaybackEvent(std::move(event));
                }, Qt::QueuedConnection);
            });
        } catch (const std::exception& error) {
            std::scoped_lock lock(engineInitializationMutex_);
            engineInitializationError_ = error.what();
        } catch (...) {
            std::scoped_lock lock(engineInitializationMutex_);
            engineInitializationError_ = "Unknown libmpv initialization failure";
        }
        if (!closing_.load()) {
            QMetaObject::invokeMethod(this, [this] { HandleEngineInitialized(); }, Qt::QueuedConnection);
        }
    });
}

void QtPlayerWindow::HandleEngineInitialized() {
    if (engineInitializationThread_.joinable()) engineInitializationThread_.join();
    if (closing_.load()) return;
    std::optional<std::string> error;
    {
        std::scoped_lock lock(engineInitializationMutex_);
        error = std::move(engineInitializationError_);
        engineInitializationError_.reset();
    }
    if (error) {
        logger_.Write(LogLevel::Error, "playback", "libmpv initialization failed: " + *error);
        const bool requestedPlayback = pendingMedia_.has_value();
        pendingMedia_.reset();
        mediaOpening_ = false;
        setWindowTitle(QStringLiteral("WannaViewer"));
        UpdateVisibility();
        if (requestedPlayback) ShowError(QStringLiteral("Playback"), *error);
        return;
    }
    engineReady_ = true;
    try {
        if (engine_.UsesOpenGlRenderApi()) videoSurface_->AttachEngine(engine_);
        ApplyConfiguredShader();
    } catch (const std::exception& rendererError) {
        logger_.Write(LogLevel::Error, "playback", std::string("OpenGL renderer initialization failed: ") +
                                                     rendererError.what());
        engineReady_ = false;
        engine_.Shutdown();
        pendingMedia_.reset();
        mediaOpening_ = false;
        setWindowTitle(QStringLiteral("WannaViewer"));
        UpdateVisibility();
        ShowError(QStringLiteral("Playback"), rendererError.what());
        return;
    }
    if (!pendingMedia_) return;
    auto media = std::move(*pendingMedia_);
    pendingMedia_.reset();
    try {
        playbackLoadStarted_ = std::chrono::steady_clock::now();
        engine_.Open(media.value, media.headers, media.externalAudioUrl);
    } catch (const std::exception& openError) {
        mediaOpening_ = false;
        setWindowTitle(QStringLiteral("WannaViewer"));
        UpdateVisibility();
        ShowError(QStringLiteral("Playback"), openError.what());
    }
}

void QtPlayerWindow::OpenInitial(std::string value) {
    QTimer::singleShot(0, this, [this, value = std::move(value)]() mutable { ResolveUrl(std::move(value)); });
}

void QtPlayerWindow::EnableBenchmark(std::string value, std::string mode) {
    benchmarkMode_ = true;
    benchmarkInput_ = value;
    benchmarkProfile_ = std::move(mode);
    setWindowTitle(QStringLiteral("WannaViewer — benchmark"));
    OpenInitial(std::move(value));
}

void QtPlayerWindow::OpenMedia(std::string value,
                               std::vector<std::pair<std::string, std::string>> headers,
                               std::string externalAudioUrl,
                               std::string resumeIdentity) {
    SavePlaybackProgress();
    resumeSaveSuspended_ = true;
    pendingResumeKey_ = playbackStateAvailable_ && resumeEnabled_ && !benchmarkMode_
        ? PlaybackStateKey(resumeIdentity.empty() ? PlaybackIdentity(value) : resumeIdentity)
        : std::string{};
    if (sourceSelection_) HideSourceSelector();
    if (overlayMode_ != OverlayMode::None) HideOverlay();
    mediaOpening_ = true;
    playbackClockAdvanced_ = false;
    playbackStarted_ = false;
    startupTimeoutReported_ = false;
    playbackLoadStarted_ = std::chrono::steady_clock::now();
    const auto parsedMedia = Url::Parse(value);
    playbackStartupTimeout_ = parsedMedia && (parsedMedia->IsHttp() || parsedMedia->IsHttps())
        ? std::chrono::seconds(60) : std::chrono::seconds(30);
    videoSurface_->ResetPresentedFrame();
    openingStatus_->setText(engineReady_ ? QStringLiteral("Loading and decoding the first frame…")
                                         : QStringLiteral("Starting the video engine…"));
    setWindowTitle(engineReady_ ? QStringLiteral("WannaViewer — opening…")
                                : QStringLiteral("WannaViewer — preparing player…"));
    UpdateVisibility();
    if (!uiTimer_->isActive()) uiTimer_->start();
    if (!engineReady_) {
        pendingMedia_ = PendingMedia{std::move(value), std::move(headers), std::move(externalAudioUrl)};
        StartEngineInitialization();
        return;
    }
    try {
        engine_.Open(value, headers, externalAudioUrl);
    } catch (...) {
        mediaOpening_ = false;
        setWindowTitle(QStringLiteral("WannaViewer"));
        UpdateVisibility();
        throw;
    }
}

void QtPlayerWindow::OpenLocalFile(std::string path) {
    const QFileInfo info(ToQString(path));
    if (!info.exists() || !info.isFile()) {
        ShowError(QStringLiteral("Open"), "The selected file no longer exists");
        return;
    }
    const std::string absolutePath = ToUtf8(info.absoluteFilePath());
    const std::string resumeIdentity = PlaybackIdentity(absolutePath);
    pendingRecentOpenValue_ = absolutePath;
    recentReplayEntry_.reset();
    pendingRecentEntry_ = RecentMediaEntry{
        PlaybackStateKey("recent\nfile\n" + ToUtf8(info.absoluteFilePath().toCaseFolded())),
        RecentMediaType::LocalFile, absolutePath, ToUtf8(info.fileName()), ToUtf8(info.absolutePath()),
        PlaybackStateKey(resumeIdentity), std::nullopt, 0};
    OpenMedia(absolutePath, {}, {}, resumeIdentity);
}

void QtPlayerWindow::OpenFileDialog() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open media"), QString(),
        QStringLiteral("Media files (*.mkv *.mp4 *.m4v *.mov *.webm *.avi *.ts *.m2ts *.mp3 *.flac *.opus *.m4a);;All files (*.*)"));
    if (!path.isEmpty()) {
        const auto absolutePath = ToUtf8(QFileInfo(path).absoluteFilePath());
        OpenLocalFile(absolutePath);
    }
}

void QtPlayerWindow::ShowUrlOverlay() {
    if (sourceSelection_) HideSourceSelector();
    overlayMode_ = OverlayMode::Url;
    overlayAction_ = OverlayAction::None;
    overlayChoices_.clear();
    overlayTitle_->setText(QStringLiteral("Open a link"));
    overlayBody_->setText(QStringLiteral("Paste a direct media URL or a supported page address"));
    overlayPrimary_->setText(QStringLiteral("Open"));
    overlaySecondary_->setText(QStringLiteral("Cancel"));
    overlayEdit_->setText(QStringLiteral("https://"));
    overlayEdit_->show();
    overlayList_->hide();
    overlaySecondary_->show();
    UpdateVisibility();
    overlayEdit_->setFocus();
    overlayEdit_->selectAll();
}

void QtPlayerWindow::ResolveUrl(std::string value, HeaderMap inheritedHeaders, bool preserveResumeIdentity) {
    if (!preserveResumeIdentity) {
        pendingResumeIdentity_ = PlaybackIdentity(value);
        pendingRecentOpenValue_ = value;
        pendingRecentEntry_.reset();
        recentReplayEntry_.reset();
        recentReplaySourceMatched_ = false;
    }
    const auto parsed = Url::Parse(value);
    if (!parsed) {
        if (QFileInfo(ToQString(value)).isFile()) {
            OpenLocalFile(value);
        }
        else ShowError(QStringLiteral("Open"), "The path does not exist or the URL scheme is not allowed");
        return;
    }
    if (!preserveResumeIdentity) pendingRecentOpenValue_ = parsed->Value();
    if (sourceSelection_) HideSourceSelector();
    if (overlayMode_ != OverlayMode::None) HideOverlay();
    if (inheritedHeaders.empty() && !preserveResumeIdentity) {
        browserRetryUrl_.clear();
        browserRetryHeaders_.clear();
        browserRetriesRemaining_ = 0;
    }
    if (parsed->IsDirectMedia()) {
        if (!pendingRecentEntry_) {
            const QString pathName = QFileInfo(ToQString(parsed->Path())).fileName();
            pendingRecentEntry_ = RecentMediaEntry{
                PlaybackStateKey("recent\nurl\n" + parsed->Value()), RecentMediaType::DirectUrl,
                parsed->Value(), ToUtf8(pathName.isEmpty() ? ToQString(parsed->Host()) : pathName),
                parsed->Host(), PlaybackStateKey(pendingResumeIdentity_), std::nullopt, 0};
        }
        std::vector<std::pair<std::string, std::string>> headers(inheritedHeaders.begin(), inheritedHeaders.end());
        OpenMedia(parsed->Value(), std::move(headers), {}, pendingResumeIdentity_);
        return;
    }
    bool expected = false;
    if (!resolving_.compare_exchange_strong(expected, true)) {
        ShowError(QStringLiteral("URL resolver"), "Another URL is already being resolved");
        return;
    }
    setWindowTitle(QStringLiteral("WannaViewer — resolving URL…"));
    if (resolverThread_.joinable()) resolverThread_.join();
    resolverThread_ = std::jthread([this, url = *parsed, headers = std::move(inheritedHeaders)](std::stop_token token) {
        ResolveContext context{http_, logger_, token, headers};
        auto result = resolvers_.Resolve(url, context);
        resolving_.store(false);
        if (closing_.load()) return;
        QMetaObject::invokeMethod(this, [this, result = std::move(result)]() mutable {
            if (!closing_.load()) HandleResolveResult(std::move(result));
        }, Qt::QueuedConnection);
    });
}

void QtPlayerWindow::HandleResolveResult(ResolveResult result) {
    setWindowTitle(QStringLiteral("WannaViewer"));
    if (result.status != ResolveStatus::Resolved) {
        recentReplayEntry_.reset();
        recentReplaySourceMatched_ = false;
        pendingRecentEntry_.reset();
        ShowError(QStringLiteral("URL resolver"), result.message);
        return;
    }
    if (recentReplayEntry_ && recentReplayEntry_->selection) {
        const auto location = recentReplaySourceMatched_
            ? FindPreferredStream(result, *recentReplayEntry_->selection)
            : FindRecentLocation(result, *recentReplayEntry_->selection);
        if (location) {
            const auto* stream = StreamAt(result, *location);
            if (stream) {
                const StreamVariant selected = *stream;
                const std::string existingId = recentReplayEntry_->id;
                if (!pendingRecentEntry_) {
                    PrepareSelectedRecent(result, location->season, location->voice,
                                          location->episode, location->stream);
                }
                if (pendingRecentEntry_) pendingRecentEntry_->id = existingId;
                recentReplaySourceMatched_ = true;
                if (selected.protocol != "embed") {
                    recentReplayEntry_.reset();
                    recentReplaySourceMatched_ = false;
                }
                OpenVariant(selected);
                return;
            }
        }
        logger_.Write(LogLevel::Info, "playback",
                      "saved recent source is unavailable; showing the current source selector");
        recentReplayEntry_.reset();
        recentReplaySourceMatched_ = false;
    }
    const auto choices = StreamLocations(result);
    if (choices.empty()) {
        ShowError(QStringLiteral("URL resolver"), "Metadata was found, but no public playable stream is available");
        return;
    }
    if (choices.size() == 1) {
        if (const auto* stream = StreamAt(result, choices.front()); stream && !stream->protectedStream) {
            const StreamVariant selected = *stream;
            if (!pendingRecentEntry_) {
                PrepareSelectedRecent(result, choices.front().season, choices.front().voice,
                                      choices.front().episode, choices.front().stream);
            }
            OpenVariant(selected);
            return;
        }
    }
    ShowSourceSelector(std::move(result));
}

void QtPlayerWindow::ShowSourceSelector(ResolveResult result) {
    if (overlayMode_ != OverlayMode::None) HideOverlay();
    sourceSelection_ = std::move(result);
    sourceSeasonIndex_ = sourceVoiceIndex_ = sourceEpisodeIndex_ = sourceStreamIndex_ = -1;
    bool foundPlayable = false;
    for (std::size_t seasonIndex = 0; seasonIndex < sourceSelection_->entry.seasons.size() && !foundPlayable; ++seasonIndex) {
        const auto& season = sourceSelection_->entry.seasons[seasonIndex];
        for (std::size_t voiceIndex = 0; voiceIndex < season.voiceTracks.size() && !foundPlayable; ++voiceIndex) {
            const auto& voice = season.voiceTracks[voiceIndex];
            for (std::size_t episodeIndex = 0; episodeIndex < voice.episodes.size() && !foundPlayable; ++episodeIndex) {
                const auto& episode = voice.episodes[episodeIndex];
                for (std::size_t streamIndex = 0; streamIndex < episode.streams.size(); ++streamIndex) {
                    if (episode.streams[streamIndex].protectedStream) continue;
                    sourceSeasonIndex_ = static_cast<int>(seasonIndex);
                    sourceVoiceIndex_ = static_cast<int>(voiceIndex);
                    sourceEpisodeIndex_ = static_cast<int>(episodeIndex);
                    sourceStreamIndex_ = static_cast<int>(streamIndex);
                    foundPlayable = true;
                    break;
                }
            }
        }
    }
    if (!foundPlayable) {
        sourceSeasonIndex_ = sourceSelection_->entry.seasons.empty() ? -1 : 0;
        sourceVoiceIndex_ = sourceEpisodeIndex_ = sourceStreamIndex_ = 0;
    }
    sourceTitle_->setText(sourceSelection_->entry.title.empty()
                              ? QStringLiteral("Choose an episode")
                              : ToQString(sourceSelection_->entry.title));
    PopulateSourceSeasons();
    PopulateSourceVoices();
    PopulateSourceEpisodes();
    PopulateSourceStreams();
    ShowControls(true, false);
    UpdateVisibility();
    sourceSeason_->setFocus();
}

void QtPlayerWindow::HideSourceSelector() {
    sourceSelection_.reset();
    sourceSeasonIndex_ = sourceVoiceIndex_ = sourceEpisodeIndex_ = sourceStreamIndex_ = -1;
    UpdateVisibility();
    setFocus();
}

void QtPlayerWindow::PopulateSourceSeasons() {
    const QSignalBlocker blocker(sourceSeason_);
    sourceSeason_->clear();
    if (!sourceSelection_) return;
    const auto& seasons = sourceSelection_->entry.seasons;
    for (const auto& season : seasons) sourceSeason_->addItem(DisplayLabel(season.title, "Default"));
    if (sourceSeasonIndex_ < 0 || static_cast<std::size_t>(sourceSeasonIndex_) >= seasons.size())
        sourceSeasonIndex_ = seasons.empty() ? -1 : 0;
    sourceSeason_->setCurrentIndex(sourceSeasonIndex_);
}

void QtPlayerWindow::PopulateSourceVoices() {
    const QSignalBlocker blocker(sourceVoice_);
    sourceVoice_->clear();
    if (!sourceSelection_ || sourceSeasonIndex_ < 0 ||
        static_cast<std::size_t>(sourceSeasonIndex_) >= sourceSelection_->entry.seasons.size()) {
        sourceVoiceIndex_ = -1;
        return;
    }
    const auto& voices = sourceSelection_->entry.seasons[static_cast<std::size_t>(sourceSeasonIndex_)].voiceTracks;
    for (const auto& voice : voices) sourceVoice_->addItem(DisplayLabel(voice.title, "Default"));
    if (sourceVoiceIndex_ < 0 || static_cast<std::size_t>(sourceVoiceIndex_) >= voices.size()) {
        sourceVoiceIndex_ = voices.empty() ? -1 : 0;
        for (std::size_t index = 0; index < voices.size(); ++index) {
            if (std::ranges::any_of(voices[index].episodes, [](const Episode& episode) { return !episode.streams.empty(); })) {
                sourceVoiceIndex_ = static_cast<int>(index);
                break;
            }
        }
    }
    sourceVoice_->setCurrentIndex(sourceVoiceIndex_);
}

void QtPlayerWindow::PopulateSourceEpisodes() {
    const QSignalBlocker blocker(sourceEpisode_);
    sourceEpisode_->clear();
    if (!sourceSelection_ || sourceSeasonIndex_ < 0 || sourceVoiceIndex_ < 0) {
        sourceEpisodeIndex_ = -1;
        return;
    }
    const auto& season = sourceSelection_->entry.seasons[static_cast<std::size_t>(sourceSeasonIndex_)];
    if (static_cast<std::size_t>(sourceVoiceIndex_) >= season.voiceTracks.size()) {
        sourceEpisodeIndex_ = -1;
        return;
    }
    const auto& episodes = season.voiceTracks[static_cast<std::size_t>(sourceVoiceIndex_)].episodes;
    for (const auto& episode : episodes) sourceEpisode_->addItem(DisplayLabel(episode.title, "Episode"));
    if (sourceEpisodeIndex_ < 0 || static_cast<std::size_t>(sourceEpisodeIndex_) >= episodes.size()) {
        sourceEpisodeIndex_ = episodes.empty() ? -1 : 0;
        for (std::size_t index = 0; index < episodes.size(); ++index) {
            if (!episodes[index].streams.empty()) { sourceEpisodeIndex_ = static_cast<int>(index); break; }
        }
    }
    sourceEpisode_->setCurrentIndex(sourceEpisodeIndex_);
}

void QtPlayerWindow::PopulateSourceStreams() {
    const QSignalBlocker blocker(sourceStream_);
    sourceStream_->clear();
    if (!sourceSelection_ || sourceSeasonIndex_ < 0 || sourceVoiceIndex_ < 0 || sourceEpisodeIndex_ < 0) {
        sourceStreamIndex_ = -1;
    } else {
        const auto& seasons = sourceSelection_->entry.seasons;
        if (static_cast<std::size_t>(sourceSeasonIndex_) >= seasons.size()) sourceStreamIndex_ = -1;
        else {
            const auto& voices = seasons[static_cast<std::size_t>(sourceSeasonIndex_)].voiceTracks;
            if (static_cast<std::size_t>(sourceVoiceIndex_) >= voices.size()) sourceStreamIndex_ = -1;
            else {
                const auto& episodes = voices[static_cast<std::size_t>(sourceVoiceIndex_)].episodes;
                if (static_cast<std::size_t>(sourceEpisodeIndex_) >= episodes.size()) sourceStreamIndex_ = -1;
                else {
                    const auto& streams = episodes[static_cast<std::size_t>(sourceEpisodeIndex_)].streams;
                    for (const auto& stream : streams) {
                        QString label = DisplayLabel(stream.quality, "Auto");
                        if (!stream.codec.empty()) label += QStringLiteral("  •  ") + ToQString(stream.codec);
                        if (!stream.protocol.empty()) label += QStringLiteral("  •  ") + ToQString(stream.protocol);
                        if (stream.protectedStream) label += QStringLiteral("  •  Protected");
                        sourceStream_->addItem(label);
                    }
                    if (sourceStreamIndex_ < 0 || static_cast<std::size_t>(sourceStreamIndex_) >= streams.size()) {
                        sourceStreamIndex_ = streams.empty() ? -1 : 0;
                        for (std::size_t index = 0; index < streams.size(); ++index) {
                            if (!streams[index].protectedStream) { sourceStreamIndex_ = static_cast<int>(index); break; }
                        }
                    }
                }
            }
        }
    }
    sourceStream_->setCurrentIndex(sourceStreamIndex_);
    UpdateSourceSelectionUi();
}

void QtPlayerWindow::UpdateSourceSelectionUi() {
    const auto* selected = SelectedSource();
    sourceOpen_->setEnabled(selected && !selected->protectedStream);
    sourceStatus_->setText(!selected ? QStringLiteral("No source is available for this selection")
                           : selected->protectedStream ? QStringLiteral("This source is protected and cannot be opened")
                                                       : QStringLiteral("Choose a source and press Open"));
}

const StreamVariant* QtPlayerWindow::SelectedSource() const {
    if (!sourceSelection_ || sourceSeasonIndex_ < 0 || sourceVoiceIndex_ < 0 ||
        sourceEpisodeIndex_ < 0 || sourceStreamIndex_ < 0) return nullptr;
    const auto& seasons = sourceSelection_->entry.seasons;
    if (static_cast<std::size_t>(sourceSeasonIndex_) >= seasons.size()) return nullptr;
    const auto& voices = seasons[static_cast<std::size_t>(sourceSeasonIndex_)].voiceTracks;
    if (static_cast<std::size_t>(sourceVoiceIndex_) >= voices.size()) return nullptr;
    const auto& episodes = voices[static_cast<std::size_t>(sourceVoiceIndex_)].episodes;
    if (static_cast<std::size_t>(sourceEpisodeIndex_) >= episodes.size()) return nullptr;
    const auto& streams = episodes[static_cast<std::size_t>(sourceEpisodeIndex_)].streams;
    if (static_cast<std::size_t>(sourceStreamIndex_) >= streams.size()) return nullptr;
    return &streams[static_cast<std::size_t>(sourceStreamIndex_)];
}

void QtPlayerWindow::PrepareSelectedRecent(const ResolveResult& result, int seasonIndex, int voiceIndex,
                                           int episodeIndex, int streamIndex) {
    const StreamLocation location{seasonIndex, voiceIndex, episodeIndex, streamIndex};
    const auto* stream = StreamAt(result, location);
    if (!stream || pendingRecentOpenValue_.empty()) return;
    const auto& season = result.entry.seasons[static_cast<std::size_t>(seasonIndex)];
    const auto& voice = season.voiceTracks[static_cast<std::size_t>(voiceIndex)];
    const auto& episode = voice.episodes[static_cast<std::size_t>(episodeIndex)];
    const std::string baseIdentity = pendingResumeIdentity_;
    pendingResumeIdentity_ = std::format("{}\nseason={}\nvoice={}\nepisode={}", baseIdentity,
        ComponentIdentity(season.id, season.title), ComponentIdentity(voice.id, voice.title),
        ComponentIdentity(episode.id, episode.title));

    std::string detail;
    const auto appendDetail = [&detail](const std::string& value) {
        if (value.empty()) return;
        if (!detail.empty()) detail += " · ";
        detail += value;
    };
    appendDetail(season.title);
    appendDetail(voice.title);
    appendDetail(episode.title);
    const std::string stableIdentity = std::format("recent\npage\n{}\nseason={}\nvoice={}\nepisode={}",
        pendingRecentOpenValue_, ComponentIdentity(season.id, season.title),
        ComponentIdentity(voice.id, voice.title), ComponentIdentity(episode.id, episode.title));
    const auto parsed = Url::Parse(pendingRecentOpenValue_);
    const std::string fallbackTitle = parsed ? parsed->Host() : pendingRecentOpenValue_;
    pendingRecentEntry_ = RecentMediaEntry{
        PlaybackStateKey(stableIdentity), RecentMediaType::WebPage, pendingRecentOpenValue_,
        result.entry.title.empty() ? fallbackTitle : result.entry.title, std::move(detail),
        PlaybackStateKey(pendingResumeIdentity_),
        RecentMediaSelection{season.id, season.title, voice.id, voice.title, episode.id, episode.title,
                             stream->quality, stream->protocol}, 0};
}

void QtPlayerWindow::OpenSelectedSource() {
    const auto* stream = SelectedSource();
    if (!stream) return;
    if (stream->protectedStream) {
        sourceStatus_->setText(QStringLiteral("This source is protected and cannot be opened"));
        return;
    }
    const StreamVariant selected = *stream;
    if (!pendingRecentEntry_) {
        PrepareSelectedRecent(*sourceSelection_, sourceSeasonIndex_, sourceVoiceIndex_,
                              sourceEpisodeIndex_, sourceStreamIndex_);
    } else if (pendingRecentEntry_->selection) {
        pendingRecentEntry_->selection->quality = selected.quality;
        pendingRecentEntry_->selection->protocol = selected.protocol;
    }
    HideSourceSelector();
    OpenVariant(selected);
}

void QtPlayerWindow::OpenVariant(const StreamVariant& stream) {
    if (stream.protectedStream) {
        ShowError(QStringLiteral("Provider"), "Provider unsupported: protected/DRM stream");
        return;
    }
    if (stream.protocol == "embed") {
        const auto provider = Url::Parse(stream.url);
        if (provider && (provider->HostIs("kodikplayer.com") || provider->HostIs("alloha.yani.tv"))) {
            browserRetryUrl_ = stream.url;
            browserRetryHeaders_ = stream.headers;
            browserRetriesRemaining_ = 1;
        } else {
            browserRetryUrl_.clear(); browserRetryHeaders_.clear(); browserRetriesRemaining_ = 0;
        }
        ResolveUrl(stream.url, stream.headers, true);
        return;
    }
    std::vector<std::pair<std::string, std::string>> headers(stream.headers.begin(), stream.headers.end());
    OpenMedia(stream.url, std::move(headers), stream.audioUrl, pendingResumeIdentity_);
}

bool QtPlayerWindow::RetryBrowserProvider() {
    if (browserRetriesRemaining_ == 0 || browserRetryUrl_.empty() || resolving_.load()) return false;
    --browserRetriesRemaining_;
    engine_.Stop();
    SetMediaLoaded(false);
    playbackStarted_ = false;
    ResolveUrl(browserRetryUrl_, browserRetryHeaders_, true);
    return true;
}

void QtPlayerWindow::HandlePlaybackEvent(PlaybackEvent event) {
    switch (event.type) {
    case PlaybackEventType::StartFile:
        logger_.Write(LogLevel::Info, "playback", "mpv started opening media");
        playbackClockAdvanced_ = false;
        playbackStarted_ = false;
        videoSurface_->ResetPresentedFrame();
        startupTimeoutReported_ = false;
        playbackLoadStarted_ = std::chrono::steady_clock::now();
        setWindowTitle(QStringLiteral("WannaViewer — opening…"));
        if (!uiTimer_->isActive()) uiTimer_->start();
        break;
    case PlaybackEventType::TracksChanged:
        UpdateTracks();
        break;
    case PlaybackEventType::FileLoaded:
        logger_.Write(LogLevel::Info, "playback", "media metadata and tracks loaded");
        currentResumeKey_ = std::move(pendingResumeKey_);
        CommitPendingRecent();
        resumeSaveSuspended_ = false;
        engine_.SetVolume(static_cast<double>(volume_->value()));
        SetMediaLoaded(true);
        RestorePlaybackProgress();
        setWindowTitle(benchmarkMode_ ? QStringLiteral("WannaViewer — benchmark buffering")
                                      : QStringLiteral("WannaViewer — buffering…"));
        if (benchmarkMode_ && benchmarkProfile_ == "hardware-shader") ApplyShaderHotkey(2, false);
        break;
    case PlaybackEventType::PlaybackStarted:
        logger_.Write(LogLevel::Info, "playback", "playback clock advanced");
        playbackClockAdvanced_ = true;
        UpdatePlaybackStartedState();
        break;
    case PlaybackEventType::EndFile:
        progressSaveTimer_->stop();
        if (event.name == "eof") SavePlaybackProgress(true);
        if (benchmarkRunning_) FinishBenchmark();
        break;
    case PlaybackEventType::Error:
        if (!playbackStarted_ && RetryBrowserProvider()) return;
        pendingRecentEntry_.reset();
        recentReplayEntry_.reset();
        recentReplaySourceMatched_ = false;
        if (!mediaLoaded_) {
            mediaOpening_ = false;
            UpdateVisibility();
        }
        ShowError(ToQString(event.name), event.value);
        break;
    case PlaybackEventType::VideoReconfigured:
    case PlaybackEventType::PropertyChanged:
        break;
    }
}

void QtPlayerWindow::UpdatePlaybackStartedState() {
    if (playbackStarted_ || !playbackClockAdvanced_) return;
    if (engine_.UsesOpenGlRenderApi() && !videoTrackIds_.empty() && !videoSurface_->HasPresentedFrame()) return;
    playbackStarted_ = true;
    logger_.Write(LogLevel::Info, "playback", engine_.UsesOpenGlRenderApi() && !videoTrackIds_.empty()
        ? "first video frame reached the Qt framebuffer" : "playback started without a video track");
    browserRetryUrl_.clear(); browserRetryHeaders_.clear(); browserRetriesRemaining_ = 0;
    setWindowTitle(benchmarkMode_ ? QStringLiteral("WannaViewer — benchmark running")
                                  : QStringLiteral("WannaViewer — playing"));
    if (benchmarkMode_ && !benchmarkRunning_) {
        benchmarkSamples_.clear();
        benchmarkDroppedBaseline_ = benchmarkDelayedBaseline_ = -1;
        benchmarkStart_ = std::chrono::steady_clock::now();
        lastBenchmarkSample_ = benchmarkStart_;
        benchmarkRunning_ = true;
    }
    RecordInteraction();
}

void QtPlayerWindow::SetMediaLoaded(bool loaded) {
    const bool wasLoaded = mediaLoaded_;
    mediaLoaded_ = loaded;
    mediaOpening_ = false;
    timelineDragging_ = false;
    pendingTimelineValue_.reset();
    displayedPlaying_.reset();
    displayedMuted_.reset();
    if (!loaded) {
        feedbackVisible_ = false;
        feedbackTimer_->stop();
    }
    const std::array<QWidget*, 11> playbackControls{
        playButton_, rewindButton_, forwardButton_, muteButton_, timeline_, volume_, subtitleButton_,
        shaderButton_, statsButton_, settingsButton_, fullscreenButton_};
    for (QWidget* widget : playbackControls)
        widget->setEnabled(loaded);
    audioButton_->setEnabled(loaded && !audioTrackIds_.empty());
    videoButton_->setEnabled(loaded && !videoTrackIds_.empty());
    if (!loaded) {
        timeline_->setValue(0);
        timeLabel_->setText(QStringLiteral("00:00  /  00:00"));
        statisticsVisible_ = false;
        controlsVisible_ = true;
        controlsOpacity_->setOpacity(1.0);
    } else if (!wasLoaded) {
        controlsVisible_ = false;
        controlsOpacity_->setOpacity(0.0);
    }
    UpdateVisibility();
    UpdateControlStates();
    if (loaded) {
        uiTimer_->start();
        if (!benchmarkMode_) progressSaveTimer_->start();
        RecordInteraction();
    } else {
        progressSaveTimer_->stop();
        if (!benchmarkMode_) uiTimer_->stop();
    }
}

void QtPlayerWindow::UpdateUi() {
    if (!engineReady_) return;
    // Audio time can advance before Qt receives a renderable video frame.
    UpdatePlaybackStartedState();
    const auto steadyNow = std::chrono::steady_clock::now();
    if ((mediaOpening_ || mediaLoaded_) && !playbackStarted_ && !startupTimeoutReported_ &&
        steadyNow - playbackLoadStarted_ > playbackStartupTimeout_) {
        const bool sourceOpened = mediaLoaded_;
        startupTimeoutReported_ = true;
        engine_.Stop();
        SetMediaLoaded(false);
        if (RetryBrowserProvider()) return;
        setWindowTitle(QStringLiteral("WannaViewer"));
        const auto timeoutSeconds = playbackStartupTimeout_.count();
        ShowError(QStringLiteral("Playback"), sourceOpened
            ? std::format("The media opened, but no video frame reached the renderer within {} seconds. "
                          "The player was reset; details are in logs/wannaviewer.log.", timeoutSeconds)
            : std::format("The media source did not finish opening within {} seconds. It may be unavailable, "
                          "expired, or rejecting the request; details are in logs/wannaviewer.log.", timeoutSeconds));
        return;
    }
    const double duration = engine_.Duration();
    const double position = engine_.Position();
    if (mediaLoaded_ && controlsVisible_ && !timelineDragging_) {
        int value = duration > 0.0
            ? static_cast<int>(std::lround(std::clamp(position / duration, 0.0, 1.0) * 10000.0)) : 0;
        double displayedPosition = position;
        if (pendingTimelineValue_ && duration > 0.0) {
            const double pendingPosition = duration * static_cast<double>(*pendingTimelineValue_) / 10000.0;
            const auto pendingAge = steadyNow - pendingTimelineStarted_;
            const bool minimumHoldActive = pendingAge < std::chrono::milliseconds(250);
            const bool waitingForMpv = std::abs(position - pendingPosition) > 0.75 &&
                                       pendingAge < std::chrono::milliseconds(1500);
            if (minimumHoldActive || waitingForMpv) {
                value = *pendingTimelineValue_;
                displayedPosition = pendingPosition;
            } else {
                pendingTimelineValue_.reset();
            }
        }
        const QSignalBlocker blocker(timeline_);
        timeline_->setValue(value);
        timeLabel_->setText(TimeText(displayedPosition) + QStringLiteral("  /  ") + TimeText(duration));
        const bool playing = !engine_.IsPaused();
        if (!displayedPlaying_ || *displayedPlaying_ != playing) {
            displayedPlaying_ = playing;
            playButton_->setIcon(MakeIcon(playing ? Glyph::Pause : Glyph::Play));
        }
    }
    if (mediaLoaded_) {
        const bool muted = engine_.IsMuted();
        if (!displayedMuted_ || *displayedMuted_ != muted) {
            displayedMuted_ = muted;
            muteButton_->setIcon(MakeIcon(muted ? Glyph::Muted : Glyph::Volume));
            muteButton_->setToolTip(muted ? QStringLiteral("Unmute") : QStringLiteral("Mute"));
            UpdateControlStates();
        }
    }
    if (statisticsVisible_ && (lastStatisticsUpdate_ == std::chrono::steady_clock::time_point{} ||
        steadyNow - lastStatisticsUpdate_ >= std::chrono::milliseconds(500))) {
        lastStatisticsUpdate_ = steadyNow;
        statistics_->setText(ToQString(engine_.Statistics().ToDisplayText()));
    }
    if (benchmarkRunning_ && steadyNow - lastBenchmarkSample_ >= std::chrono::seconds(1)) {
        lastBenchmarkSample_ = steadyNow;
        auto sample = engine_.Statistics();
        if (benchmarkDroppedBaseline_ < 0) {
            benchmarkDroppedBaseline_ = sample.droppedFrames;
            benchmarkDelayedBaseline_ = sample.delayedFrames;
        }
        benchmarkSamples_.push_back(std::move(sample));
    }
    if (benchmarkRunning_ && duration > 0.0 && position >= duration - 0.05) FinishBenchmark();
    if (!controlsVisible_ && !statisticsVisible_ && !benchmarkMode_ && playbackStarted_) uiTimer_->stop();
}

void QtPlayerWindow::SeekFromSlider(bool commit) {
    if (!mediaLoaded_) return;
    timelineDragging_ = !commit;
    const double duration = engine_.Duration();
    if (duration <= 0.0) return;
    const double seconds = duration * static_cast<double>(timeline_->value()) / 10000.0;
    timeLabel_->setText(TimeText(seconds) + QStringLiteral("  /  ") + TimeText(duration));
    if (commit) {
        pendingTimelineValue_ = timeline_->value();
        pendingTimelineStarted_ = std::chrono::steady_clock::now();
        engine_.SeekAbsolute(seconds);
        ShowPlaybackFeedback(QStringLiteral("Seek  ·  %1").arg(TimeText(seconds)));
    }
    RecordInteraction();
}

void QtPlayerWindow::TogglePlayback() {
    if (!mediaLoaded_) return;
    const bool willPause = !engine_.IsPaused();
    engine_.TogglePause();
    ShowPlaybackFeedback(willPause ? QStringLiteral("Paused") : QStringLiteral("Playing"));
    RecordInteraction();
}

void QtPlayerWindow::ToggleMute() {
    if (!mediaLoaded_) return;
    const bool willMute = !engine_.IsMuted();
    engine_.ToggleMute();
    ShowPlaybackFeedback(willMute ? QStringLiteral("Muted") : QStringLiteral("Sound on"));
    RecordInteraction();
}

void QtPlayerWindow::SeekRelative(double seconds) {
    if (!mediaLoaded_) return;
    engine_.SeekRelative(seconds);
    ShowPlaybackFeedback(seconds < 0.0
        ? QStringLiteral("Back  ·  %1 seconds").arg(qRound(std::abs(seconds)))
        : QStringLiteral("Forward  ·  %1 seconds").arg(qRound(seconds)));
    RecordInteraction();
}

void QtPlayerWindow::CycleAudioTrack() {
    if (!mediaLoaded_ || audioTrackIds_.empty()) return;
    const int next = (audioSelection_ + 1) % static_cast<int>(audioTrackIds_.size());
    audioSelection_ = next;
    engine_.SetAudioTrack(audioTrackIds_[static_cast<std::size_t>(next)]);
    ShowPlaybackFeedback(QStringLiteral("Audio  ·  %1").arg(audioTrackLabels_[static_cast<std::size_t>(next)]));
    RecordInteraction();
}

void QtPlayerWindow::CycleSubtitleTrack() {
    if (!mediaLoaded_ || subtitleTrackIds_.empty()) return;
    const int next = (subtitleSelection_ + 1) % static_cast<int>(subtitleTrackIds_.size());
    subtitleSelection_ = next;
    engine_.SetSubtitleTrack(subtitleTrackIds_[static_cast<std::size_t>(next)]);
    UpdateControlStates();
    ShowPlaybackFeedback(QStringLiteral("Subtitles  ·  %1").arg(subtitleTrackLabels_[static_cast<std::size_t>(next)]));
    RecordInteraction();
}

void QtPlayerWindow::ShowPlaybackFeedback(QString text) {
    if ((!mediaLoaded_ && !backgroundTest_) || overlayMode_ != OverlayMode::None || sourceSelection_) return;
    playbackFeedback_->setText(std::move(text));
    feedbackVisible_ = true;
    UpdateVisibility();
    feedbackTimer_->start();
}

void QtPlayerWindow::HidePlaybackFeedback() {
    feedbackTimer_->stop();
    if (!feedbackVisible_) return;
    feedbackVisible_ = false;
    UpdateVisibility();
}

void QtPlayerWindow::UpdateControlStates() {
    SetButtonActive(muteButton_, mediaLoaded_ && displayedMuted_.value_or(false));
    SetButtonActive(subtitleButton_, mediaLoaded_ && subtitleSelection_ > 0);
    SetButtonActive(shaderButton_, mediaLoaded_ && shaderPresetIndex_ != 0);
    SetButtonActive(statsButton_, mediaLoaded_ && statisticsVisible_);
    SetButtonActive(fullscreenButton_, mediaLoaded_ && fullscreen_);
}

void QtPlayerWindow::RestorePlaybackProgress() {
    if (!playbackStateAvailable_ || !resumeEnabled_ || benchmarkMode_ ||
        currentResumeKey_.empty() || !mediaLoaded_) return;
    const double duration = engine_.Duration();
    const auto position = playbackState_.ResumePosition(currentResumeKey_, duration);
    if (!position) return;
    pendingResumePosition_ = position;
    resumePromptWasPlaying_ = !engine_.IsPaused();
    if (resumePromptWasPlaying_) engine_.SetPaused(true);
    ShowChoiceOverlay(QStringLiteral("Resume playback"),
                      QStringLiteral("A saved position is available for this video"),
                      {QStringLiteral("Continue from %1").arg(TimeText(*position)),
                       QStringLiteral("Start from the beginning")},
                      0, OverlayAction::Resume);
    overlaySecondary_->hide();
}

void QtPlayerWindow::CompleteResumePrompt(bool resume) {
    if (!pendingResumePosition_) return;
    const double position = *pendingResumePosition_;
    const bool restorePlaying = resumePromptWasPlaying_;
    pendingResumePosition_.reset();
    resumePromptWasPlaying_ = false;
    overlayAction_ = OverlayAction::None;
    HideOverlay();
    if (resume) {
        const double duration = engine_.Duration();
        pendingTimelineValue_ = duration > 0.0
            ? static_cast<int>(std::lround(std::clamp(position / duration, 0.0, 1.0) * 10000.0)) : 0;
        pendingTimelineStarted_ = std::chrono::steady_clock::now();
        engine_.SeekAbsolute(position);
        ShowPlaybackFeedback(QStringLiteral("Resumed  ·  %1").arg(TimeText(position)));
        logger_.Write(LogLevel::Info, "playback", std::format("resumed at {:.1f} seconds", position));
    } else {
        pendingTimelineValue_ = 0;
        pendingTimelineStarted_ = std::chrono::steady_clock::now();
        engine_.SeekAbsolute(0.0);
        const bool saved = !playbackState_.Remove(currentResumeKey_) || PersistPlaybackState();
        if (saved) {
            ShowPlaybackFeedback(QStringLiteral("Started from the beginning"));
            logger_.Write(LogLevel::Info, "playback", "saved position discarded for current media");
        } else {
            ShowError(QStringLiteral("Resume"),
                      "The saved position could not be updated and may appear again after restart");
        }
    }
    if (restorePlaying) engine_.SetPaused(false);
}

void QtPlayerWindow::SavePlaybackProgress(bool completed) {
    if (!playbackStateAvailable_ || !resumeEnabled_ || benchmarkMode_ ||
        resumeSaveSuspended_ || currentResumeKey_.empty()) return;
    bool changed = false;
    if (completed) {
        changed = playbackState_.Remove(currentResumeKey_);
    } else if (engineReady_ && mediaLoaded_) {
        const double position = engine_.Position();
        const double duration = engine_.Duration();
        if (PlaybackStateStore::ShouldPersist(position, duration)) {
            changed = playbackState_.Update(currentResumeKey_, position, duration);
        } else if (std::isfinite(position) && std::isfinite(duration) && duration >= 60.0 &&
                   position >= 10.0) {
            changed = playbackState_.Remove(currentResumeKey_);
        }
    }
    if (!changed) return;
    (void)PersistPlaybackState();
}

bool QtPlayerWindow::PersistPlaybackState() {
    try {
        playbackState_.Save(paths_.config / "playback-state.json");
        return true;
    } catch (const std::exception& error) {
        logger_.Write(LogLevel::Error, "playback", std::string("unable to save resume state: ") + error.what());
        return false;
    }
}

void QtPlayerWindow::SetResumeEnabled(bool enabled) {
    if (enabled == resumeEnabled_) return;
    if (enabled) {
        playbackState_ = PlaybackStateStore::Load(paths_.config / "playback-state.json");
        playbackStateAvailable_ = true;
    }
    config_.Set("playback.resume", enabled ? "true" : "false");
    config_.Save(paths_.config / "player.conf");
    resumeEnabled_ = enabled;
    RefreshRecentMedia();
}

void QtPlayerWindow::ClearPlaybackHistory() {
    (void)playbackState_.Clear();
    playbackState_.Save(paths_.config / "playback-state.json");
    playbackStateAvailable_ = true;
    currentResumeKey_.clear();
    pendingResumeKey_.clear();
    if (config_.GetBool("playback.resume", true)) resumeEnabled_ = true;
    RefreshRecentMedia();
}

void QtPlayerWindow::RefreshRecentMedia() {
    if (!recentList_) return;
    const QSignalBlocker blocker(recentList_);
    recentList_->clear();
    if (recentMediaAvailable_) {
        for (const auto& entry : recentMedia_.Entries()) {
            bool available = true;
            QString secondary = ToQString(entry.detail);
            if (entry.type == RecentMediaType::LocalFile) {
                available = QFileInfo(ToQString(entry.openValue)).isFile();
                if (!available) {
                    if (!secondary.isEmpty()) secondary += QStringLiteral("  ·  ");
                    secondary += QStringLiteral("File not found");
                }
            }
            if (resumeEnabled_ && !entry.resumeKey.empty()) {
                const auto progress = playbackState_.Find(entry.resumeKey);
                if (progress && PlaybackStateStore::ShouldPersist(progress->positionSeconds,
                                                                   progress->durationSeconds)) {
                    if (!secondary.isEmpty()) secondary += QStringLiteral("  ·  ");
                    secondary += QStringLiteral("Continue at %1").arg(TimeText(progress->positionSeconds));
                }
            }
            QString text = ToQString(entry.title);
            if (!secondary.isEmpty()) text += QLatin1Char('\n') + secondary;
            auto* item = new QListWidgetItem(text, recentList_);
            item->setData(Qt::UserRole, ToQString(entry.id));
            item->setData(Qt::UserRole + 1, available);
            item->setToolTip(ToQString(entry.openValue));
            item->setSizeHint(QSize(0, secondary.isEmpty() ? 42 : 52));
        }
    }
    const bool showRecent = recentMediaAvailable_ && recentList_->count() != 0;
    emptyPlayIcon_->setVisible(!showRecent);
    recentTitle_->setVisible(showRecent);
    recentList_->setVisible(showRecent);
    recentOpen_->setVisible(showRecent);
    recentRemove_->setVisible(showRecent);
    recentClear_->setVisible(showRecent);
    if (showRecent) recentList_->setCurrentRow(0);
    UpdateRecentActions();
    LayoutOverlays();
}

void QtPlayerWindow::UpdateRecentActions() {
    if (!recentList_ || !recentOpen_) return;
    const auto* item = recentList_->currentItem();
    recentOpen_->setEnabled(item && item->data(Qt::UserRole + 1).toBool());
    recentRemove_->setEnabled(item != nullptr);
    recentClear_->setEnabled(recentList_->count() != 0);
}

void QtPlayerWindow::OpenSelectedRecent() {
    const auto* item = recentList_->currentItem();
    if (!item || !item->data(Qt::UserRole + 1).toBool()) return;
    const auto* stored = recentMedia_.Find(ToUtf8(item->data(Qt::UserRole).toString()));
    if (!stored) {
        RefreshRecentMedia();
        return;
    }
    const RecentMediaEntry entry = *stored;
    pendingRecentEntry_.reset();
    recentReplayEntry_.reset();
    recentReplaySourceMatched_ = false;
    if (entry.type == RecentMediaType::LocalFile) {
        OpenLocalFile(entry.openValue);
        return;
    }
    pendingRecentOpenValue_ = entry.openValue;
    pendingResumeIdentity_ = PlaybackIdentity(entry.openValue);
    if (entry.type == RecentMediaType::DirectUrl) {
        pendingRecentEntry_ = entry;
        pendingRecentEntry_->resumeKey = PlaybackStateKey(pendingResumeIdentity_);
        OpenMedia(entry.openValue, {}, {}, pendingResumeIdentity_);
        return;
    }
    recentReplayEntry_ = entry;
    recentReplaySourceMatched_ = false;
    ResolveUrl(entry.openValue, {}, true);
}

void QtPlayerWindow::RemoveSelectedRecent() {
    const auto* item = recentList_->currentItem();
    if (!item) return;
    if (!recentMedia_.Remove(ToUtf8(item->data(Qt::UserRole).toString()))) return;
    const bool saved = PersistRecentMedia();
    RefreshRecentMedia();
    if (!saved) ShowError(QStringLiteral("Recent"), "The recent list could not be saved");
}

void QtPlayerWindow::ClearRecentMedia() {
    if (!recentMedia_.Clear()) return;
    const bool saved = PersistRecentMedia();
    RefreshRecentMedia();
    if (!saved) ShowError(QStringLiteral("Recent"), "The recent list could not be saved");
}

void QtPlayerWindow::CommitPendingRecent() {
    if (!pendingRecentEntry_ || !recentMediaAvailable_ || benchmarkMode_) {
        pendingRecentEntry_.reset();
        return;
    }
    RecentMediaEntry entry = std::move(*pendingRecentEntry_);
    pendingRecentEntry_.reset();
    if (!recentMedia_.Upsert(std::move(entry))) {
        logger_.Write(LogLevel::Error, "playback", "invalid recent media entry was not saved");
        return;
    }
    if (!PersistRecentMedia()) return;
    RefreshRecentMedia();
}

bool QtPlayerWindow::PersistRecentMedia() {
    if (!recentMediaAvailable_) return false;
    try {
        recentMedia_.Save(paths_.config / "recent-media.json");
        return true;
    } catch (const std::exception& error) {
        logger_.Write(LogLevel::Error, "playback", std::string("unable to save recent media: ") + error.what());
        return false;
    }
}

void QtPlayerWindow::SaveUserSettings() {
    settingsSaveTimer_->stop();
    if (!settingsDirty_) return;
    try {
        config_.Set("playback.volume", std::to_string(volume_->value()));
        config_.Save(paths_.config / "player.conf");
        settingsDirty_ = false;
    } catch (const std::exception& error) {
        logger_.Write(LogLevel::Error, "config", std::string("unable to save playback settings: ") + error.what());
    }
}

void QtPlayerWindow::RecordInteraction() {
    if (!mediaLoaded_ || overlayMode_ != OverlayMode::None || sourceSelection_) return;
    ShowControls(true);
    hideTimer_->start(kControlsHideDelayMs);
    if (!uiTimer_->isActive()) uiTimer_->start();
}

void QtPlayerWindow::ShowControls(bool show, bool animated) {
    if (!mediaLoaded_ || (!show && (overlayMode_ != OverlayMode::None || sourceSelection_))) return;
    if (!animated || !motionEnabled_) {
        controlsAnimation_->stop();
        controlsVisible_ = show;
        controlsOpacity_->setOpacity(show ? 1.0 : 0.0);
        controls_->setVisible(show);
        if (show) controls_->raise();
        return;
    }
    const qreal targetOpacity = show ? 1.0 : 0.0;
    const bool stateAlreadyMatches = controlsVisible_ == show && controls_->isVisible() == show;
    if (stateAlreadyMatches &&
        (controlsAnimation_->state() == QAbstractAnimation::Running ||
         qAbs(controlsOpacity_->opacity() - targetOpacity) < 0.001)) return;
    controlsVisible_ = show;
    controlsAnimation_->stop();
    if (show) {
        controls_->show();
        controls_->raise();
    }
    controlsAnimation_->setEasingCurve(show ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    controlsAnimation_->setDuration(show ? 180 : 140);
    controlsAnimation_->setStartValue(controlsOpacity_->opacity());
    controlsAnimation_->setEndValue(targetOpacity);
    controlsAnimation_->start();
}

bool QtPlayerWindow::CursorOverControls() const {
    if (!controls_->isVisible()) return false;
    const QRect globalRect(controls_->mapToGlobal(QPoint(0, 0)), controls_->size());
    return globalRect.contains(QCursor::pos());
}

void QtPlayerWindow::UpdateTracks() {
    audioTrackIds_.clear(); subtitleTrackIds_.clear(); videoTrackIds_.clear();
    audioTrackLabels_.clear(); subtitleTrackLabels_.clear(); videoTrackLabels_.clear();
    subtitleTrackLabels_.push_back(QStringLiteral("Off"));
    subtitleTrackIds_.push_back(-1);
    audioSelection_ = -1; subtitleSelection_ = 0; videoSelection_ = -1;
    for (const auto& track : engine_.Tracks()) {
        std::string label = track.title.empty() ? track.language : track.title;
        if (label.empty() && track.type == "video" && track.height > 0)
            label = std::format("{}p {}", track.height, track.codec);
        if (label.empty()) label = std::format("{} {}", track.type, track.id);
        if (track.type == "audio") {
            audioTrackLabels_.push_back(ToQString(label)); audioTrackIds_.push_back(track.id);
            if (track.selected) audioSelection_ = static_cast<int>(audioTrackIds_.size() - 1);
        } else if (track.type == "sub") {
            subtitleTrackLabels_.push_back(ToQString(label)); subtitleTrackIds_.push_back(track.id);
            if (track.selected) subtitleSelection_ = static_cast<int>(subtitleTrackIds_.size() - 1);
        } else if (track.type == "video") {
            videoTrackLabels_.push_back(ToQString(label)); videoTrackIds_.push_back(track.id);
            if (track.selected) videoSelection_ = static_cast<int>(videoTrackIds_.size() - 1);
        }
    }
    if (audioSelection_ < 0 && !audioTrackIds_.empty()) audioSelection_ = 0;
    if (videoSelection_ < 0 && !videoTrackIds_.empty()) videoSelection_ = 0;
    audioButton_->setEnabled(mediaLoaded_ && !audioTrackIds_.empty());
    subtitleButton_->setEnabled(mediaLoaded_);
    videoButton_->setEnabled(mediaLoaded_ && !videoTrackIds_.empty());
    UpdateControlStates();
}

void QtPlayerWindow::ToggleFullscreen() {
    fullscreen_ = !fullscreen_;
    UpdateControlStates();
    if (fullscreen_) {
        showFullScreen();
        ShowControls(false, false);
        ShowPlaybackFeedback(QStringLiteral("Full screen"));
    } else {
        showNormal();
        ShowPlaybackFeedback(QStringLiteral("Windowed"));
        RecordInteraction();
    }
}

void QtPlayerWindow::ToggleStatistics() {
    if (!mediaLoaded_) return;
    statisticsVisible_ = !statisticsVisible_;
    if (statisticsVisible_) {
        lastStatisticsUpdate_ = {};
        uiTimer_->start();
    }
    UpdateControlStates();
    UpdateVisibility();
    ShowPlaybackFeedback(statisticsVisible_ ? QStringLiteral("Statistics on")
                                            : QStringLiteral("Statistics off"));
    RecordInteraction();
}

void QtPlayerWindow::ShowAudioMenu() {
    ShowChoiceOverlay(QStringLiteral("Audio track"), QStringLiteral("Choose the audio stream used for playback"),
                      audioTrackLabels_, audioSelection_, OverlayAction::Audio);
}

void QtPlayerWindow::ShowSubtitleMenu() {
    ShowChoiceOverlay(QStringLiteral("Subtitles"), QStringLiteral("Choose a subtitle track or turn subtitles off"),
                      subtitleTrackLabels_, subtitleSelection_, OverlayAction::Subtitles);
}

void QtPlayerWindow::ShowVideoMenu() {
    ShowChoiceOverlay(QStringLiteral("Video track"), QStringLiteral("Choose the video stream or quality"),
                      videoTrackLabels_, videoSelection_, OverlayAction::Video);
}

void QtPlayerWindow::ShowShaderMenu() {
    std::vector<QString> choices;
    choices.reserve(shaders_.Presets().size() + 1U);
    for (const auto& preset : shaders_.Presets()) choices.push_back(ToQString(preset.name));
    choices.push_back(QStringLiteral("Custom GLSL…"));
    ShowChoiceOverlay(QStringLiteral("Video shaders"), QStringLiteral("Choose a preset or load custom GLSL files"),
                      std::move(choices), static_cast<int>(shaderPresetIndex_), OverlayAction::Shaders);
}

void QtPlayerWindow::ShowSettingsMenu() {
    const auto cache = config_.GetString("network.cache_mode", "balanced");
    const auto hardware = config_.GetString("playback.hwdec", "auto");
    const auto pacing = config_.GetString("playback.video_sync", "display-resample");
    const bool animations = config_.GetBool("ui.animations", true);
    const int selected = cache == "low-latency" ? 0 : cache == "unstable" ? 2 : 1;
    ShowChoiceOverlay(QStringLiteral("Playback settings"), QStringLiteral("Choose a setting to apply"),
                      {SettingChoice(cache == "low-latency", QStringLiteral("Network cache  ·  Low latency")),
                       SettingChoice(cache != "low-latency" && cache != "unstable", QStringLiteral("Network cache  ·  Balanced")),
                       SettingChoice(cache == "unstable", QStringLiteral("Network cache  ·  Unstable connection")),
                       SettingChoice(hardware != "no", QStringLiteral("Hardware decoding  ·  Auto")),
                       SettingChoice(hardware == "no", QStringLiteral("Hardware decoding  ·  Off")),
                       SettingChoice(pacing != "audio", QStringLiteral("Frame pacing  ·  Display resample")),
                       SettingChoice(pacing == "audio", QStringLiteral("Frame pacing  ·  Audio clock")),
                       SettingChoice(resumeEnabled_, QStringLiteral("Resume playback  ·  On")),
                       SettingChoice(!resumeEnabled_, QStringLiteral("Resume playback  ·  Off")),
                       SettingChoice(animations, QStringLiteral("Interface animations  ·  On (next launch)")),
                       SettingChoice(!animations, QStringLiteral("Interface animations  ·  Off (next launch)")),
                       QStringLiteral("Clear saved playback positions")}, selected, OverlayAction::Settings);
}

void QtPlayerWindow::ShowChoiceOverlay(QString title, QString hint, std::vector<QString> choices,
                                       int selected, OverlayAction action) {
    if (choices.empty()) return;
    if (sourceSelection_) HideSourceSelector();
    overlayMode_ = OverlayMode::Choice;
    overlayAction_ = action;
    overlayChoices_ = std::move(choices);
    overlayTitle_->setText(std::move(title));
    overlayBody_->setText(std::move(hint));
    overlayPrimary_->setText(QStringLiteral("Apply"));
    overlaySecondary_->setText(QStringLiteral("Cancel"));
    overlayEdit_->hide();
    overlayList_->clear();
    for (const auto& choice : overlayChoices_) overlayList_->addItem(choice);
    overlayList_->show();
    overlaySecondary_->show();
    const int index = selected >= 0 && static_cast<std::size_t>(selected) < overlayChoices_.size() ? selected : 0;
    overlayList_->setCurrentRow(index);
    ShowControls(true, false);
    UpdateVisibility();
    overlayList_->setFocus();
}

void QtPlayerWindow::ShowMessageOverlay(QString title, QString detail) {
    if (sourceSelection_) HideSourceSelector();
    overlayMode_ = OverlayMode::Message;
    overlayAction_ = OverlayAction::None;
    overlayChoices_.clear();
    overlayTitle_->setText(std::move(title));
    overlayBody_->setText(std::move(detail));
    overlayPrimary_->setText(QStringLiteral("Close"));
    overlaySecondary_->hide();
    overlayEdit_->hide();
    overlayList_->hide();
    ShowControls(true, false);
    UpdateVisibility();
    overlayPrimary_->setFocus();
}

void QtPlayerWindow::HideOverlay() {
    if (overlayAction_ == OverlayAction::Resume && pendingResumePosition_) {
        CompleteResumePrompt(true);
        return;
    }
    overlayMode_ = OverlayMode::None;
    overlayAction_ = OverlayAction::None;
    overlayChoices_.clear();
    UpdateVisibility();
    setFocus();
    RecordInteraction();
}

void QtPlayerWindow::ApplyOverlaySelection() {
    if (overlayMode_ == OverlayMode::Message) { HideOverlay(); return; }
    if (overlayMode_ == OverlayMode::Url) {
        const QString value = overlayEdit_->text().trimmed();
        if (value.isEmpty()) return;
        HideOverlay();
        ResolveUrl(ToUtf8(value));
        return;
    }
    if (overlayMode_ != OverlayMode::Choice) return;
    const int selected = overlayList_->currentRow();
    if (selected < 0 || static_cast<std::size_t>(selected) >= overlayChoices_.size()) return;
    const auto action = overlayAction_;
    if (action == OverlayAction::Resume) {
        CompleteResumePrompt(selected == 0);
        return;
    }
    HideOverlay();
    try {
        if (action == OverlayAction::Audio && static_cast<std::size_t>(selected) < audioTrackIds_.size()) {
            audioSelection_ = selected;
            engine_.SetAudioTrack(audioTrackIds_[static_cast<std::size_t>(selected)]);
            ShowPlaybackFeedback(QStringLiteral("Audio  ·  %1").arg(audioTrackLabels_[static_cast<std::size_t>(selected)]));
        } else if (action == OverlayAction::Subtitles && static_cast<std::size_t>(selected) < subtitleTrackIds_.size()) {
            subtitleSelection_ = selected;
            engine_.SetSubtitleTrack(subtitleTrackIds_[static_cast<std::size_t>(selected)]);
            UpdateControlStates();
            ShowPlaybackFeedback(QStringLiteral("Subtitles  ·  %1").arg(subtitleTrackLabels_[static_cast<std::size_t>(selected)]));
        } else if (action == OverlayAction::Video && static_cast<std::size_t>(selected) < videoTrackIds_.size()) {
            videoSelection_ = selected;
            engine_.SetVideoTrack(videoTrackIds_[static_cast<std::size_t>(selected)]);
            ShowPlaybackFeedback(QStringLiteral("Video  ·  %1").arg(videoTrackLabels_[static_cast<std::size_t>(selected)]));
        } else if (action == OverlayAction::Shaders) {
            ApplyShaderPreset(static_cast<std::size_t>(selected));
        } else if (action == OverlayAction::Settings && selected >= 0 && selected <= 2) {
            const std::string value = selected == 0 ? "low-latency" : selected == 2 ? "unstable" : "balanced";
            engine_.ConfigureCache(value);
            config_.Set("network.cache_mode", value);
            config_.Save(paths_.config / "player.conf");
            ShowPlaybackFeedback(QStringLiteral("Cache  ·  %1").arg(selected == 0 ? QStringLiteral("Low latency") :
                                                                    selected == 2 ? QStringLiteral("Unstable connection") :
                                                                                    QStringLiteral("Balanced")));
        } else if (action == OverlayAction::Settings && (selected == 3 || selected == 4)) {
            const bool enabled = selected == 3;
            engine_.SetHardwareDecoding(enabled);
            config_.Set("playback.hwdec", enabled ? "auto" : "no");
            config_.Save(paths_.config / "player.conf");
            ShowPlaybackFeedback(enabled ? QStringLiteral("Hardware decoding  ·  Auto")
                                         : QStringLiteral("Hardware decoding  ·  Off"));
        } else if (action == OverlayAction::Settings && (selected == 5 || selected == 6)) {
            const std::string value = selected == 5 ? "display-resample" : "audio";
            engine_.SetVideoSync(value);
            config_.Set("playback.video_sync", value);
            config_.Save(paths_.config / "player.conf");
            ShowPlaybackFeedback(selected == 5 ? QStringLiteral("Frame pacing  ·  Display resample")
                                               : QStringLiteral("Frame pacing  ·  Audio clock"));
        } else if (action == OverlayAction::Settings && (selected == 7 || selected == 8)) {
            const bool enabled = selected == 7;
            SetResumeEnabled(enabled);
            ShowPlaybackFeedback(enabled ? QStringLiteral("Resume playback  ·  On")
                                         : QStringLiteral("Resume playback  ·  Off"));
        } else if (action == OverlayAction::Settings && (selected == 9 || selected == 10)) {
            const bool enabled = selected == 9;
            config_.Set("ui.animations", enabled ? "true" : "false");
            config_.Save(paths_.config / "player.conf");
            ShowPlaybackFeedback(enabled ? QStringLiteral("Animations on after restart")
                                         : QStringLiteral("Animations off after restart"));
        } else if (action == OverlayAction::Settings && selected == 11) {
            ClearPlaybackHistory();
            ShowPlaybackFeedback(QStringLiteral("Playback history cleared"));
        }
    } catch (const std::exception& error) {
        ShowError(QStringLiteral("Action failed"), error.what());
    }
}

void QtPlayerWindow::ApplyConfiguredShader() {
    if (shaderPresetIndex_ == 0 || shaderPresetIndex_ >= shaders_.Presets().size()) return;
    try {
        engine_.SetShaders(shaders_.Resolve(shaders_.Presets()[shaderPresetIndex_]));
    } catch (const std::exception& error) {
        shaderPresetIndex_ = 0;
        engine_.SetShaders({});
        logger_.Write(LogLevel::Error, "shader", std::string("unable to restore shader preset: ") + error.what());
    }
}

void QtPlayerWindow::PersistShaderPreset(std::size_t index) {
    if (index >= shaders_.Presets().size()) return;
    config_.Set("shader.preset", shaders_.Presets()[index].id);
    config_.Save(paths_.config / "player.conf");
}

void QtPlayerWindow::ApplyShaderHotkey(int hotkey, bool persist) {
    if (!mediaLoaded_) return;
    const auto* preset = shaders_.ForHotkey(hotkey);
    if (!preset) return;
    try {
        engine_.SetShaders(shaders_.Resolve(*preset));
        const auto iterator = std::ranges::find_if(shaders_.Presets(), [preset](const ShaderPreset& item) {
            return item.id == preset->id;
        });
        shaderPresetIndex_ = iterator == shaders_.Presets().end()
            ? 0U : static_cast<std::size_t>(std::distance(shaders_.Presets().begin(), iterator));
        UpdateControlStates();
        ShowPlaybackFeedback(QStringLiteral("Shaders  ·  %1").arg(ToQString(preset->name)));
    } catch (const std::exception& error) {
        engine_.SetShaders({});
        shaderPresetIndex_ = 0;
        UpdateControlStates();
        ShowError(QStringLiteral("Shader"), error.what());
        return;
    }
    if (persist) {
        try {
            PersistShaderPreset(shaderPresetIndex_);
        } catch (const std::exception& error) {
            ShowError(QStringLiteral("Settings"), error.what());
        }
    }
}

void QtPlayerWindow::ApplyShaderPreset(std::size_t index, bool persist) {
    if (index == shaders_.Presets().size()) { OpenCustomShaders(); return; }
    if (index >= shaders_.Presets().size()) return;
    try {
        engine_.SetShaders(shaders_.Resolve(shaders_.Presets()[index]));
        shaderPresetIndex_ = index;
        UpdateControlStates();
        ShowPlaybackFeedback(QStringLiteral("Shaders  ·  %1").arg(ToQString(shaders_.Presets()[index].name)));
    } catch (const std::exception& error) {
        engine_.SetShaders({});
        shaderPresetIndex_ = 0;
        UpdateControlStates();
        ShowError(QStringLiteral("Shader"), error.what());
        return;
    }
    if (persist) {
        try {
            PersistShaderPreset(shaderPresetIndex_);
        } catch (const std::exception& error) {
            ShowError(QStringLiteral("Settings"), error.what());
        }
    }
}

void QtPlayerWindow::OpenCustomShaders() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Open GLSL shaders"), QString(), QStringLiteral("GLSL shaders (*.glsl);;All files (*.*)"));
    if (files.isEmpty()) return;
    try {
        const auto destinationRoot = paths_.shaders / "Custom";
        std::filesystem::create_directories(destinationRoot);
        std::vector<std::filesystem::path> imported;
        imported.reserve(static_cast<std::size_t>(files.size()));
        for (const auto& file : files) {
            const std::filesystem::path source(ToUtf8(file));
            if (source.extension() != ".glsl") throw std::runtime_error("Only .glsl files can be loaded as shaders");
            const auto destination = destinationRoot / source.filename();
            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
            imported.push_back(std::filesystem::weakly_canonical(destination));
        }
        engine_.SetShaders(imported);
        shaderPresetIndex_ = shaders_.Presets().size();
        UpdateControlStates();
        try {
            config_.Set("shader.preset", "off");
            config_.Save(paths_.config / "player.conf");
        } catch (const std::exception& error) {
            ShowError(QStringLiteral("Settings"), error.what());
            return;
        }
        ShowPlaybackFeedback(QStringLiteral("Shaders  ·  Custom (this session)"));
    } catch (const std::exception& error) {
        engine_.SetShaders({});
        shaderPresetIndex_ = 0;
        UpdateControlStates();
        ShowError(QStringLiteral("Custom shaders"), error.what());
    }
}

void QtPlayerWindow::ShowError(QString title, std::string_view detail) {
    logger_.Write(LogLevel::Error, "ui", detail);
    ShowMessageOverlay(std::move(title), ToQString(detail));
}

void QtPlayerWindow::LogHardwareInformation() {
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    logger_.Write(LogLevel::Info, "hardware", std::format("Windows architecture={} logical_processors={}",
                  system.wProcessorArchitecture, system.dwNumberOfProcessors));
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return;
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)))
            logger_.Write(LogLevel::Info, "hardware", "GPU: " + ToUtf8(QString::fromWCharArray(description.Description)));
        for (UINT outputIndex = 0;; ++outputIndex) {
            IDXGIOutput* output = nullptr;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
            IDXGIOutput6* output6 = nullptr;
            if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6)))) {
                DXGI_OUTPUT_DESC1 outputDescription{};
                if (SUCCEEDED(output6->GetDesc1(&outputDescription))) {
                    logger_.Write(LogLevel::Info, "hardware", std::format(
                        "Display={} bits_per_color={} colorspace={} HDR_candidate={}",
                        ToUtf8(QString::fromWCharArray(outputDescription.DeviceName)), outputDescription.BitsPerColor,
                        static_cast<unsigned>(outputDescription.ColorSpace), outputDescription.BitsPerColor >= 10));
                }
                output6->Release();
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    DEVMODEW mode{sizeof(mode)};
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
        logger_.Write(LogLevel::Info, "hardware", std::format("Primary display {}x{} {}bpp {}Hz",
                      mode.dmPelsWidth, mode.dmPelsHeight, mode.dmBitsPerPel, mode.dmDisplayFrequency));
    }
}

void QtPlayerWindow::FinishBenchmark() {
    benchmarkRunning_ = false;
    if (benchmarkSamples_.empty()) benchmarkSamples_.push_back(engine_.Statistics());
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - benchmarkStart_).count();
    double cpuTotal = 0.0;
    double cpuPeak = 0.0;
    for (const auto& sample : benchmarkSamples_) {
        cpuTotal += sample.cpuPercent;
        cpuPeak = std::max(cpuPeak, sample.cpuPercent);
    }
    const auto& last = benchmarkSamples_.back();
    const auto droppedAfterWarmup = std::max<std::int64_t>(0, last.droppedFrames - std::max<std::int64_t>(0, benchmarkDroppedBaseline_));
    const auto delayedAfterWarmup = std::max<std::int64_t>(0, last.delayedFrames - std::max<std::int64_t>(0, benchmarkDelayedBaseline_));
    nlohmann::json report{
        {"file", benchmarkInput_}, {"profile", benchmarkProfile_}, {"elapsed_seconds", elapsed},
        {"codec", last.videoCodec}, {"resolution", last.resolution}, {"fps", last.fps},
        {"pixel_format", last.pixelFormat}, {"bit_depth", last.bitDepth}, {"hdr", last.hdrStatus},
        {"decoder", last.hardwareDecoder}, {"renderer", last.gpuRenderer},
        {"average_cpu_percent", cpuTotal / static_cast<double>(benchmarkSamples_.size())},
        {"peak_cpu_percent", cpuPeak}, {"dropped_frames", droppedAfterWarmup},
        {"delayed_frames", delayedAfterWarmup}, {"dropped_frames_total", last.droppedFrames},
        {"delayed_frames_total", last.delayedFrames}, {"warmup_seconds", 1.0},
        {"shader_configuration", last.shaderChain},
        {"timing_note", "decode/render timing is not exposed by the stable libmpv client API in this build"}
    };
    const auto output = report.dump(2) + "\n";
    std::ofstream(paths_.root / "benchmark.json", std::ios::trunc) << output;
    logger_.Write(LogLevel::Info, "benchmark", "Benchmark completed; report written to benchmark.json");
    close();
}

} // namespace wannaviewer

#endif
