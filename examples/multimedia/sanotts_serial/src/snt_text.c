#include "snt_text.h"
#include "snt_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- UTF-8 Utilities ---------------------------------------------------- */

size_t snt_text_next_codepoint(const char *s, size_t remain, uint32_t *cp) {
    if (!s || remain == 0) return 0;
    const unsigned char *u = (const unsigned char *)s;
    unsigned char b0 = u[0];

    if (b0 < 0x80) {
        if (cp) *cp = b0;
        return 1;
    }

    if ((b0 & 0xE0) == 0xC0) {
        if (remain < 2) return 0;
        unsigned char b1 = u[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        if (b0 < 0xC2) return 0; /* Overlong */
        if (cp) *cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
        return 2;
    }

    if ((b0 & 0xF0) == 0xE0) {
        if (remain < 3) return 0;
        unsigned char b1 = u[1];
        unsigned char b2 = u[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return 0;
        if (b0 == 0xE0 && b1 < 0xA0) return 0; /* Overlong */
        if (b0 == 0xED && b1 >= 0xA0) return 0; /* Surrogate 0xD800-0xDFFF */
        if (cp) *cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
        return 3;
    }

    if ((b0 & 0xF8) == 0xF0) {
        if (remain < 4) return 0;
        unsigned char b1 = u[1];
        unsigned char b2 = u[2];
        unsigned char b3 = u[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0;
        if (b0 == 0xF0 && b1 < 0x90) return 0; /* Overlong */
        if (b0 == 0xF4 && b1 >= 0x90) return 0; /* > 0x10FFFF */
        if (b0 > 0xF4) return 0;
        if (cp) *cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
        return 4;
    }

    return 0;
}

bool snt_text_is_valid_utf8(const char *utf8, size_t bytes, size_t *valid_bytes) {
    if (!utf8) {
        if (valid_bytes) *valid_bytes = 0;
        return false;
    }
    size_t off = 0;
    while (off < bytes) {
        size_t step = snt_text_next_codepoint(utf8 + off, bytes - off, NULL);
        if (step == 0) {
            if (valid_bytes) *valid_bytes = off;
            return false;
        }
        off += step;
    }
    if (valid_bytes) *valid_bytes = off;
    return true;
}

/* Helper to append a single UTF-8 codepoint to a buffer */
static size_t append_codepoint(char *out, size_t cap, size_t cur_len, uint32_t cp) {
    if (cp < 0x80) {
        if (cur_len + 1 <= cap) out[cur_len] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        if (cur_len + 2 <= cap) {
            out[cur_len + 0] = (char)(0xC0 | (cp >> 6));
            out[cur_len + 1] = (char)(0x80 | (cp & 0x3F));
        }
        return 2;
    } else if (cp < 0x10000) {
        if (cur_len + 3 <= cap) {
            out[cur_len + 0] = (char)(0xE0 | (cp >> 12));
            out[cur_len + 1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[cur_len + 2] = (char)(0x80 | (cp & 0x3F));
        }
        return 3;
    } else if (cp <= 0x10FFFF) {
        if (cur_len + 4 <= cap) {
            out[cur_len + 0] = (char)(0xF0 | (cp >> 18));
            out[cur_len + 1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[cur_len + 2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[cur_len + 3] = (char)(0x80 | (cp & 0x3F));
        }
        return 4;
    }
    return 0;
}

static size_t append_str(char *out, size_t cap, size_t cur_len, const char *s) {
    size_t len = strlen(s);
    if (cur_len + len <= cap) {
        memcpy(out + cur_len, s, len);
    }
    return len;
}

/* ---- Chinese Number Expansion Helpers ---------------------------------- */

static const char *const ZH_DIGITS[] = {
    "零", "一", "二", "三", "四", "五", "六", "七", "八", "九"
};

int snt_text_digits_to_chinese_telecom(const char *digits, char *out, size_t cap) {
    if (!digits || !out || cap == 0) return SNT_EINVAL;
    size_t cur = 0;
    for (size_t i = 0; digits[i]; i++) {
        char c = digits[i];
        if (c >= '0' && c <= '9') {
            const char *zh = ZH_DIGITS[c - '0'];
            cur += append_str(out, cap - 1, cur, zh);
        } else {
            /* Copy non-digit as is */
            if (cur + 1 < cap) out[cur] = c;
            cur++;
        }
    }
    if (cur < cap) out[cur] = '\0';
    else out[cap - 1] = '\0';
    return (cur < cap) ? SNT_OK : SNT_E2BIG;
}

/* Helper for cardinal numbers under 10,000 */
static size_t format_under_10k(int val, char *out, size_t cap, size_t cur, bool has_higher) {
    if (val == 0) return 0;
    int qian = val / 1000;
    int bai = (val % 1000) / 100;
    int shi = (val % 100) / 10;
    int ge = val % 10;

    bool zero_pending = has_higher && (qian == 0);

    if (qian > 0) {
        cur += append_str(out, cap, cur, ZH_DIGITS[qian]);
        cur += append_str(out, cap, cur, "千");
    }

    if (bai > 0) {
        if (zero_pending) {
            cur += append_str(out, cap, cur, "零");
            zero_pending = false;
        }
        cur += append_str(out, cap, cur, ZH_DIGITS[bai]);
        cur += append_str(out, cap, cur, "百");
    } else if (qian > 0 && (shi > 0 || ge > 0)) {
        zero_pending = true;
    }

    if (shi > 0) {
        if (zero_pending) {
            cur += append_str(out, cap, cur, "零");
            zero_pending = false;
        }
        if (!(shi == 1 && qian == 0 && bai == 0 && !has_higher)) {
            cur += append_str(out, cap, cur, ZH_DIGITS[shi]);
        }
        cur += append_str(out, cap, cur, "十");
    } else if (bai > 0 && ge > 0) {
        zero_pending = true;
    }

    if (ge > 0) {
        if (zero_pending) {
            cur += append_str(out, cap, cur, "零");
        }
        cur += append_str(out, cap, cur, ZH_DIGITS[ge]);
    }

    return cur;
}

int snt_text_int_to_chinese_words(int64_t val, char *out, size_t cap) {
    if (!out || cap == 0) return SNT_EINVAL;
    if (val == 0) {
        if (cap < 4) return SNT_E2BIG;
        snprintf(out, cap, "零");
        return SNT_OK;
    }

    size_t cur = 0;
    if (val < 0) {
        cur += append_str(out, cap - 1, cur, "负");
        val = -val;
    }

    int yi = (int)(val / 100000000LL);
    int wan = (int)((val % 100000000LL) / 10000);
    int ge = (int)(val % 10000);

    bool has_higher = false;
    if (yi > 0) {
        cur = format_under_10k(yi, out, cap - 1, cur, false);
        cur += append_str(out, cap - 1, cur, "亿");
        has_higher = true;
    }

    if (wan > 0) {
        cur = format_under_10k(wan, out, cap - 1, cur, has_higher);
        cur += append_str(out, cap - 1, cur, "万");
        has_higher = true;
    } else if (has_higher && ge > 0) {
        cur += append_str(out, cap - 1, cur, "零");
    }

    if (ge > 0) {
        cur = format_under_10k(ge, out, cap - 1, cur, has_higher);
    }

    if (cur < cap) out[cur] = '\0';
    else out[cap - 1] = '\0';
    return (cur < cap) ? SNT_OK : SNT_E2BIG;
}

int snt_text_decimal_to_chinese(const char *decimal_str, char *out, size_t cap) {
    if (!decimal_str || !out || cap == 0) return SNT_EINVAL;
    char int_buf[64];
    const char *dot = strchr(decimal_str, '.');
    if (!dot) {
        int64_t v = (int64_t)atoll(decimal_str);
        return snt_text_int_to_chinese_words(v, out, cap);
    }

    size_t int_len = (size_t)(dot - decimal_str);
    if (int_len >= sizeof(int_buf)) int_len = sizeof(int_buf) - 1;
    memcpy(int_buf, decimal_str, int_len);
    int_buf[int_len] = '\0';

    int64_t iv = (int_len > 0) ? (int64_t)atoll(int_buf) : 0;
    char iv_zh[128];
    snt_text_int_to_chinese_words(iv, iv_zh, sizeof(iv_zh));

    char frac_zh[128];
    snt_text_digits_to_chinese_telecom(dot + 1, frac_zh, sizeof(frac_zh));

    size_t n = snprintf(out, cap, "%s点%s", iv_zh, frac_zh);
    return (n < cap) ? SNT_OK : SNT_E2BIG;
}

int snt_text_time_to_chinese(int hour, int minute, char *out, size_t cap) {
    if (!out || cap == 0 || hour < 0 || hour > 23 || minute < 0 || minute > 59)
        return SNT_EINVAL;

    char h_zh[64], m_zh[64];
    snt_text_int_to_chinese_words(hour, h_zh, sizeof(h_zh));

    size_t cur = 0;
    cur += append_str(out, cap - 1, cur, h_zh);
    cur += append_str(out, cap - 1, cur, "点");

    if (minute == 0) {
        cur += append_str(out, cap - 1, cur, "整");
    } else if (minute < 10) {
        cur += append_str(out, cap - 1, cur, "零");
        snt_text_int_to_chinese_words(minute, m_zh, sizeof(m_zh));
        cur += append_str(out, cap - 1, cur, m_zh);
        cur += append_str(out, cap - 1, cur, "分");
    } else {
        snt_text_int_to_chinese_words(minute, m_zh, sizeof(m_zh));
        cur += append_str(out, cap - 1, cur, m_zh);
        cur += append_str(out, cap - 1, cur, "分");
    }

    if (cur < cap) out[cur] = '\0';
    else out[cap - 1] = '\0';
    return (cur < cap) ? SNT_OK : SNT_E2BIG;
}

int snt_text_date_to_chinese(int year, int month, int day, char *out, size_t cap) {
    if (!out || cap == 0 || year < 0 || month < 1 || month > 12 || day < 1 || day > 31)
        return SNT_EINVAL;

    char y_digits[16];
    snprintf(y_digits, sizeof(y_digits), "%d", year);
    char y_zh[64], m_zh[64], d_zh[64];
    snt_text_digits_to_chinese_telecom(y_digits, y_zh, sizeof(y_zh));
    snt_text_int_to_chinese_words(month, m_zh, sizeof(m_zh));
    snt_text_int_to_chinese_words(day, d_zh, sizeof(d_zh));

    size_t n = snprintf(out, cap, "%s年%s月%s日", y_zh, m_zh, d_zh);
    return (n < cap) ? SNT_OK : SNT_E2BIG;
}

/* ---- Text Normalization ------------------------------------------------ */

int snt_text_normalize(const char *in, size_t in_len,
                       char *out, size_t out_cap, size_t *out_len,
                       snt_text_norm_mode_t mode) {
    if (!in || !out || out_cap == 0) return SNT_EINVAL;

    size_t cur = 0;
    size_t off = 0;

    while (off < in_len) {
        uint32_t cp = 0;
        size_t step = snt_text_next_codepoint(in + off, in_len - off, &cp);
        if (step == 0) {
            /* Skip or replace invalid byte */
            off++;
            continue;
        }

        /* 1. Skip control characters except \n and \t */
        if (cp < 0x20 && cp != '\n' && cp != '\t') {
            off += step;
            continue;
        }

        /* 2. Full-width ASCII: 0xFF01 ('!') to 0xFF5E ('~') -> cp - 0xFEE0 */
        if (cp >= 0xFF01 && cp <= 0xFF5E) {
            cp = cp - 0xFEE0;
        } else if (cp == 0x3000) {
            /* Full-width space */
            cp = 0x20;
        } else if (cp == 0x3002) {
            /* Chinese period '。' -> '.' */
            cp = '.';
        } else if (cp == 0x3001) {
            /* Chinese enumeration comma '、' -> ',' */
            cp = ',';
        }

        /* Mode-specific expansions */
        if (mode == SNT_TEXT_NORM_EXPAND_WORDS || mode == SNT_TEXT_NORM_EXPAND_DIGITS) {
            /* Check if this is start of a number */
            if (cp >= '0' && cp <= '9') {
                /* Look ahead to parse entire number token */
                size_t num_start = off;
                size_t num_end = off + step;
                bool has_dot = false;
                bool is_time = false;
                bool is_date = false;

                while (num_end < in_len) {
                    uint32_t ncp = 0;
                    size_t nstep = snt_text_next_codepoint(in + num_end, in_len - num_end, &ncp);
                    if (nstep == 0) break;
                    if (ncp >= 0xFF10 && ncp <= 0xFF19) ncp -= 0xFEE0;
                    if (ncp >= '0' && ncp <= '9') {
                        num_end += nstep;
                    } else if (ncp == '.' || ncp == 0xFF0E) {
                        has_dot = true;
                        num_end += nstep;
                    } else if (ncp == ':' || ncp == 0xFF1A) {
                        is_time = true;
                        num_end += nstep;
                    } else {
                        /* Check for date marker '年' */
                        if (ncp == 0x5E74) is_date = true;
                        break;
                    }
                }

                /* If it's a plain integer and mode is expand words */
                if (!has_dot && !is_time && !is_date) {
                    char num_str[32];
                    size_t num_len = 0;
                    for (size_t p = num_start; p < num_end && num_len < sizeof(num_str) - 1; ) {
                        uint32_t dcp;
                        size_t ds = snt_text_next_codepoint(in + p, in_len - p, &dcp);
                        if (dcp >= 0xFF10 && dcp <= 0xFF19) dcp -= 0xFEE0;
                        if (dcp >= '0' && dcp <= '9') num_str[num_len++] = (char)dcp;
                        p += ds;
                    }
                    num_str[num_len] = '\0';

                    if (mode == SNT_TEXT_NORM_EXPAND_WORDS) {
                        char zh_buf[128];
                        int64_t val = (int64_t)atoll(num_str);
                        snt_text_int_to_chinese_words(val, zh_buf, sizeof(zh_buf));
                        cur += append_str(out, out_cap - 1, cur, zh_buf);
                    } else {
                        char zh_buf[128];
                        snt_text_digits_to_chinese_telecom(num_str, zh_buf, sizeof(zh_buf));
                        cur += append_str(out, out_cap - 1, cur, zh_buf);
                    }
                    off = num_end;
                    continue;
                }
            }
        }

        /* Standard append */
        cur += append_codepoint(out, out_cap - 1, cur, cp);
        off += step;
    }

    if (cur < out_cap) {
        out[cur] = '\0';
    } else {
        out[out_cap - 1] = '\0';
    }

    if (out_len) *out_len = cur;
    return (cur < out_cap) ? SNT_OK : SNT_E2BIG;
}

/* ---- Sentence & Segment Splitting --------------------------------------- */

static bool is_punct_terminator(uint32_t cp) {
    return (cp == '.' || cp == '!' || cp == '?' || cp == ';' || cp == '\n' ||
            cp == 0x3002 /* 。 */ || cp == 0xFF01 /* ！ */ ||
            cp == 0xFF1F /* ？ */ || cp == 0xFF1B /* ； */);
}

static bool is_punct_pause(uint32_t cp) {
    return (cp == ',' || cp == ':' || cp == 0xFF0C /* ， */ ||
            cp == 0x3001 /* 、 */ || cp == 0xFF1A /* ： */ ||
            cp == 0x2014 /* — */ || cp == 0x2026 /* … */);
}

/* Check if byte offset 'pos' in 'text' is within a numeric / date / time unit */
static bool is_in_atomic_unit(const char *text, size_t len, size_t pos) {
    if (pos == 0 || pos >= len) return false;

    /* Scan backwards up to 16 bytes for a digit or colon or date unit */
    size_t start = (pos > 16) ? (pos - 16) : 0;
    bool digit_before = false;
    for (size_t i = pos; i > start; ) {
        i--;
        unsigned char c = (unsigned char)text[i];
        if (isdigit(c) || c == ':' || c == '.') {
            digit_before = true;
            break;
        }
        if (c < 0x80 && !isspace(c)) break;
    }

    if (!digit_before) return false;

    /* Scan forwards up to 16 bytes */
    size_t end = (pos + 16 < len) ? (pos + 16) : len;
    bool digit_after = false;
    for (size_t i = pos; i < end; i++) {
        unsigned char c = (unsigned char)text[i];
        if (isdigit(c) || c == ':' || c == '.') {
            digit_after = true;
            break;
        }
        /* Chinese date units: 年, 月, 日 */
        if (i + 2 < end) {
            uint32_t cp;
            size_t step = snt_text_next_codepoint(text + i, end - i, &cp);
            if (step > 0 && (cp == 0x5E74 /* 年 */ || cp == 0x6708 /* 月 */ || cp == 0x65E5 /* 日 */)) {
                digit_after = true;
                break;
            }
        }
        if (c < 0x80 && !isspace(c)) break;
    }

    return digit_before && digit_after;
}

int snt_text_split_segments(const char *utf8, size_t bytes,
                            size_t max_chars_per_seg,
                            snt_text_segment_cb cb, void *user) {
    if (!utf8 || !cb) return SNT_EINVAL;
    if (bytes == 0) return SNT_OK;
    if (max_chars_per_seg == 0) max_chars_per_seg = 25;

    size_t seg_index = 0;
    size_t off = 0;

    while (off < bytes) {
        /* Skip leading whitespace */
        while (off < bytes && isspace((unsigned char)utf8[off])) off++;
        if (off >= bytes) break;

        size_t seg_start = off;
        size_t char_count = 0;
        size_t best_break = 0;
        size_t last_pause = 0;
        size_t p = off;

        while (p < bytes) {
            uint32_t cp = 0;
            size_t step = snt_text_next_codepoint(utf8 + p, bytes - p, &cp);
            if (step == 0) {
                p++;
                continue;
            }

            char_count++;
            p += step;

            /* Check for terminator */
            if (is_punct_terminator(cp)) {
                /* Include following closing quotes or brackets */
                while (p < bytes) {
                    uint32_t ncp = 0;
                    size_t nstep = snt_text_next_codepoint(utf8 + p, bytes - p, &ncp);
                    if (nstep > 0 && (ncp == 0x201D /* ” */ || ncp == 0x2019 /* ’ */ ||
                                      ncp == ')' || ncp == ']' || ncp == '}' ||
                                      ncp == 0x300D /* 」 */ || ncp == 0x300F /* 』 */ ||
                                      ncp == 0xFF09 /* ） */ || ncp == 0x3011 /* 】 */)) {
                        p += nstep;
                    } else {
                        break;
                    }
                }
                best_break = p;
                break;
            }

            /* Record secondary pauses */
            if (is_punct_pause(cp)) {
                last_pause = p;
            }

            /* If character limit reached */
            if (char_count >= max_chars_per_seg) {
                if (last_pause > seg_start && !is_in_atomic_unit(utf8, bytes, last_pause)) {
                    best_break = last_pause;
                } else if (!is_in_atomic_unit(utf8, bytes, p)) {
                    best_break = p;
                } else {
                    /* Advance past atomic unit */
                    while (p < bytes && is_in_atomic_unit(utf8, bytes, p)) {
                        uint32_t acp;
                        size_t astep = snt_text_next_codepoint(utf8 + p, bytes - p, &acp);
                        if (astep == 0) break;
                        p += astep;
                    }
                    best_break = p;
                }
                break;
            }
        }

        if (best_break == 0 || best_break <= seg_start) {
            best_break = p;
        }

        size_t seg_len = best_break - seg_start;
        if (seg_len > 0) {
            int rc = cb(utf8 + seg_start, seg_len, seg_index++, user);
            if (rc != 0) {
                /* Consumer requested stop */
                return SNT_OK;
            }
        }

        off = best_break;
    }

    return SNT_OK;
}
