/* hda_client.h
 * HDA DLL C API - v1.0 (简化版)
 * - 无认证（无token/TLS）
 * - 最小客户端配置（仅server_address）
 * - 1D时基使用显式时间数组（double[]）
 * - 支持通道属性
 * - Writer仅支持open/write/close（无flush/abort，无hash/file_id）
 * - API命名：hda_client_connect/hda_client_disconnect，所有API使用hda_前缀
 */

#pragma once

#include <stdint.h>
// #include "version.h"

#ifdef _WIN32
  #ifndef HDA_CALL
    #define HDA_CALL __stdcall
  #endif
  #ifdef HDA_BUILD_DLL
    #define HDA_API __declspec(dllexport)
  #else
    #define HDA_API __declspec(dllimport)
  #endif
#else
  #ifndef HDA_CALL
    #define HDA_CALL
  #endif
  #ifndef HDA_API
    #define HDA_API
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================
 *  不透明句柄
 * ========================= */
typedef void* hda_client_t;
typedef void* hda_writer_t;

/* =========================
 *  状态码
 * ========================= */
typedef enum hda_status_t {
  HDA_OK = 0,
  HDA_E_INVALID_ARG = 1,
  HDA_E_NOT_INITIALIZED = 2,
  HDA_E_NETWORK = 4,
  HDA_E_TIMEOUT = 5,
  HDA_E_BACKPRESSURE = 6,
  HDA_E_NOT_FOUND = 7,
  HDA_E_IO = 8,

  /* 认证相关错误码 */
  HDA_E_UNAUTHENTICATED = 9,     /* 未认证或 Token 无效 */
  HDA_E_AUTH_FAILED = 10,        /* 认证失败 */

  HDA_E_INTERNAL = 100
} hda_status_t;

/* =========================
 *  数据类型（单一枚举）
 *  注意：用于实际值存储类型和attrs.DATA_TYPE
 * ========================= */
typedef enum hda_dtype_t {
  HDA_DT_FLOAT32   = 0,  /* 32位浮点数 */
  HDA_DT_STRING    = 1,  /* UTF-8字符串（变长） */
  HDA_DT_INTEGER32 = 2,  /* 32位有符号整数 */
  HDA_DT_RAW_DIO   = 3,  /* 数字I/O（1字节） */
  HDA_DT_RAW_U16   = 4,  /* 16位无符号整数 */
  HDA_DT_RAW_U32   = 5,  /* 32位无符号整数 */
  HDA_DT_INTEGER16 = 6,  /* 16位有符号整数 */
  HDA_DT_FLOAT64   = 7   /* 64位浮点数 */
} hda_dtype_t;

/* =========================
 *  时间模式枚举
 * ========================= */
typedef enum hda_time_mode_t {
  HDA_TIME_MODE_FULL_ARRAY = 0,  /* 全时间数组模式 */
  HDA_TIME_MODE_START_FREQ = 1   /* 起始时间+频率模式 */
} hda_time_mode_t;

/* =========================
 *  读取模式枚举
 * ========================= */
typedef enum hda_read_mode_t {
  HDA_READ_MODE_CONTINUOUS = 0,  /* 连续读取 */
  HDA_READ_MODE_UNIFORM_SAMPLING = 1  /* 均匀采样模式：指定返回数量，服务端按间隔抽取 */
} hda_read_mode_t;

/* =========================
 *  客户端配置
 * ========================= */
typedef struct hda_client_config_t {
  const char* server_address;     /* 必需：服务器地址，如 "10.0.0.11:50055" */

  /* 可选：认证凭证 */
  const char* username;           /* 用户名，NULL = 不认证 */
  const char* password;           /* 密码，NULL = 不认证 */
} hda_client_config_t;

/* =========================
 *  写入键
 * ========================= */
typedef struct hda_write_key_t {
  const char* device;             /* 例如 "HL-3" */
  int64_t shot;                   /* 炮号 */
  const char* subsystem;          /* 子系统名称 */
  /*  const char* schema_version;     例如 "v1" */
} hda_write_key_t;

