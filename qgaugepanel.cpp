#include "qgaugepanel.h"
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QDebug>

QGaugePanel::QGaugePanel(QWidget *parent)
    : QWidget(parent)
{
    minValue = -50;                //最小值
    maxValue = 250;                //最大值

    precision = 0;                  //精确度,小数点后几位
    scalePrecision = 0;             //刻度尺精确度,小数点后几位

    warnThold = 150;                //警告阈值
    alarmThold = 200;               //报警阈值

    trackVisible = false;           //是否显示轨迹
    scaleMajor = 6;                 //大刻度数量
    scaleMinor = 10;                 //小刻度数量
    startAngle = 60;                 //开始旋转角度270-x
    endAngle = 60;                   //结束旋转角度x-90

    animation = false;                 //是否启用动画显示
    animationStep = 1000;               //动画显示时步长

    ringWidth = 10;                  //圆环宽度
    ringColor = QColor(171,171,171,200);               //圆环颜色

    pointerStyle = false;
    scaleColor = QColor(0,255,255,200);              //刻度颜色
    pointerColor = QColor(255,0,255,200);            //指针颜色
    bgColor = QColor(51,72,91,200);                 //背景颜色
    textColor = QColor(255,255,255,255);               //文字颜色
    unit = "℃";                   //单位
    text = "仪表名称";                   //描述文字

    reverse = true;                   //是否往回走
    value = 100;            //当前值

    if(this->animation)
    {
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [=](){
            static int status = 0;
            if(status == 0) {
                this->value += 1;
                if(this->value >= maxValue && reverse)
                    status = 1;
            }else {
                this->value -= 1;
                if(this->value <= minValue && reverse)
                    status = 0;
            }

            this->update();
        });
        timer->start(animationStep);
    }

    connect(this, SIGNAL(valueChanged(double)), this, SLOT(updateValue(double)));
}

QGaugePanel::~QGaugePanel()
{

}

double QGaugePanel::getMinValue() const
{
    return this->minValue;
}

double QGaugePanel::getMaxValue() const
{
    return this->maxValue;
}

double QGaugePanel::getValue() const
{
    return this->value;
}

double QGaugePanel::getWarnThold() const
{
    return this->warnThold;
}

double QGaugePanel::getAlarmThold() const
{
    return this->alarmThold;
}

int QGaugePanel::getPrecision() const
{
    return this->precision;
}

int QGaugePanel::getScalePrecision() const
{
    return this->scalePrecision;
}

int QGaugePanel::getScaleMajor() const
{
    return this->scaleMajor;
}

int QGaugePanel::getScaleMinor() const
{
    return this->scaleMinor;
}

int QGaugePanel::getStartAngle() const
{
    return this->startAngle;
}

int QGaugePanel::getEndAngle() const
{
    return this->endAngle;
}


bool QGaugePanel::getAnimation() const
{
    return this->animation;
}

double QGaugePanel::getAnimationStep() const
{
    return this->animationStep;
}


int QGaugePanel::getRingWidth() const
{
    return this->ringWidth;
}

QColor QGaugePanel::getRingColor() const
{
    return this->ringColor;
}

bool QGaugePanel::getPointerStyle() const
{
    return this->pointerStyle;
}

QColor QGaugePanel::getScaleColor() const
{
    return this->scaleColor;
}

QColor QGaugePanel::getPointerColor() const
{
    return this->pointerColor;
}

QColor QGaugePanel::getBgColor() const
{
    return this->bgColor;
}

QColor QGaugePanel::getTextColor() const
{
    return this->textColor;
}

QString QGaugePanel::getUnit() const
{
    return this->unit;
}

QString QGaugePanel::getText() const
{
    return this->text;
}

QSize QGaugePanel::sizeHint() const
{
    int width = this->width();
    int height = this->height();
    return QSize(qMin(width, height), qMin(width, height));
}

QSize QGaugePanel::minimumSizeHint() const
{
    int width = this->width();
    int height = this->height();
    return QSize(qMin(width, height), qMin(width, height));
}

