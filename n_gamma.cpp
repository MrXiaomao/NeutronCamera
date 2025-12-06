#include "n_gamma.h"
#include <H5Cpp.h>

#include <QVector>
#include <QList>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <QFile>
// #include <numeric>

#include "alglibinternal.h"
// #include "ap.h"
#include "interpolation.h"
#include "linalg.h"
#include "optimization.h"
#include <math.h>
// using namespace alglib;

const double diffstep = 1.4901e-08;
const float EPSILON = 1e-6f;  // 根据实际精度需求调整
using namespace H5;

n_gamma::n_gamma() {}

// 读取波形数据，这里是有效波形
// 读取 wave_CH1.h5 中的数据集 data（行 = 脉冲数, 列 = 采样点数）
// 返回 QVector<std::array<qint16, 512>>，尺寸为 [numPulses x 512] = [35679 x 512]
QVector<std::array<qint16, 512>> n_gamma::readWave(const std::string &fileName,
                                              const std::string &dsetName)
{
    QVector<std::array<qint16, 512>> wave_CH1;

    try {
        H5File file(fileName, H5F_ACC_RDONLY);
        DataSet dataset = file.openDataSet(dsetName);
        DataSpace dataspace = dataset.getSpace();

        const int RANK = dataspace.getSimpleExtentNdims();
        if (RANK != 2) {
            std::cerr << "Error: dataset rank != 2" << std::endl;
            return {};
        }

        hsize_t dims[2];
        dataspace.getSimpleExtentDims(dims, nullptr);
        // 约定：dims[0] = 脉冲数（35679），dims[1] = 采样点数（512）
        hsize_t numPulses  = dims[0];
        hsize_t numSamples = dims[1];

        if (numSamples != 512) {
            std::cerr << "Error: numSamples != 512, got " << numSamples << std::endl;
            return {};
        }

        std::cout << "HDF5 dataset shape: " << numPulses << " x " << numSamples << std::endl;

        // 先读到一维 buffer（int16）
        std::vector<short> buffer(numPulses * numSamples);
        dataset.read(buffer.data(), PredType::NATIVE_SHORT);

        // 填到 QVector<std::array<qint16, 512>>：wave_CH1[pulse][sample]
        wave_CH1.resize(static_cast<int>(numPulses));
        for (int p = 0; p < static_cast<int>(numPulses); ++p) {
            for (int s = 0; s < static_cast<int>(numSamples); ++s) {
                short v = buffer[p * numSamples + s];      // 行主序：第 p 行第 s 列
                wave_CH1[p][s] = v;
            }
        }
    }
    catch (const FileIException &e) {
        e.printErrorStack();
        return {};
    }
    catch (const DataSetIException &e) {
        e.printErrorStack();
        return {};
    }
    catch (const DataSpaceIException &e) {
        e.printErrorStack();
        return {};
    }

    return wave_CH1;
}

/**
 * @brief n_gamma::computePSD 筛选符合要求的波形，并且计算波形PSD值
 * @param wave_CH1 输入的波形, wave_CH1[pulseIndex][sampleIndex]：numPulses x 512
 * @return data[i] = (标定后能量, PSD 值)，每个元素是一个 QPair<float, float>，first 是能量，second 是 PSD 值
 */
