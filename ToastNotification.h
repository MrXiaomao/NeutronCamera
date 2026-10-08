#pragma once

#include <QFrame>
#include <QQueue>
#include <QString>
#include <QVector>

class QLabel;
class QEnterEvent;
class QEvent;
class QMouseEvent;
class QPushButton;
class QProgressBar;
class QTimer;
class QToolButton;
class QGraphicsOpacityEffect;

class ToastNotification final : public QFrame
{
    Q_OBJECT

public:
    enum class Type
    {
        Info,
        Success,
        Warning,
        Error
    };

    // Action 只保存业务标识和按钮文案，点击后由 ToastManager 统一转发给宿主窗口。
    struct Action
    {
        QString id;
        QString text;
        bool primary = false;

        Action() = default;
        Action(const QString &actionId, const QString &actionText, bool isPrimary = false)
            : id(actionId)
            , text(actionText)
            , primary(isPrimary)
        {
        }
    };

    ToastNotification(Type type,
                      const QString &title,
                      const QString &message,
                      int durationMs,
                      const QVector<Action> &actions = QVector<Action>(),
                      QWidget *parent = nullptr);

    Type type() const;
    QString title() const;
    QString message() const;
    int durationMs() const;
    QString fullText() const;

public slots:
    void start();
    void closeToast();

signals:
    void dismissed(ToastNotification *toast);
    void clicked(ToastNotification *toast);
    void actionTriggered(ToastNotification *toast, const QString &actionId);
    void copied(ToastNotification *toast);
    void accepted(ToastNotification *toast);

protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void applyTypeStyle();
    void updateProgress();
    void copyTextToClipboard();
    void accept();
    QString iconText() const;
    QColor accentColor() const;

    Type m_type = Type::Info;
    QString m_title;
    QString m_message;
    QVector<Action> m_actions;
    int m_durationMs = 3000;
    int m_remainingMs = 3000;
    bool m_hovered = false;
    bool m_closing = false;

    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QToolButton *m_closeButton = nullptr;
    QPushButton *m_okButton = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QGraphicsOpacityEffect *m_opacityEffect = nullptr;
    QTimer *m_tickTimer = nullptr;
};

class ToastManager final : public QObject
{
    Q_OBJECT

public:
    enum class Position
    {
        TopRight,
        TopCenter,
        BottomRight,
        BottomCenter
    };

    explicit ToastManager(QWidget *host, QObject *parent = nullptr);

    // 每个宿主窗口挂一个 ToastManager，方便业务层用静态入口发消息。
    static ToastManager *instance(QWidget *host);
    static void notify(QWidget *host,
                       ToastNotification::Type type,
                       const QString &title,
                       const QString &message,
                       int durationMs = -1,
                       const QVector<ToastNotification::Action> &actions = QVector<ToastNotification::Action>());
    static void info(QWidget *host, const QString &title, const QString &message);
    static void success(QWidget *host, const QString &title, const QString &message);
    static void warning(QWidget *host, const QString &title, const QString &message);
    static void error(QWidget *host, const QString &title, const QString &message);

    void setPosition(Position position);
    void setMaxVisible(int count);
    void setMaxQueued(int count);
    void setDuration(int durationMs);
    int queuedCount() const;

    void show(ToastNotification::Type type,
              const QString &title,
              const QString &message,
              int durationMs = -1,
              const QVector<ToastNotification::Action> &actions = QVector<ToastNotification::Action>());
    void info(const QString &title,
              const QString &message,
              const QVector<ToastNotification::Action> &actions = QVector<ToastNotification::Action>());
    void success(const QString &title,
                 const QString &message,
                 const QVector<ToastNotification::Action> &actions = QVector<ToastNotification::Action>());
    void warning(const QString &title,
                 const QString &message,
                 const QVector<ToastNotification::Action> &actions = QVector<ToastNotification::Action>());
    void error(const QString &title,
               const QString &message,
               const QVector<ToastNotification::Action> &actions = QVector<ToastNotification::Action>());
    void clear();

signals:
    void toastClicked(const QString &title, const QString &message);
    void toastActionTriggered(const QString &actionId, const QString &title, const QString &message);
    void toastCopied(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void removeToast(ToastNotification *toast);

private:
    // 未显示的消息先进入队列，等当前可见 Toast 消失后再真正创建控件。
    struct PendingToast
    {
        ToastNotification::Type type = ToastNotification::Type::Info;
        QString title;
        QString message;
        int durationMs = -1;
        QVector<ToastNotification::Action> actions;
    };

    QRect targetGeometryFor(int index, const QSize &size) const;
    void enqueueToast(const PendingToast &toast);
    void showNow(const PendingToast &toast);
    void pumpQueue();
    void relayout(bool animated);

    QWidget *m_host = nullptr;
    QVector<ToastNotification *> m_toasts;
    // 队列用于处理短时间大量提示，避免新消息直接挤掉用户还没看完的 Toast。
    QQueue<PendingToast> m_queue;
    Position m_position = Position::BottomRight;
    int m_maxVisible = 5;
    int m_maxQueued = 20;
    int m_defaultDurationMs = 3200;
    int m_margin = 18;
    int m_spacing = 10;
};
