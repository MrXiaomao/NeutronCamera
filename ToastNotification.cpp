#include "ToastNotification.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStyle>

#include <algorithm>
#include <utility>

namespace
{
constexpr char ToastManagerObjectName[] = "_toast_manager_instance";

QString colorName(const QColor &color)
{
    return color.name(QColor::HexRgb);
}
}

ToastNotification::ToastNotification(Type type,
                                     const QString &title,
                                     const QString &message,
                                     int durationMs,
                                     const QVector<Action> &actions,
                                     QWidget *parent)
    : QFrame(parent)
    , m_type(type)
    , m_title(title)
    , m_message(message)
    , m_actions(actions)
    , m_durationMs(std::max(0, durationMs))
    , m_remainingMs(std::max(0, durationMs))
{
    setObjectName(QStringLiteral("ToastNotification"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowFlags(Qt::FramelessWindowHint);
    setFixedWidth(360);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(40);
    connect(m_tickTimer, &QTimer::timeout, this, [this]() {
        if (m_hovered || m_durationMs <= 0 || m_closing) {
            return;
        }
        m_remainingMs = std::max(0, m_remainingMs - m_tickTimer->interval());
        updateProgress();
        if (m_remainingMs <= 0) {
            closeToast();
        }
    });

	m_iconLabel = new QLabel(this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(34, 34);
	QIcon icon;
	// 调用Qt系统内置标准图标，无需额外导入资源
	switch(type)
	{
    case Type::Info:
        icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
        break;
	case Type::Success:
		icon = style()->standardIcon(QStyle::SP_DialogApplyButton);
		break;
	case Type::Warning:
		icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
		break;
	case Type::Error:
		icon = style()->standardIcon(QStyle::SP_MessageBoxCritical);
		break;
	default: break;
	}
	m_iconLabel->setPixmap(icon.pixmap(34,34));
			
    m_titleLabel = new QLabel(m_title, this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_messageLabel = new QLabel(m_message, this);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_closeButton = new QToolButton(this);
    m_closeButton->setText(QStringLiteral("x"));
    m_closeButton->setAutoRaise(true);
    m_closeButton->setFixedSize(24, 24);
    m_closeButton->setCursor(Qt::PointingHandCursor);
    connect(m_closeButton, &QToolButton::clicked, this, &ToastNotification::closeToast);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(3);
    m_progressBar->setValue(1000);

    auto *textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(4);
    textLayout->addWidget(m_titleLabel);
    textLayout->addWidget(m_messageLabel);

    auto *topLayout = new QHBoxLayout;
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);
    topLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);
    topLayout->addLayout(textLayout, 1);
    topLayout->addWidget(m_closeButton, 0, Qt::AlignTop);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 12, 14, 10);
    rootLayout->setSpacing(10);
    rootLayout->addLayout(topLayout);

    auto *actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(46, 0, 0, 0);
    actionLayout->setSpacing(8);
    actionLayout->addStretch(1);

    m_okButton = new QPushButton(QStringLiteral("确定"), this);
    m_okButton->setObjectName(QStringLiteral("ToastOkButton"));
    m_okButton->setCursor(Qt::PointingHandCursor);
    m_okButton->setFixedHeight(26);
    connect(m_okButton, &QPushButton::clicked, this, &ToastNotification::accept);
    actionLayout->addWidget(m_okButton);

    for (const Action &action : std::as_const(m_actions)) {
        auto *actionButton = new QPushButton(action.text, this);
        actionButton->setObjectName(action.primary ? QStringLiteral("ToastPrimaryActionButton")
                                                   : QStringLiteral("ToastActionButton"));
        actionButton->setCursor(Qt::PointingHandCursor);
        actionButton->setFixedHeight(26);
        connect(actionButton, &QPushButton::clicked, this, [this, action]() {
            emit actionTriggered(this, action.id);
        });
        actionLayout->addWidget(actionButton);
    }

    rootLayout->addLayout(actionLayout);
    rootLayout->addWidget(m_progressBar);

    applyTypeStyle();
    updateProgress();
}

ToastNotification::Type ToastNotification::type() const
{
    return m_type;
}

QString ToastNotification::title() const
{
    return m_title;
}

QString ToastNotification::message() const
{
    return m_message;
}

int ToastNotification::durationMs() const
{
    return m_durationMs;
}

QString ToastNotification::fullText() const
{
    return m_title + QStringLiteral("\n") + m_message;
}

void ToastNotification::start()
{
    if (m_durationMs > 0) {
        m_tickTimer->start();
    }

    auto *animation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    animation->setDuration(160);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QPropertyAnimation::finished, animation, &QObject::deleteLater);
    animation->start();
}