QVector<QPair<float, float>> n_gamma::computePSD(const QVector<std::array<qint16, 512>> &wave_CH1)
{
    if (wave_CH1.isEmpty())
        return {};

    // const int numPulses  = wave_CH1.size();       // ~35679
    const int numSamples = 512;    // 固定为 512

    using Pulse = std::array<qint16, 512>;
    // 后续要修改波形（baseline 扣除、筛选），拷贝一份
    QVector<Pulse> pulses = wave_CH1;

    // ---------- 参数 ----------
    const int Peak_position_low  = 20;   // 1-based
    const int Peak_position_up   = 50;   // 1-based
    const int PSD_N_PAR          = 15;
    const int PSD_L1_PAR         = 30;
    const int PSD_L2_PAR         = 100;
    const float ratio            = 0.3f; // 恒比定时比值 k

    // ---------- 剔除不满足峰位要求的波形 ----------
    {
        QVector<Pulse> filtered;
        filtered.reserve(pulses.size());

        for (int idx = 0; idx < pulses.size(); ++idx) {
            const Pulse &p = pulses[idx];

            float peak = p[0];
            int peakIndex0 = 0; // 0-based
            for (int s = 1; s < numSamples; ++s) {
                if (p[s] > peak) {
                    peak = p[s];
                    peakIndex0 = s;
                }
            }

            int peakIndex1 = peakIndex0 + 1; // 1-based
            if (peakIndex1 > Peak_position_low && peakIndex1 < Peak_position_up) {
                filtered.push_back(p);
            }
        }
        pulses.swap(filtered);
    }

    if (pulses.isEmpty())
        return {};

    // ---------- 扣除畸形波形：峰值 <= 100 的剔除 ----------
    {
        QVector<Pulse> filtered;
        filtered.reserve(pulses.size());

        for (const Pulse &p : pulses) {
            float peak = p[0];
            for (int s = 1; s < numSamples; ++s) {
                if (p[s] > peak)
                    peak = p[s];
            }
            if (peak > 100.0f) { // del_max <= 100 剔除
                filtered.push_back(p);
            }
        }
        pulses.swap(filtered);
    }

    if (pulses.isEmpty())
        return {};

    // ---------- baseline 计算并扣除 (前 16 点平均) ----------
    {
        const int baseSamples = 16;
        for (Pulse &p : pulses) {
            float sum = 0.0f;
            for (int s = 0; s < baseSamples; ++s) {
                sum += p[s];
            }
            qint16 baseline = matlab_int16(sum / baseSamples);

            for (int s = 0; s < numSamples; ++s) {
                //这里可能有溢出风险
                p[s] = p[s] - baseline;
            }
        }
    }

    // ---------- 再剔除一遍：最小值 <= -70 的脉冲 ----------
    {
        // QVector<int> del1;
        QVector<Pulse> filtered;
        filtered.reserve(pulses.size());
        int lines =1;
        for (const Pulse &p : pulses) {
            float minVal = p[0];
            for (int s = 1; s < numSamples; ++s) {
                if (p[s] < minVal)
                    minVal = p[s];
            }
            if (minVal > -70) {  // del_min <= -70 剔除
                filtered.push_back(p);
            }
            // else{
            //     del1.push_back(lines);
            // }
            lines++;
        }
        pulses.swap(filtered);
    }

    if (pulses.isEmpty())
        return {};

    // ---------- 波形处理与 PSD 计算 ----------
    const int L = pulses.size();                       // 剩余有效脉冲数
    QVector<QPair<float, float>> results1;
    results1.reserve(L);  // 预分配空间
    int validPulseNum = 0;

    for (int i1 = 0; i1 < L; ++i1) {
        const Pulse &pulse = pulses[i1];

        // 1) 峰值和峰位
        float peak = pulse[0];
        int peakIndex0 = 0;
        for (int s = 1; s < numSamples; ++s) {
            if (pulse[s] > peak) {
                peak = pulse[s];
                peakIndex0 = s;
            }
        }

        // 2) 找恒比阈值 crossing 点 Th_id
        int Th_id0 = -1;  // 0-based
        const float thr = ratio * peak;
        for (int s = 0; s <= peakIndex0 && s + 1 < numSamples; ++s) {
            if (pulse[s] <= thr && pulse[s + 1] > thr) {
                Th_id0 = s;
                break;
            }
        }

        if (Th_id0 < 0) {
            // 没找到 crossing，跳过
            continue;
        }

        float Energy = peak; // 峰值法能量

        // 3) 积分区间
        int startLong  = Th_id0 + PSD_N_PAR;
        int endLong    = Th_id0 + PSD_L2_PAR;
        int startShort = Th_id0 + PSD_N_PAR + PSD_L1_PAR;

        // 边界保护
        if (startLong >= numSamples || startShort >= numSamples)
            continue;
        if (endLong >= numSamples)
            endLong = numSamples - 1;

        qint32 PSD_Long  = 0;
        qint32 PSD_Short = 0;

        for (int s = startLong; s <= endLong; ++s) {
            PSD_Long += static_cast<qint32>(pulse[s]);
        }
        for (int s = startShort; s <= endLong; ++s) {
            PSD_Short += static_cast<qint32>(pulse[s]);
        }

        if (PSD_Long == 0)
            continue;

        float psdRatio = static_cast<float>(PSD_Short) / static_cast<float>(PSD_Long);

        if (Energy > 0.0f && psdRatio < 1.0f && psdRatio > 0.0f) {
            // 这里的位置用"在原 pulses 里的序号"，你也可以额外保留原始 index
            // 直接添加 QPair<float, float>，first 是能量（未标定），second 是 PSD
            results1.append(qMakePair(Energy, psdRatio));
            ++validPulseNum;
        }
    }

    // 去掉无效行（原 MATLAB 是 data(:,1)==0），这里不需要 resize，因为已经只添加了有效的
    if (results1.isEmpty())
        return {};

    // ---------- 按能量（first）排序 ----------
    std::sort(results1.begin(), results1.end(),
              [](const QPair<float, float> &a, const QPair<float, float> &b) {
                  return a.first < b.first;   // 按能量排序
              });

    // ---------- 能量标定：E = E*0.5587 + 34.465 ----------
    for (int i = 0; i < results1.size(); ++i) {
        results1[i].first = results1[i].first * 0.5587f + 34.465f;
    }

    return results1;  // 对应 MATLAB 的 data
}