void QGaugePanel::paintEvent(QPaintEvent *)
{
    int width = this->width();
    int height = this->height();
    int side = qMin(width, height);

    //绘制准备工作,启用反锯齿,平移坐标轴中心,等比例缩放
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    //绘制背景
    // if (bgColor != Qt::transparent) {
    //     painter.setPen(Qt::NoPen);
    //     painter.fillRect(this->rect(), bgColor);
    // }

    painter.translate(width / 2, height / 2);
    painter.scale(side / 200.0, side / 200.0);

    //绘制圆环
    drawRing(&painter);

    drawOverlay(&painter);

    //绘制刻度线
    drawScale(&painter);

    //绘制刻度值
    drawScaleNum(&painter);

    //绘制指示器
    drawPointer(&painter);

    //绘制当前值
    drawValue(&painter);
}

void QGaugePanel::drawRing(QPainter *painter)
{
    painter->save();
    int radius = 100;
    QLinearGradient lg1(0, -radius, 0, radius);

    lg1.setColorAt(0, Qt::white); //设置渐变的颜色和路径比例
    lg1.setColorAt(1, Qt::gray); //只是粗略的颜色，具体的可以参考RGB颜色查询对照表

    painter->setBrush(lg1/*ringColor*//*lg1*/); // 创建QBrush对象,把这个渐变对象传递进去：
    painter->setPen(Qt::NoPen); //边框线无色
    painter->drawEllipse(-radius, -radius, radius << 1, radius << 1);
    painter->setBrush(bgColor);
    painter->drawEllipse(-92, -92, 184, 184);
    painter->restore();

    //以下角度startAngle、endAngle应该是根据值计算出来更好贴切
    {
        int radius = 70;
        int _startAngle = 270 - startAngle;                 //开始旋转角度
        int _endAngle = endAngle - 90;                   //结束旋转角度
        int angleRange = _startAngle - _endAngle;

        int warnAngle = angleRange * (warnThold - minValue) / (maxValue - minValue);
        int alarmAngle = angleRange * (alarmThold - minValue) / (maxValue - minValue);

        painter->save();
        radius = radius - ringWidth;
        // 常态
        {
            QPen pen;
            pen.setCapStyle(Qt::FlatCap);
            pen.setWidthF(5);
            pen.setColor(Qt::cyan);
            painter->setPen(pen);

            QRectF rect = QRectF(-radius, -radius, radius * 2, radius * 2);
            double angleAll = warnAngle;
            painter->drawArc(rect, _startAngle * 16, (angleAll) * (-16));//顺时针
        }

        // 警告
        {
            QPen pen;
            pen.setCapStyle(Qt::FlatCap);
            pen.setWidthF(5);
            pen.setColor(Qt::yellow);
            painter->setPen(pen);

            QRectF rect = QRectF(-radius, -radius, radius * 2, radius * 2);
            painter->drawArc(rect, (_startAngle - warnAngle) * 16, (alarmAngle - warnAngle) * (-16));//顺时针
        }

        // 报警
        {
            QPen pen;
            pen.setCapStyle(Qt::FlatCap);
            pen.setWidthF(5);
            pen.setColor(Qt::red);
            painter->setPen(pen);

            QRectF rect = QRectF(-radius, -radius, radius * 2, radius * 2);
            painter->drawArc(rect, (_startAngle - alarmAngle) * 16, (angleRange - alarmAngle) * (-16));//顺时针
        }

        painter->restore();
    }
}

void QGaugePanel::drawScale(QPainter *painter)
{
    painter->save();
    painter->rotate(startAngle);
    int steps = (scaleMajor * scaleMinor); //相乘后的值是分的份数
    double angleStep = (360.0 - startAngle - endAngle) / steps; //每一个份数的角度

    QPen pen ;
    //pen.setCapStyle(Qt::RoundCap);
    pen.setColor(scaleColor);
    for (int i = 0; i <= steps; i++)
    {
        if (i % scaleMinor == 0)//整数刻度显示加粗
        {
            pen.setWidth(1); //设置线宽
            painter->setPen(pen); //使用面向对象的思想，把画笔关联上画家。通过画家画出来

            painter->drawLine(0, 60, 0, 70); //两个参数应该是两个坐标值
        }
        else
        {
            pen.setWidth(0);
            painter->setPen(pen);
            painter->drawLine(0, 65, 0, 70);
        }
        painter->rotate(angleStep);
    }
    painter->restore();
}