/* =========================
 *  属性字符串最大长度（用于读取响应）
 * ========================= */
#define HDA_MAX_DEVICE_LEN        64
#define HDA_MAX_SUBSYSTEM_LEN     64
#define HDA_MAX_CONTACT_INFO_LEN  128
#define HDA_MAX_DAQ_DTYPE_LEN     32
#define HDA_MAX_DESCRIPTION_LEN   256
#define HDA_MAX_ENTRY_TIME_LEN    32
#define HDA_MAX_T_TYPE_LEN        8
#define HDA_MAX_V_UNIT_LEN        32

/* =========================
 *  通道属性（用于写入）
 *  注意：字符串字段为指针（用户提供的字符串）。
 *        NULL表示"未设置/不更新"。
 *        数值字段使用-1表示"未设置"（约定）。
 * ========================= */
typedef struct hda_channel_attr_t {
  const char* DEVICE;
  const char* SUBSYSTEM;
  const char* CONTACT_INFO;
  const char* DAQ_DTYPE;

  int32_t     DAQ_RAW_BITS;   /* -1表示未设置 */
  int32_t     DAQ_VALIDITY;   /* -1表示未设置 */

  hda_dtype_t DATA_TYPE;      /* 数据元素类型 */

  const char* DESCRIPTION;
  int32_t     EBAR_EXIST;     /* -1表示未设置 */
  const char* ENTRY_TIME;     /* "YYYY-MM-DD HH:MM:SS" */

  int32_t     PHI_DIM;        /* -1表示未设置 */
  int32_t     R_DIM;          /* -1表示未设置 */
  float       T_COORD_FREQ;
  float       T_COORD_START;
  int64_t     T_DIM;          /* -1表示未设置 */

  const char* T_TYPE;         /* "US" / "NUS" */
  const char* T_UNIT;         /* "s" / "ms" */

  int32_t     VERSION;        /* -1表示未设置 */
  const char* V_UNIT;

  int32_t     Z_DIM;          /* -1表示未设置 */
} hda_channel_attr_t;

/* =========================
 *  通道属性响应（用于读取）
 *  注意：字符串字段为固定长度char数组（嵌入结构体）。
 *        空字符串""表示"未设置"。
 *        无内存分配，无需释放。
 *        T_UNIT是指向"s"或"ms"的静态指针（无需分配）。
 * ========================= */
typedef struct hda_channel_attr_resp_t {
  /* 字符串字段（固定长度char数组） */
  char DEVICE[HDA_MAX_DEVICE_LEN];
  char SUBSYSTEM[HDA_MAX_SUBSYSTEM_LEN];
  char CONTACT_INFO[HDA_MAX_CONTACT_INFO_LEN];
  char DAQ_DTYPE[HDA_MAX_DAQ_DTYPE_LEN];

  int32_t     DAQ_RAW_BITS;   /* -1表示未设置 */
  int32_t     DAQ_VALIDITY;   /* -1表示未设置 */

  hda_dtype_t DATA_TYPE;

  char DESCRIPTION[HDA_MAX_DESCRIPTION_LEN];
  int32_t     EBAR_EXIST;     /* -1表示未设置 */
  char ENTRY_TIME[HDA_MAX_ENTRY_TIME_LEN];

  int32_t     PHI_DIM;        /* -1表示未设置 */
  int32_t     R_DIM;          /* -1表示未设置 */
  float       T_COORD_FREQ;
  float       T_COORD_START;
  int64_t     T_DIM;          /* -1表示未设置 */

  char T_TYPE[HDA_MAX_T_TYPE_LEN];         /* "US" / "NUS" */
  const char* T_UNIT;         /* 静态指针指向 "s" 或 "ms" */

  int32_t     VERSION;        /* -1表示未设置 */
  char V_UNIT[HDA_MAX_V_UNIT_LEN];

  int32_t     Z_DIM;          /* -1表示未设置 */
} hda_channel_attr_resp_t;