/**
 * @brief n_gamma::computeDensity
 * @param psdData PSD数据，每个元素是一个 QPair<float, float>，first 是 Energy，second 是 PSD
 * @param NLevel:NLevel:密度网格的绘制，网格边长个数
 * @return 各个psd对应的密度值
 */
QVector<float> n_gamma::computeDensity(QVector<QPair<float, float>> &psdData, int NLevel /*=200*/)
{
    // DensityResult res;
    QVector<float> den;

    const int M = psdData.size();
    if (M == 0 || NLevel < 2) return den;

    // ---- 取 Energy / PSD 的 min/max ----
    float max_x = psdData[0].first, min_x = psdData[0].first;
    float max_y = psdData[0].second, min_y = psdData[0].second;

    for (int i = 1; i < M; ++i) {
        float x = psdData[i].first;
        float y = psdData[i].second;
        if (x > max_x) max_x = x;
        if (x < min_x) min_x = x;
        if (y > max_y) max_y = y;
        if (y < min_y) min_y = y;
    }

    // ---- 步长（对应 MATLAB step_x/step_y）----
    const float step_x = (max_x - min_x) / float(NLevel - 1);
    const float step_y = (max_y - min_y) / float(NLevel - 1);

    // 防止除0（全相等）
    if (std::fabs(step_x) < EPSILON || std::fabs(step_y) < EPSILON ) {
        return den;
    }

    // ---- colorMap 初始化 ----
    const int gridSize = NLevel + 1;
    QVector<QVector<int>> colorMap;  // (NLevel+1) x (NLevel+1)
    colorMap.resize(gridSize);
    for (int i = 0; i < gridSize; ++i) {
        colorMap[i].fill(0, gridSize);
    }

    // ---- 第一遍：统计每个格子里的点数 ----
    for (int i = 0; i < M; ++i) {
        float x = psdData[i].first;
        float y = psdData[i].second;

        int gx = static_cast<int>(std::round((x - min_x) / step_x));
        int gy = static_cast<int>(std::round((y - min_y) / step_y));

        // 转成 0-based 并夹紧，避免越界
        gx = std::clamp(gx, 0, gridSize);
        gy = std::clamp(gy, 0, gridSize);

        colorMap[gx][gy] += 1;
    }

    // ---- 第二遍：给每个点赋密度 c(i) ----
    den.resize(M);
    for (int i = 0; i < M; ++i) {
        float x = psdData[i].first;
        float y = psdData[i].second;

        int gx = static_cast<int>(std::round(((x - min_x) / step_x)));
        int gy = static_cast<int>(std::round(((y - min_y) / step_y)));

        gx = std::clamp(gx, 0, gridSize);
        gy = std::clamp(gy, 0, gridSize);

        den[i] = static_cast<float>(colorMap[gx][gy]);
    }

    return den;
}

