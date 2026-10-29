#ifndef __SNT_TEXT_H__
#define __SNT_TEXT_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 文本规范化模式
 */
typedef enum {
    /* 默认模式：清洗无效 UTF-8，全角转半角，保留数字/日期/时间单元完整，
     * 适配 eSpeak-ng 原生发音解析（与 Task A 黄金集完全一致） */
    SNT_TEXT_NORM_DEFAULT = 0,
    /* 汉字读法展开（如 12345 -> 一万二千三百四十五，08:30 -> 八点三十分） */
    SNT_TEXT_NORM_EXPAND_WORDS = 1,
    /* 逐位数字念法展开（如 12345 -> 一二三四五） */
    SNT_TEXT_NORM_EXPAND_DIGITS = 2
} snt_text_norm_mode_t;

/**
 * 检查字符串是否为严格有效 UTF-8。
 * @param utf8 输入字节流
 * @param bytes 字节数
 * @param valid_bytes 输出有效前缀字节数（可为 NULL）
 * @return true 全部有效；false 含有非法 UTF-8 序列
 */
bool snt_text_is_valid_utf8(const char *utf8, size_t bytes, size_t *valid_bytes);

/**
 * 读取下一个 UTF-8 Unicode 码点。
 * @param s 输入指针
 * @param remain 剩余字节数
 * @param cp 输出 Unicode 码点（可为 NULL）
 * @return 该码点所占字节数（1-4），遇非法序列返回 0
 */
size_t snt_text_next_codepoint(const char *s, size_t remain, uint32_t *cp);

/**
 * 文本规范化：
 * - 验证并清洗 UTF-8 字符序列（非法字符跳过或替换为安全空格）
 * - 过滤/规范化不可见控制字符
 * - 全角数字/标点转半角（如 １２３４５ -> 12345，： -> :，，。！？ -> ,.!?）
 * - 保证规范化单元（数字、时间、日期）不被截断
 * - 若 mode == SNT_TEXT_NORM_EXPAND_WORDS / EXPAND_DIGITS，进行明确中文数词转换
 * 输出写入 out 缓冲区并以 '\0' 结尾。
 *
 * @param in 输入 UTF-8 文本
 * @param in_len 输入字节数
 * @param out 输出缓冲区
 * @param out_cap 输出缓冲区容量（字节）
 * @param out_len 输出实际字节数（不含 '\0'）
 * @param mode 规范化模式
 * @return 0 成功；负数错误码 (SNT_EINVAL, SNT_E2BIG)
 */
int snt_text_normalize(const char *in, size_t in_len,
                       char *out, size_t out_cap, size_t *out_len,
                       snt_text_norm_mode_t mode);

/**
 * 常用中文转换辅助函数
 */
int snt_text_int_to_chinese_words(int64_t val, char *out, size_t cap);
int snt_text_digits_to_chinese_telecom(const char *digits, char *out, size_t cap);
int snt_text_decimal_to_chinese(const char *decimal_str, char *out, size_t cap);
int snt_text_time_to_chinese(int hour, int minute, char *out, size_t cap);
int snt_text_date_to_chinese(int year, int month, int day, char *out, size_t cap);

/**
 * 分句回调函数原型：
 * @param seg_text 当前分段文本指针（不保证以 \0 结尾）
 * @param seg_bytes 当前分段文本字节数
 * @param seg_index 当前分段序号（从 0 开始）
 * @param user 用户上下文指针
 * @return 0 继续下一段；非 0 中止分句
 */
typedef int (*snt_text_segment_cb)(const char *seg_text, size_t seg_bytes,
                                   size_t seg_index, void *user);

/**
 * 智能分句与切分：
 * - 优先依据中文标点分句（。！？；及英文 .!?; \n 等）
 * - 保证完整 UTF-8 字符，规范化单元（时间、日期、数字）不可被中途切断
 * - 限制单段最大字符数（默认约 25 个中文汉字），确保生成的音素 IDs <= 256
 * - 对超长无标点句子，在逗号/空格或字符安全边界切分
 * - 回调顺序逐段输出
 *
 * @param utf8 输入文本
 * @param bytes 字节数
 * @param max_chars_per_seg 单段最大字符数建议值（传 0 使用默认 25）
 * @param cb 分段回调函数
 * @param user 用户上下文
 * @return 0 成功；负数错误码 (SNT_EINVAL)
 */
int snt_text_split_segments(const char *utf8, size_t bytes,
                            size_t max_chars_per_seg,
                            snt_text_segment_cb cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* __SNT_TEXT_H__ */