void QGaugePanel::drawScaleNum(QPainter *painter)
{
    painter->save();
    painter->setPen(textColor);
    //startAngle是起始角度，endAngle是结束角度，scaleMajor在一个量程中分成的刻度数
    double startRad = ( 270-startAngle) * (3.14 / 180);
    double deltaRad = (360 - startAngle - endAngle) * (3.14 / 180) / scaleMajor;
    double sina,cosa;
    int x, y;
    QFontMetricsF fm(this->font());
    double w, h, tmpVal;
    QString str;

    for (int i = 0; i <= scaleMajor; i++)
    {
        sina = sin(startRad - i * deltaRad);
        cosa = cos(startRad - i * deltaRad);

        tmpVal = 1.0 * i *((maxValue - minValue) / scaleMajor) + minValue;
        str = QString::number((qint32)tmpVal);  //%1作为占位符   arg()函数比起 sprintf()来是类型安全的
        w = fm.size(Qt::TextSingleLine,str).width();
        h = fm.size(Qt::TextSingleLine,str).height();
        x = 82 * cosa - w / 2;
        y = -82 * sina + h / 4;
        painter->drawText(x, y, str); //函数的前两个参数是显示的坐标位置，后一个是显示的内容，是字符类型""

    }
    painter->restore();
}

void QGaugePanel::drawPointer(QPainter *painter)
{    
    int radius = 70;    
    painter->save();

    double degRotate = (360.0 - startAngle - endAngle) / (maxValue - minValue) * (value - minValue);

    if (trackVisible){
        QRectF rect(-radius, -radius, radius * 2, radius * 2);
        painter->setBrush(QColor(50, 154, 255, 50));
        painter->setPen(Qt::NoPen);
        painter->drawPie(rect, (270-startAngle) * 16, degRotate*(-16));
    }

    if (pointerStyle){
        painter->setPen(Qt::NoPen);
        painter->setBrush(pointerColor);

        QPolygon pts;
        pts.setPoints(4, -5, 0, 0, -8, 5, 0, 0, radius);

        painter->rotate(startAngle);
        //double degRotate = (360.0 - startAngle - endAngle) / (maxValue - minValue) * (value - minValue);
        painter->rotate(degRotate);
        painter->drawConvexPolygon(pts);
    } else {
        QPolygon pts;
        pts.setPoints(3, -2, 0, 2, 0, 0, radius);	/* (-2,0)/(2,0)/(0,60) *///第一个参数是 ，坐标的个数。后边的是坐标

        painter->rotate(startAngle);
        //double degRotate = (360.0 - startAngle - endAngle) / (maxValue - minValue)*(value - minValue);

        //画指针
        painter->rotate(degRotate);  //顺时针旋转坐标系统
        QRadialGradient haloGradient(0, 0, 60, 0, 0);  //辐射渐变
        haloGradient.setColorAt(0, QColor(60, 60, 60));
        haloGradient.setColorAt(1, QColor(160, 160, 160)); //灰
        painter->setPen(Qt::white); //定义线条文本颜色  设置线条的颜色
        painter->setBrush(haloGradient);//刷子定义形状如何填满 填充后的颜色
        painter->drawConvexPolygon(pts); //这是个重载函数，绘制多边形。
        painter->restore();

        //画中心点
        QColor niceBlue(150, 150, 200);
        QConicalGradient coneGradient(0, 0, -90.0);  //角度渐变
        coneGradient.setColorAt(0.0, Qt::darkGray);
        coneGradient.setColorAt(0.2, niceBlue);
        coneGradient.setColorAt(0.5, Qt::white);
        coneGradient.setColorAt(1.0, Qt::darkGray);
        painter->setPen(Qt::NoPen);  //没有线，填满没有边界
        painter->setBrush(coneGradient);
        painter->drawEllipse(-5, -5, 10, 10);
    }

    painter->restore();

    // painter->save();

}

void QGaugePanel::drawValue(QPainter *painter)
{
    QString str = QString("%1 %2").arg(value, 0, 'f', precision).arg(unit);
    QFontMetricsF fm(font());
    double w = fm.size(Qt::TextSingleLine,str).width();
    painter->setPen(textColor);
    //painter->drawText(-w / 2, 42, str);
    painter->drawText(-w / 2, 32, str);

    w = fm.size(Qt::TextSingleLine,text).width();
    //painter->drawText(-w / 2, -30, str);
    painter->drawText(-w / 2, 60, text);
}