n_gamma::HistResult n_gamma::selectAndHist(const QVector<QPair<float, float>> &data)
{
    // QVector<float> out;
    QVector<float> psd_Na;
    HistResult out;

    // ---------- 1) 能量框选：700 < E < 900 ----------
    psd_Na.reserve(data.size());
    for (const auto &pair : data) {
        float E = pair.first;
        if (E > 600.0f && E < 1000.0f) {
            psd_Na.push_back(pair.second);
        }
    }

    // ---------- 2) psd_x = 0:0.002:1 ----------
    const double start = 0.0f;
    const double step  = 0.002f;
    const double end   = 1.0f;

    int numBins = int(std::round((end - start) / step)) + 1; // 501 bins
    out.psd_x.resize(numBins);
    for (int i = 0; i < numBins; ++i) {
        out.psd_x[i] = start + i * step;
    }

    out.count_y.fill(0, numBins);

    if (psd_Na.isEmpty())
        return out;

    // ---------- 3) MATLAB hist(x, centers) 的等价分箱 ----------
    // centers -> edges = centers +/- step/2
    // 第一个边界为 centers[0] - step/2
    // 最后一个边界为 centers[last] + step/2
    // const float half = step * 0.5f;

    // 统计每个PSD落在哪个中心对应的区间
    for (const auto &psd : psd_Na) {
        // 计算 idx = round((psd - start)/step)
        // 等价于“找最近中心”的 hist 行为
        int idx = int(std::floor((psd - start) / step + 0.5f));

        if (idx >= 0 && idx < numBins) {
            out.count_y[idx] += 1;
        }
        // 落在范围外的 MATLAB hist 会自动忽略
    }

    return out;
}

// 等价 MATLAB:
// [peak, peak_id]= findpeaks(Y,'minpeakheight',1,'npeaks',2,'minpeakdistance',5);
// division = floor(mean(peak_id));
// X1=X(1:division); ... (MATLAB 1-based)
/**
 * @brief 获取FOM曲线及其中子-伽马拟合曲线
 * @param psd_x psd的Bin，X轴
 * @param count_y 各个bin区间对应的计数
 * @param minPeakHeight 最小峰位间隔
 * @param nPeaks 寻峰数目
 * @param minPeakDistance 最小峰位间距
 * @return FOM 散点曲线，中子部分拟合曲线，拟合优度，伽马部分拟合曲线，拟合优度,FOM图像的X轴范围
 */