/* =========================
 *  写入请求
 * ========================= */

/* 1D波形写入（显式时间数组）
 *
 * STRING类型特殊处理：
 *   - STRING是变长类型，不能直接使用pValue
 *   - 方式1（推荐）：使用ppStrings，内部自动计算总字节数
 *   - 方式2（向后兼容）：使用pValue传递null结尾的连续字符串，
 *     并设置value_bytes_len为总字节数（包含null终止符）
 *   - 示例：{"Hello", "World"} -> "Hello\0World\0"（12字节，包含2个null）
 */
typedef struct hda_write_1d_req_t {
  const char* channel;             /* 通道名称 */
  hda_dtype_t dtype;               /* 值类型 */

  int64_t count;                   /* 样本数量
                                   * STRING类型：字符串数量 */

  const double* pTime;             /* 时间数组，长度=count（共享时间时可为NULL） */

  /* 数据指针 - 使用方式取决于dtype：
   * - 定长类型（FLOAT32、INTEGER32等）：使用pValue，value_bytes_len=0自动计算
   * - STRING类型：使用ppStrings（推荐）或pValue配合value_bytes_len */
  const void*   pValue;            /* 定长类型的值数组 */
  int32_t value_bytes_len;         /* 定长类型：设为0自动计算
                                   * STRING类型配合pValue：必须指定总字节数 */

  /* STRING类型专用：字符串指针数组
   * STRING类型推荐使用 - 自动计算总字节数（包含null终止符）。
   * 示例：
   *   const char* strs[] = {"Hello", "World"};
   *   req.dtype = HDA_DT_STRING;
   *   req.count = 2;
   *   req.ppStrings = strs;
   *   req.pValue = NULL;
   *   req.value_bytes_len = 0;
   */
  const char* const* ppStrings;

  const hda_channel_attr_t* attrs; /* 可选通道属性（可为NULL） */
} hda_write_1d_req_t;

/* 0D参数写入 */
typedef struct hda_write_0d_req_t {
  const char* channel;             /* 参数名称 */
  hda_dtype_t dtype;
  int64_t count;                   /* 1或小数组 */
  const void* data;

  const hda_channel_attr_t* attrs; /* 可选属性（可为NULL） */
} hda_write_0d_req_t;

/* 2D数组写入（行优先顺序）
 *
 * 数据必须在内存中连续（行优先顺序）。
 * 支持的格式：
 *   - 栈数组：float arr[R][C]; 传递 (void*)arr
 *   - 动态连续数组：float* ptr = malloc(R*C*sizeof(float)); 传递 ptr
 *   - Python numpy：np.ascontiguousarray(arr); 传递 arr.ctypes.data
 *
 * STRING类型特殊处理：
 *   - STRING是变长类型，不能直接使用pValue
 *   - 方式1（推荐）：使用ppStrings，内部自动计算总字节数
 *   - 方式2（向后兼容）：使用pValue传递null结尾的连续字符串，
 *     并设置value_bytes_len为总字节数（包含null终止符）
 *   - 示例：{"A", "B", "C", "D"} 用于2x2数组 -> "A\0B\0C\0D\0"（8字节）
 */
typedef struct hda_write_2d_req_t {
  const char* channel;             /* 通道名称 */
  hda_dtype_t dtype;               /* 值类型 */
  int64_t rows;                    /* 行数 */
  int64_t cols;                    /* 列数 */

  /* 数据指针 - 使用方式取决于dtype：
   * - 定长类型：使用pValue指向rows*cols个连续元素
   * - STRING类型：使用ppStrings（推荐）或pValue配合value_bytes_len */
  const void* pValue;              /* 定长类型的值数组（行优先，连续） */
  int32_t value_bytes_len;         /* 定长类型：设为0自动计算
                                   * STRING类型配合pValue：必须指定总字节数 */

  /* STRING类型专用：字符串指针数组（rows*cols个字符串，行优先顺序）
   * STRING类型推荐使用 - 自动计算总字节数（包含null终止符）。
   * 示例：
   *   const char* strs[6] = {"A", "B", "C", "D", "E", "F"};  // 2x3数组
   *   req.dtype = HDA_DT_STRING;
   *   req.rows = 2;
   *   req.cols = 3;
   *   req.ppStrings = strs;
   *   req.pValue = NULL;
   *   req.value_bytes_len = 0;
   */
  const char* const* ppStrings;

  const hda_channel_attr_t* attrs; /* 可选通道属性（可为NULL） */
} hda_write_2d_req_t;