void ToastNotification::closeToast()
{
    if (m_closing) {
        return;
    }
    m_closing = true;
    m_tickTimer->stop();

    auto *animation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    animation->setDuration(180);
    animation->setStartValue(m_opacityEffect->opacity());
    animation->setEndValue(0.0);
    animation->setEasingCurve(QEasingCurve::InCubic);
    connect(animation, &QPropertyAnimation::finished, this, [this, animation]() {
        animation->deleteLater();
        emit dismissed(this);
        deleteLater();
    });
    animation->start();
}

void ToastNotification::enterEvent(QEvent *event)
{
    m_hovered = true;
    QFrame::enterEvent(event);
}

void ToastNotification::leaveEvent(QEvent *event)
{
    m_hovered = false;
    QFrame::leaveEvent(event);
}

void ToastNotification::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_closing) {
        emit clicked(this);
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void ToastNotification::applyTypeStyle()
{
    const QColor accent = accentColor();
    const QColor border = accent.lighter(145);
    const QColor iconBackground = accent;

    setStyleSheet(QStringLiteral(
                      "QFrame#ToastNotification{"
                      "background:#ffffff;"
                      "border:1px solid %1;"
                      "border-left:5px solid %2;"
                      "border-radius:8px;"
                      "}"
                      "QLabel{color:#29323a;}"
                      "QToolButton{border:0;color:#66727c;font-weight:bold;}"
                      "QToolButton:hover{color:#202830;background:#eef3f6;border-radius:4px;}"
                      "QPushButton#ToastOkButton,QPushButton#ToastActionButton{"
                      "background:#f5f7f9;border:1px solid #d4dde4;border-radius:5px;padding:0 10px;color:#43505a;}"
                      "QPushButton#ToastOkButton:hover,QPushButton#ToastActionButton:hover{"
                      "background:#eef4fa;border-color:%2;color:#202830;}"
                      "QPushButton#ToastPrimaryActionButton{"
                      "background:%2;border:1px solid %2;border-radius:5px;padding:0 10px;color:white;font-weight:bold;}"
                      "QPushButton#ToastPrimaryActionButton:hover{background:%3;border-color:%3;}"
                      "QProgressBar{border:0;background:#eef3f6;border-radius:1px;}"
                      "QProgressBar::chunk{background:%2;border-radius:1px;}")
                      .arg(colorName(border), colorName(accent), colorName(accent.darker(108))));

    if (m_type == Type::Success)
        m_iconLabel->setStyleSheet(QStringLiteral(
                                    "background:%1;color:white;border-radius:17px;font-weight:bold;")
                                    .arg(colorName(iconBackground)));
}

void ToastNotification::updateProgress()
{
    if (m_durationMs <= 0) {
        m_progressBar->setValue(1000);
        return;
    }
    const int value = std::clamp(static_cast<int>(m_remainingMs * 1000.0 / m_durationMs), 0, 1000);
    m_progressBar->setValue(value);
}

void ToastNotification::copyTextToClipboard()
{
    QApplication::clipboard()->setText(fullText());
}

void ToastNotification::accept()
{
    this->closeToast();

    emit accepted(this);
}

QString ToastNotification::iconText() const
{
    switch (m_type) {
    case Type::Success:
        return QStringLiteral("OK");
    case Type::Warning:
        return QStringLiteral("!");
    case Type::Error:
        return QStringLiteral("X");
    case Type::Info:
    default:
        return QStringLiteral("i");
    }
}

QColor ToastNotification::accentColor() const
{
    switch (m_type) {
    case Type::Success:
        return QColor("#1F9D63");
    case Type::Warning:
        return QColor("#D88416");
    case Type::Error:
        return QColor("#D64545");
    case Type::Info:
    default:
        return QColor("#2E7DD1");
    }
}

ToastManager::ToastManager(QWidget *host, QObject *parent)
    : QObject(parent)
    , m_host(host)
{
    Q_ASSERT(m_host);
    setObjectName(QString::fromLatin1(ToastManagerObjectName));
    m_host->installEventFilter(this);
}

ToastManager *ToastManager::instance(QWidget *host)
{
    if (!host) {
        return nullptr;
    }

    // Manager 绑定在宿主窗口下面，避免全局单例跨窗口串消息，也能随窗口自动释放。
    if (auto *manager = host->findChild<ToastManager *>(QString::fromLatin1(ToastManagerObjectName), Qt::FindDirectChildrenOnly)) {
        return manager;
    }
    return new ToastManager(host, host);
}

void ToastManager::notify(QWidget *host,
                          ToastNotification::Type type,
                          const QString &title,
                          const QString &message,
                          int durationMs,
                          const QVector<ToastNotification::Action> &actions)
{
    if (ToastManager *manager = instance(host)) {
        manager->show(type, title, message, durationMs, actions);
    }
}

void ToastManager::info(QWidget *host, const QString &title, const QString &message)
{
    notify(host, ToastNotification::Type::Info, title, message);
}

void ToastManager::success(QWidget *host, const QString &title, const QString &message)
{
    notify(host, ToastNotification::Type::Success, title, message);
}

void ToastManager::warning(QWidget *host, const QString &title, const QString &message)
{
    notify(host, ToastNotification::Type::Warning, title, message);
}

void ToastManager::error(QWidget *host, const QString &title, const QString &message)
{
    notify(host, ToastNotification::Type::Error, title, message);
}

void ToastManager::setPosition(Position position)
{
    if (m_position == position) {
        return;
    }
    m_position = position;
    relayout(true);
}

void ToastManager::setMaxVisible(int count)
{
    m_maxVisible = std::max(1, count);
    while (m_toasts.size() > m_maxVisible) {
        m_toasts.front()->closeToast();
        m_toasts.removeFirst();
    }
    relayout(true);
}

void ToastManager::setMaxQueued(int count)
{
    m_maxQueued = std::max(0, count);
    while (m_queue.size() > m_maxQueued) {
        m_queue.dequeue();
    }
}

void ToastManager::setDuration(int durationMs)
{
    m_defaultDurationMs = std::max(0, durationMs);
}

int ToastManager::queuedCount() const
{
    return m_queue.size();
}

void ToastManager::show(ToastNotification::Type type,
                        const QString &title,
                        const QString &message,
                        int durationMs,
                        const QVector<ToastNotification::Action> &actions)
{
    if (!m_host) {
        return;
    }

    PendingToast pending;
    pending.type = type;
    pending.title = title;
    pending.message = message;
    pending.durationMs = durationMs >= 0 ? durationMs : m_defaultDurationMs;
    pending.actions = actions;

    if (m_toasts.size() >= m_maxVisible) {
        enqueueToast(pending);
        return;
    }

    showNow(pending);
}

void ToastManager::enqueueToast(const PendingToast &toast)
{
    if (m_maxQueued <= 0) {
        return;
    }
    // 队列满时丢弃最旧消息，优先保证最新业务状态能被用户看到。
    while (m_queue.size() >= m_maxQueued) {
        m_queue.dequeue();
    }
    m_queue.enqueue(toast);
}

void ToastManager::showNow(const PendingToast &pending)
{
    auto *toast = new ToastNotification(pending.type,
                                        pending.title,
                                        pending.message,
                                        pending.durationMs,
                                        pending.actions,
                                        m_host);
    connect(toast, &ToastNotification::dismissed, this, &ToastManager::removeToast);
    connect(toast, &ToastNotification::clicked, this, [this](ToastNotification *item) {
        emit toastClicked(item->title(), item->message());
    });
    connect(toast, &ToastNotification::actionTriggered, this, [this](ToastNotification *item, const QString &actionId) {
        emit toastActionTriggered(actionId, item->title(), item->message());
    });
    connect(toast, &ToastNotification::copied, this, [this](ToastNotification *item) {
        emit toastCopied(item->fullText());
    });

    toast->adjustSize();
    m_toasts.push_back(toast);

    const QRect target = targetGeometryFor(m_toasts.size() - 1, toast->sizeHint());
    const int offset = (m_position == Position::BottomCenter || m_position == Position::BottomRight) ? 14 : -14;
    toast->setGeometry(target.translated(0, offset));
    toast->show();
    toast->raise();
    toast->start();

    relayout(true);
}

void ToastManager::pumpQueue()
{
    // 每关闭一个 Toast 就尝试补位，始终维持“最多可见 N 个”的稳定布局。
    while (!m_queue.isEmpty() && m_toasts.size() < m_maxVisible) {
        showNow(m_queue.dequeue());
    }
}

void ToastManager::info(const QString &title,
                        const QString &message,
                        const QVector<ToastNotification::Action> &actions)
{
    show(ToastNotification::Type::Info, title, message, -1, actions);
}

void ToastManager::success(const QString &title,
                           const QString &message,
                           const QVector<ToastNotification::Action> &actions)
{
    show(ToastNotification::Type::Success, title, message, -1, actions);
}

void ToastManager::warning(const QString &title,
                           const QString &message,
                           const QVector<ToastNotification::Action> &actions)
{
    show(ToastNotification::Type::Warning, title, message, -1, actions);
}

void ToastManager::error(const QString &title,
                         const QString &message,
                         const QVector<ToastNotification::Action> &actions)
{
    show(ToastNotification::Type::Error, title, message, -1, actions);
}

void ToastManager::clear()
{
    const QVector<ToastNotification *> toasts = m_toasts;
    m_toasts.clear();
    m_queue.clear();
    for (ToastNotification *toast : toasts) {
        if (toast) {
            toast->closeToast();
        }
    }
}

bool ToastManager::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host && event->type() == QEvent::Resize) {
        relayout(false);
    }
    return QObject::eventFilter(watched, event);
}

