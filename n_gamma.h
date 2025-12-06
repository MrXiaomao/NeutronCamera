#ifndef N_GAMMA_H
#define N_GAMMA_H
#include <QtGlobal>
#include <QVector>
#include <array>
// #include <algorithm>

#include <H5Cpp.h>
#include <QVector>
// #include <vector>
#include <iostream>
#include <cmath>

#include "ap.h"
using namespace alglib;

using namespace H5;

class n_gamma
{
public:
    n_gamma();

    // data[i][0] = Energy
    // data[i][1] = PSD
    struct HistResult {
        QVector<double> psd_x;  // bin centers
        QVector<int> count_y;  // counts per bin
    };

    struct PeaksResult {
        QVector<int> peak_id;     // 峰位置(0-based)
        QVector<float> peak_val;  // 峰高度
        int division = -1;        // floor(mean(peak_id))
        QVector<double> X1, Y1;
        QVector<double> X2, Y2;
    };

    struct FOM {
        QVector<double> X;//FOM的X轴
        QVector<double> Y, Y_fit1, Y_fit2;//散点曲线Y，两个高斯峰的拟合曲线fit1,fit2
        double R1,R2; //拟合优度
        double xlim[2]; //绘图的X轴限制区间
    };

    // 输入: x, y 等长向量（比如 data 第 3/4 列）
    // 输出: c，每个点所在网格的计数（密度）
    // 另外可选输出 colorMap（NLevel+1 x NLevel+1 格子计数）
    QVector<std::array<qint16, 512>> readWave(const std::string &fileName, const std::string &dsetName);

    QVector<QPair<float, float>> computePSD(const QVector<std::array<qint16, 512>> &wave_CH1);

    QVector<float> computeDensity(QVector<QPair<float, float>> &psdData, int NLevel = 200);

    HistResult selectAndHist(const QVector<QPair<float, float>> &data);
    FOM GetFOM(const QVector<double> &psd_x,
                                       const QVector<int> &count_y,
                                       double minPeakHeight = 1.0f,
                                       int nPeaks = 2,
                                       int minPeakDistance = 5);

    static void function_cx_1_func(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr);

    bool lsqcurvefit1(QVector<double> fit_x, QVector<double> fit_y, double* fit_c, double* r_square);

    //四舍五入取整
    inline qint16 matlab_int16(double x)
    {
        // round half away from zero
        double r = (x >= 0.0) ? std::floor(x + 0.5) : std::ceil(x - 0.5);

        // saturate
        if (r > 32767.0) return 32767;
        if (r < -32768.0) return -32768;

        return static_cast<qint16>(r);
    }
};


// 保存结果到文件的辅助函数
void saveResultsToFile(const QVector<QVector<float>>& results, const QString& filename);

#endif // N_GAMMA_H