n_gamma::FOM n_gamma::GetFOM(const QVector<double> &psd_x,
                              const QVector<int> &count_y,
                              double minPeakHeight/* = 1.0f*/,
                              int nPeaks/* = 2*/,
                              int minPeakDistance/* = 5*/)
{

    n_gamma::FOM fom;
    n_gamma::PeaksResult out;

    const int N = psd_x.size();
    if (N == 0 || count_y.size() != N) return fom;

    // 获取count_y的最大值
    float max_y = 0.0f;
    float sum_y = 0.0f;
    for (int i = 0; i < N; ++i) {
        if (count_y[i] > max_y) max_y = count_y[i];
        sum_y += count_y[i];
    }
    
    //计数太少，不做拟合。直接返回
    if(max_y <20 || sum_y < 100)
    {
        for(int i=0; i<psd_x.size(); i++)
        {
            fom.Y.push_back(count_y.at(i)*1.0);
            fom.Y_fit1.push_back(0.0);
            fom.Y_fit2.push_back(0.0);
        }
        fom.X.append(psd_x);
        fom.xlim[0] = 0.0;
        fom.xlim[1] = 1.0;
        fom.R1 = 0.0;
        fom.R2 = 0.0;
        return fom;
    }
    
    // 把 count_y 转成 float 方便处理
    QVector<float> Y(N);
    for (int i = 0; i < N; ++i) Y[i] = static_cast<float>(count_y[i]);

    // -------- 1) 找所有局部峰（不含两端）--------
    struct Cand { int idx; float val; };
    QVector<Cand> candidates;
    candidates.reserve(N);

    for (int i = 1; i < N - 1; ++i) {
        float v = Y[i];
        if (v >= minPeakHeight &&
            v > Y[i - 1] &&
            v >= Y[i + 1])   // MATLAB findpeaks 的典型局部极大判据
        {
            candidates.push_back({i, v});
        }
    }

    if (candidates.isEmpty()) {
        // 没峰就直接返回空结构
        return fom;
    }

    // -------- 2) 按峰高降序排序 --------
    std::sort(candidates.begin(), candidates.end(),
              [](const Cand &a, const Cand &b){ return a.val > b.val; });

    // -------- 3) 按 minPeakDistance 选前 nPeaks 个峰 --------
    QVector<Cand> picked;
    picked.reserve(nPeaks);

    for (const auto &c : candidates) {
        bool tooClose = false;
        for (const auto &p : picked) {
            if (std::abs(c.idx - p.idx) < minPeakDistance) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) {
            picked.push_back(c);
            if (picked.size() == nPeaks) break;
        }
    }

    if (picked.isEmpty())
        return fom;

    // MATLAB 返回 peak_id 按位置升序更常见，这里也排一下
    std::sort(picked.begin(), picked.end(),
              [](const Cand &a, const Cand &b){ return a.idx < b.idx; });

    out.peak_id.resize(picked.size());
    out.peak_val.resize(picked.size());
    for (int k = 0; k < picked.size(); ++k) {
        out.peak_id[k]  = picked[k].idx;   // 0-based
        out.peak_val[k] = picked[k].val;
    }

    // -------- 4) division = floor(mean(peak_id)) --------
    double sumIdx = 0.0;
    for (int idx : out.peak_id) sumIdx += idx;
    out.division = static_cast<int>(std::floor(sumIdx / out.peak_id.size()));

    // 边界保护
    if (out.division < 0) out.division = 0;
    if (out.division >= N) out.division = N - 1;

    // -------- 5) 切两段数据 --------
    // MATLAB:
    // X1 = X(1:division);   (1-based, inclusive)
    // X2 = X(division+1:end)
    //
    // C++ 0-based:
    // 前半段 [0 .. division]
    // 后半段 [division+1 .. N-1]
    int div = out.division;

    out.X1.reserve(div + 1);
    out.Y1.reserve(div + 1);
    for (int i = 0; i <= div; ++i) {
        out.X1.push_back(psd_x[i]*1.0);
        out.Y1.push_back(Y[i]*1.0);
    }

    out.X2.reserve(N - div - 1);
    out.Y2.reserve(N - div - 1);
    for (int i = div + 1; i < N; ++i) {
        out.X2.push_back(psd_x[i]);
        out.Y2.push_back(Y[i]);
    }

    //拟合初值
    double p1[3] = {out.peak_val.at(0), psd_x.at(out.peak_id.at(0)), 0.001}; //这里峰位是准确给出的
    double p2[3] = {out.peak_val.at(1), psd_x.at(out.peak_id.at(1)), 0.001}; //这里峰位是准确给出的
    double R1_square = 0.0;
    double R2_square = 0.0;

    //拟合并给出结果
    lsqcurvefit1(out.X1, out.Y1, p1, &R1_square);
    lsqcurvefit1(out.X2, out.Y2, p2, &R2_square);

    //计算拟合曲线y值
    alglib::real_1d_array c1,c2;
    c1.setcontent(3, p1);
    c2.setcontent(3, p2);
    for(int i=0; i<psd_x.size(); i++)
    {
        double x_temp = psd_x.at(i);
        alglib::real_1d_array xData;
        xData.setlength(1);
        xData[0] = x_temp;
        double y1,y2;
        function_cx_1_func(c1, xData, y1, NULL);
        function_cx_1_func(c2, xData, y2, NULL);
        fom.Y.push_back(count_y.at(i)*1.0);
        fom.Y_fit1.push_back(y1);
        fom.Y_fit2.push_back(y2);
    }
    fom.X.append(psd_x);
    fom.xlim[0] = p1[1]-8*p1[2]/1.414;
    fom.xlim[1] = p2[1]+8*p2[2]/1.414;
    fom.R1 = R1_square;
    fom.R2 = R2_square;

    return fom;
}