void ToastManager::removeToast(ToastNotification *toast)
{
    m_toasts.removeAll(toast);
    pumpQueue();
    relayout(true);
}

QRect ToastManager::targetGeometryFor(int index, const QSize &size) const
{
    const int width = std::min(360, std::max(260, m_host->width() - m_margin * 2));
    const int height = size.height();
    int x = m_margin;
    int y = m_margin;

    if (m_position == Position::TopRight || m_position == Position::BottomRight) {
        x = m_host->width() - width - m_margin;
    } else {
        x = (m_host->width() - width) / 2;
    }

    if (m_position == Position::TopRight || m_position == Position::TopCenter) {
        y = m_margin + index * (height + m_spacing);
    } else {
        y = m_host->height() - m_margin - height - index * (height + m_spacing);
    }

    return QRect(x, y, width, height);
}

void ToastManager::relayout(bool animated)
{
    for (int i = 0; i < m_toasts.size(); ++i) {
        ToastNotification *toast = m_toasts[i];
        if (!toast) {
            continue;
        }

        toast->adjustSize();
        const QRect target = targetGeometryFor(i, toast->sizeHint());
        toast->raise();

        if (!animated) {
            toast->setGeometry(target);
            continue;
        }

        auto *animation = new QPropertyAnimation(toast, "geometry", toast);
        animation->setDuration(180);
        animation->setStartValue(toast->geometry());
        animation->setEndValue(target);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QPropertyAnimation::finished, animation, &QObject::deleteLater);
        animation->start();
    }
}
