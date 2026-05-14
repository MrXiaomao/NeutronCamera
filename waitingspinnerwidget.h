/* Original Work Copyright (c) 2012-2014 Alexander Turkin
   Modified 2014 by William Hallatt
   Modified 2015 by Jacob Dawid
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

// Qt includes
#include <QWidget>
#include <QTimer>
#include <QColor>

class WaitingSpinnerWidget : public QWidget {
    Q_OBJECT
public:
    /*! Constructor for "standard" widget behaviour - use this
   * constructor if you wish to, e.g. embed your widget in another. */
    WaitingSpinnerWidget(QWidget *parent = 0,
                         bool centerOnParent = true,
                         bool disableParentWhenSpinning = true);

    /*! Constructor - use this constructor to automatically create a modal
   * ("blocking") spinner on top of the calling widget/window.  If a valid
   * parent widget is provided, "centreOnParent" will ensure that
   * QtWaitingSpinner automatically centres itself on it, if not,
   * "centreOnParent" is ignored. */
    WaitingSpinnerWidget(Qt::WindowModality modality,
                         QWidget *parent = 0,
                         bool centerOnParent = true,
                         bool disableParentWhenSpinning = true);

public slots:
    void start();
    void stop();

public:
    void setColor(QColor color);
    void setRoundness(qreal roundness);
    void setMinimumTrailOpacity(qreal minimumTrailOpacity);
    void setTrailFadePercentage(qreal trail);
    void setRevolutionsPerSecond(qreal revolutionsPerSecond);
    void setNumberOfLines(int lines);
    void setLineLength(int length);
    void setLineWidth(int width);
    void setInnerRadius(int radius);
    void setText(const QString& text);
    void setTextColor(const QColor& color); // 设置文字颜色

    QColor color();
    qreal roundness();
    qreal minimumTrailOpacity();
    qreal trailFadePercentage();
    qreal revolutionsPersSecond();
    int numberOfLines();
    int lineLength();
    int lineWidth();
    int innerRadius();

    bool isSpinning() const;

private slots:
    void rotate();

protected:
    virtual void paintEvent(QPaintEvent *paintEvent);

private:
    static int lineCountDistanceFromPrimary(int current, int primary,
                                            int totalNrOfLines);
    static QColor currentLineColor(int distance, int totalNrOfLines,
                                   qreal trailFadePerc, qreal minOpacity,
                                   QColor color);

    void initialize();
    void updateSize();
    void updateTimer();
    void updatePosition();

private:
    QColor  _color;// 设置颜色
    qreal   _roundness; // 设置线条圆润度，范围 0 至 100
    qreal   _minimumTrailOpacity;// 设置尾部最淡处的不透明度百分比 (%)
    qreal   _trailFadePercentage;// 设置渐隐区域占整体的百分比 (%)
    qreal   _revolutionsPerSecond;// 旋转速度：每秒转多少圈
    int     _numberOfLines;// 绘制半径线条个数（条）
    int     _lineLength;// 每条线条的长度（像素）
    int     _lineWidth;// 每条线条的宽度（像素）
    int     _innerRadius; // 内圆半径（控制“死区”大小）
    QString _text; // 存储提示文字
    QColor _textColor; // 存储文字颜色

private:
    WaitingSpinnerWidget(const WaitingSpinnerWidget&);
    WaitingSpinnerWidget& operator=(const WaitingSpinnerWidget&);

    QTimer *_timer;
    bool    _centerOnParent;// 是否在父窗口中居中显示
    bool    _disableParentWhenSpinning;// 旋中是否屏蔽父窗口
    int     _currentCounter;// 旋转计数
    bool    _isSpinning;// 是否正在旋转
};