/* 子系统时间写入（用于NUS模式，每个子系统一个时间数组） */
typedef struct hda_write_subsystem_time_req_t {
  int64_t count;                   /* 时间样本数量 */
  const char* T_UNIT;              /* 字符串："s" / "ms" */
  hda_time_mode_t time_mode;       /* 时间写入模式 */

  /* 支持两种模式： */
  union {
    const double* pTime;           /* 时间数组，长度=count（全时间数组模式） */
    struct {
      double start_time;           /* 起始时间（起始时间+频率模式） */
      double frequency;            /* 采样频率（Hz）（起始时间+频率模式） */
    } time_params;                 /* 起始时间+频率模式的参数 */
  } mode;
} hda_write_subsystem_time_req_t;

/* =========================
 *  属性写入/读取
 * ========================= */
typedef struct hda_write_attribute_req_t {
  const char* channel;
  const hda_channel_attr_t* attrs;
} hda_write_attribute_req_t;

typedef struct hda_write_attribute_resp_t {
  /* 空响应，成功即可 */
} hda_write_attribute_resp_t;

typedef struct hda_read_attribute_req_t {
  int64_t shot;                    /* 炮号 */
  const char* channel;
} hda_read_attribute_req_t;

typedef struct hda_read_attribute_resp_t {
  hda_channel_attr_resp_t attrs;   /* 输出属性（固定数组，无需释放） */
} hda_read_attribute_resp_t;

/* =========================
 *  读取请求/响应
 * ========================= */

/* 统一读取请求结构体（用于1D和2D） */
typedef struct hda_read_req_t {
  int64_t shot;                    /* 炮号 */
  const char* channel;
  hda_read_mode_t mode;            /* 读取模式：CONTINUOUS / SKIP */

  /* 索引模式 */
  int64_t start_index = 0;
  int64_t count = 1000;            /* 默认1000 */

  /* 时间范围模式（与start_index互斥） */
  double start_time = 0.0;
  double end_time = 0.0;
  const char* T_UNIT = nullptr;   /* "s" / "ms" */

  int32_t return_timebase = 0;     /* 是否返回时间数据 */
} hda_read_req_t;

typedef struct hda_read_1d_resp_t {
  hda_dtype_t dtype;
  int64_t start_index;
  int32_t sample_count;

  double* out_time;
  int32_t out_time_bytes;

  void* out_value;
  int32_t out_value_bytes;
} hda_read_1d_resp_t;

typedef struct hda_read_0d_req_t {
  int64_t shot;                    /* 炮号 */
  const char* channel;
} hda_read_0d_req_t;

typedef struct hda_read_0d_resp_t {
  hda_dtype_t dtype;
  int32_t count;

  void* out_buffer;
  int32_t out_buffer_bytes;
} hda_read_0d_resp_t;

/* 2D数组读取响应 */
typedef struct hda_read_2d_resp_t {
  hda_dtype_t dtype;
  int64_t start_index;
  int32_t sample_count;

  double* out_time;
  int32_t out_time_bytes;

  void* out_value;
  int32_t out_value_bytes;
} hda_read_2d_resp_t;

typedef struct hda_read_time_req_t {
  int64_t shot;                    /* 炮号 */
  const char* channel;             /* 通道名称 */
} hda_read_time_req_t;