/**
 * @brief function_cx_1_func 定义待拟合函数
 * @param c
 * @param x
 * @param func
 * @param ptr 用于自定义传参
 */
void n_gamma::function_cx_1_func(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
{
    func = c[0]*exp(-pow((x[0]-c[1])/c[2],2));
}

/**
 * @brief lsqcurvefit1 高斯拟合
 * func = c[0]*exp(-pow((x-c[1])/c[2],2));
 * @param fit_x //待拟合一维数组x
 * @param fit_y //待拟合一维数组y
 * @param fit_c //拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
 * @param peak  //拟合函数中的高斯部分中心道址
 * @return
 */
bool n_gamma::lsqcurvefit1(QVector<double> fit_x, QVector<double> fit_y, double* fit_c, double* r_square)
{
    // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
    int num = fit_x.size();
    try
    {
        //QVector容器转real_2d_array
        alglib::real_2d_array x;
        x.setcontent(num, 1, fit_x.constData());

        // QVector容器转real_1d_array
        alglib::real_1d_array y;
        y.setcontent(fit_y.size(), fit_y.constData());

        alglib::real_1d_array c;
        c.setcontent(3, fit_c);

        double epsx = 0.000001;
        ae_int_t maxits = 10000;
        lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
        lsfitreport rep;

        //
        // Fitting without weights
        //
        lsfitcreatef(x, y, c, diffstep, state);
        alglib::lsfitsetcond(state, epsx, maxits);
        alglib::lsfitfit(state, function_cx_1_func);
        lsfitresults(state, c, rep); //参数存储到state中

        //取出拟合参数c
        for(int i=0;i<3;i++){
            fit_c[i] = c[i];
        }

        // 决定系数 R² R² = 1 - SSE/SST
        // 其中：
        // SSE = 残差平方和 (Sum of Squares Error)
        // SST = 总平方和 (Total Sum of Squares)
        // R² = 1 - sum((y_fit - y_data).^2) / sum((y_data - mean(y_data)).^2);
        double meany = 0.0;
        for(int i=0; i<num; i++)
        {
            meany += fit_y.at(i);
        }
        meany /= num;

        double sum1=0.0, sum2=0.0;
        for(int i=0; i<num; i++)
        {
            double x_temp = fit_x.at(i);
            alglib::real_1d_array xData;
            xData.setlength(1);
            xData[0] = x_temp;
            double y;
            function_cx_1_func(c, xData, y, NULL);

            double residual = fit_y.at(i) - y; //残差
            sum1 += pow(residual, 2);
            sum2 += pow((fit_y.at(i) - meany), 2);
        }
        *r_square = 1.0 - sum1/sum2;

        printf("lsqcurvefit1 c:%s, r_square=%.1f\n", c.tostring(1).c_str(), *r_square);
        qDebug()<<"lsqcurvefit1 c:"<<c.tostring(1).c_str()<<", iterationscount="<<rep.iterationscount
                 <<", r2="<<rep.r2
                 <<", terminationtype="<<rep.terminationtype
                 <<"r_square="<<*r_square;
    }
    catch(alglib::ap_error alglib_exception)
    {
        printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
        return 0;
    }
    return 1;
}

// 保存结果到文件的辅助函数
void saveResultsToFile(const QVector<QVector<float>>& results, const QString& filename) {
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "Index\tPeak\tEnergy\tPSD_Ratio\n";

        for (const auto& row : results) {
            out << QString("%1\t%2\t%3\t%4\n")
            .arg(row[0])
                .arg(row[1], 0, 'f', 2)
                .arg(row[2], 0, 'f', 2)
                .arg(row[3], 0, 'f', 4);
        }

        file.close();
        qDebug() << "结果已保存到：" << filename;
    }
}