void QGaugePanel::drawOverlay(QPainter *painter)
{
    int radius = 80;
    painter->save();
    painter->setPen(Qt::NoPen);

    QPainterPath smallCircle;
    QPainterPath bigCircle;
    radius -= 1;
    smallCircle.addEllipse(-radius, -radius, radius * 2, radius * 2);
    radius *= 2;
    bigCircle.addEllipse(-radius, -radius + 140, radius * 2, radius * 2);

    //高光的形状为小圆扣掉大圆的部分
    QPainterPath highlight = smallCircle & bigCircle;//smallCircle - bigCircle;
    QColor overlayColor;
    QLinearGradient linearGradient(0, -radius / 2, 0, 0);
    overlayColor.setAlpha(50);
    linearGradient.setColorAt(0.0, overlayColor);
    overlayColor.setAlpha(30);
    linearGradient.setColorAt(1.0, overlayColor);
    painter->setBrush(linearGradient);
    painter->rotate(-20);
    painter->drawPath(highlight);

    painter->restore();
}

//设置范围值
void QGaugePanel::setRange(int minValue, int maxValue)
{
    this->setMinValue(minValue);
    this->setMaxValue(maxValue);

    this->setWarnThold((double)maxValue * 0.6);
    this->setAlarmThold((double)maxValue * 0.8);
}

void QGaugePanel::setRange(double minValue, double maxValue)
{
    this->setMinValue(minValue);
    this->setMaxValue(maxValue);

    this->setWarnThold(maxValue * 0.6);
    this->setAlarmThold(maxValue * 0.8);
}

//设置最大最小值
void QGaugePanel::setMaxValue(double maxValue)
{
    this->maxValue = maxValue;
}

void QGaugePanel::setMinValue(double minValue)
{
    this->minValue = minValue;
}

void QGaugePanel::setValue(double value)
{
    updateValue(value);
}

void QGaugePanel::setValue(int value)
{
    if (this->value != value){
        this->value = value;
        this->update();
    }
}

void QGaugePanel::setWarnThold(double warnThold)
{
    this->warnThold = warnThold;
}

void QGaugePanel::setAlarmThold(double alarmThold)
{
    this->alarmThold = alarmThold;
}

//设置精确度
void QGaugePanel::setPrecision(int precision)
{
    this->precision = precision;
}

//设置刻度尺精确度
void QGaugePanel::setScalePrecision(int scalePrecision)
{
    this->scalePrecision = scalePrecision;
}

//设置主刻度数量
void QGaugePanel::setScaleMajor(int scaleMajor)
{
    this->scaleMajor = scaleMajor;
}

//设置小刻度数量
void QGaugePanel::setScaleMinor(int scaleMinor)
{
    this->scaleMinor = scaleMinor;
}

//设置刻度颜色
void QGaugePanel::setScaleColor(const QColor &scaleColor)
{
    this->scaleColor = scaleColor;
}

//设置指针颜色
void QGaugePanel::setPointerColor(const QColor &pointerColor)
{
    this->pointerColor = pointerColor;
}

//设置指针样式
void QGaugePanel::setPointerStyle(bool pointerStyle)
{
    this->pointerStyle = pointerStyle;
}

//设置开始旋转角度
void QGaugePanel::setStartAngle(int startAngle)
{
    this->startAngle = startAngle;
}

//设置结束旋转角度
void QGaugePanel::setEndAngle(int endAngle)
{
    this->endAngle = endAngle;
}

//设置是否启用动画显示
void QGaugePanel::setAnimation(bool animation)
{    
    if (this->animation != animation){
        this->animation = animation;
        if (this->animation)
            timer->start(this->animationStep);
        else
            timer->stop();
    }
}

//设置动画显示的步长
void QGaugePanel::setAnimationStep(double animationStep)
{
    this->animationStep = animationStep;
    if (this->animation){
        timer->stop();
        timer->start(this->animationStep);
    }
}

//设置圆环宽度+颜色
void QGaugePanel::setRingWidth(int ringWidth)
{
    this->ringWidth = ringWidth;
}

void QGaugePanel::setRingColor(const QColor &ringColor)
{
    this->ringColor = ringColor;
}

//设置背景颜色
void QGaugePanel::setBgColor(const QColor &bgColor)
{
    this->bgColor = bgColor;
}

//设置文本颜色
void QGaugePanel::setTextColor(const QColor &textColor)
{
    this->textColor = textColor;
}

//设置单位
void QGaugePanel::setUnit(const QString &unit)
{
    this->unit = unit;
}

//设置中间描述文字
void QGaugePanel::setText(const QString &text)
{
    this->text = text;
}

void QGaugePanel::updateValue(double value)
{
    if(value > maxValue || value < minValue){
        return;
    }

    this->value = value;
    this->update();
}