typedef struct hda_read_time_resp_t {
  int64_t count;
  double* out_time;          /* 用户提供的缓冲区，由用户管理 */
  int32_t out_time_bytes;
  const char* T_UNIT;        /* 指向静态字符串 "s" 或 "ms"，无需释放 */
} hda_read_time_resp_t;

/* =========================
 *  API：连接/断开
 * ========================= */
HDA_API hda_status_t HDA_CALL
hda_client_connect(const hda_client_config_t* cfg, hda_client_t* out_client);

HDA_API hda_status_t HDA_CALL
hda_client_disconnect(hda_client_t client);

/* =========================
 *  API：写入
 * ========================= */
HDA_API hda_status_t HDA_CALL
hda_open_writer(hda_client_t client, const hda_write_key_t* key, hda_writer_t* out_writer);

HDA_API hda_status_t HDA_CALL
hda_write_1d(hda_writer_t writer, const hda_write_1d_req_t* req);

HDA_API hda_status_t HDA_CALL
hda_write_0d(hda_writer_t writer, const hda_write_0d_req_t* req);

HDA_API hda_status_t HDA_CALL
hda_write_2d(hda_writer_t writer, const hda_write_2d_req_t* req);

HDA_API hda_status_t HDA_CALL
hda_write_subsystem_time(hda_writer_t writer, const hda_write_subsystem_time_req_t* req);

HDA_API hda_status_t HDA_CALL
hda_write_attribute(hda_writer_t writer,
                    const hda_write_attribute_req_t* req,
                    hda_write_attribute_resp_t* resp);

/* 关闭writer（无flush/abort；无hash/file_id返回） */
HDA_API hda_status_t HDA_CALL
hda_close_writer(hda_writer_t writer, int32_t timeout_ms);

/* =========================
 *  API：读取
 * ========================= */
HDA_API hda_status_t HDA_CALL
hda_read_1d(hda_client_t client,
            const hda_read_req_t* req,
            hda_read_1d_resp_t* resp);

HDA_API hda_status_t HDA_CALL
hda_read_0d(hda_client_t client,
            const hda_read_0d_req_t* req,
            hda_read_0d_resp_t* resp);

HDA_API hda_status_t HDA_CALL
hda_read_2d(hda_client_t client,
            const hda_read_req_t* req,
            hda_read_2d_resp_t* resp);

HDA_API hda_status_t HDA_CALL
hda_read_time(hda_client_t client,
              const hda_read_time_req_t* req,
              hda_read_time_resp_t* resp);

HDA_API hda_status_t HDA_CALL
hda_read_attribute(hda_client_t client,
                   const hda_read_attribute_req_t* req,
                   hda_read_attribute_resp_t* resp);

/* =========================
 *  API：错误详情
 * ========================= */
/**
 * 获取最近一次操作的错误信息。
 *
 * 注意：此函数返回的是线程局部存储的错误信息。
 * 如果同一线程中有多个 client/writer 对象，
 * 请在操作失败后立即调用此函数，否则错误信息可能被后续操作覆盖。
 *
 * @param client 客户端句柄
 * @param out_buf 输出缓冲区
 * @param buf_len 缓冲区长度
 * @return 状态码
 */
HDA_API hda_status_t HDA_CALL
hda_get_last_error(hda_client_t client, char* out_buf, int32_t buf_len);

/* =========================
 *  API：版本信息
 * ========================= */
/**
 * 获取 DLL 版本号字符串。
 *
 * @return 版本字符串指针，如 "1.0.0"。该指针指向静态内存，无需释放。
 */
HDA_API const char* HDA_CALL hda_get_version(void);

/**
 * 获取 DLL 版本号的各组成部分。
 *
 * @param out_major 输出主版本号
 * @param out_minor 输出次版本号
 * @param out_patch 输出补丁版本号
 */
HDA_API void HDA_CALL hda_get_version_numbers(int* out_major, int* out_minor, int* out_patch);

#ifdef __cplusplus
} /* extern "C" */
#endif
