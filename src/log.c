#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include "uthash.h"

#define _LOG_SRC "UNKNOWN"
#include "log.h"

/**
 * Максимальный размер отображаемой части источника лога
 */
#define LOG_SRC_MAX_SIZE 16

/**
 * Максимальный размер хрянящегося источника в хэше
 */
#define LOG_SRC_STORED_MAX_SIZE 128

/**
 * Максимальная длина имени функции
 */
#define LOG_FUNCTION_NAME_MAX_SIZE 20

/**
 * Максимальная длина имени файла
 */
#define LOG_FILE_NAME_MAX_SIZE 20

/**
 * 1 - включает вывод имени функции в лог
 * 0 - выключает
 */
#ifndef DO_LOG_FUNCTION_NAME
#define DO_LOG_FUNCTION_NAME 0
#endif

/**
 * 1 - включает вывод текущего времени в лог
 * 0 - выключает
 */
#ifndef DO_LOG_CURRENT_TIME
#define DO_LOG_CURRENT_TIME 0
#endif

/**
 * Ширина поля адреса в строке log_raw
 */
#define LOG_RAW_ADDR_FIELD_WIDTH 8

#define MUTEX_CHECK_LOCK(p_mutex)                                             \
{                                                                             \
    int mutex_lock_res;                                                       \
    mutex_lock_res = pthread_mutex_lock(p_mutex);                             \
    assert(mutex_lock_res == 0);                                              \
    UNUSED_PARAM(mutex_lock_res);                                             \
}

#define MUTEX_CHECK_UNLOCK(p_mutex)                                           \
{                                                                             \
    int mutex_unlock_res;                                                     \
    mutex_unlock_res = pthread_mutex_unlock(p_mutex);                         \
    assert(mutex_unlock_res == 0);                                            \
    UNUSED_PARAM(mutex_unlock_res);                                           \
}

/**
 * Контекст источника лога
 */
typedef struct tag_log_source_hm_elt
{
    UT_hash_handle hh;
    char           source[LOG_SRC_STORED_MAX_SIZE]; /*!< источник */
    log_level_t    min_log_level;                   /*!< минимально выводимый уровень логов (для всех источников)*/
}
log_source_hm_elt_t;

/**
 * Контекст системы логирования
 */
typedef struct tag_log_ctx
{
    log_source_hm_elt_t *source_hm;     /*!< хранилище зарегистрированнных источников лога */
    log_level_t          min_log_level; /*!< минимально выводимый уровень логов для всех источников */
    pthread_mutex_t      mutex;         /*!< мьютекс */
    volatile bool        use_mutex;     /*!< флаг необходимости использования мьютекса */
    volatile bool        initialized;   /*!< конекст уже инициализирован */
}
log_ctx_t;


/**
 * Структура, с описанием даты и времени
 */
struct tag_log_datetime
{
    int year;  /*!< год */
    int month; /*!< месяц года */
    int day;   /*!< день месяца */
    int hour;  /*!< час (0-23) */
    int min;   /*!< минута (0-59) */
    int sec;   /*!< секунда (0-59) */
    int usec;  /*!< микросекунда (0-999999) */
};

static
log_ctx_t log_ctx; ///< глобальный контекст системы логгирования.

static
char log_buf[8192]; ///< глобальный буфер логгирования.

/**
 * mapping уровней лога в текст
 */
static
const char * const log_level_map[LL_CNT] =
{
    "INVALID",
    "RAW",
    "TRACE",
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR",
    "NONE"
};

#if DO_LOG_CURRENT_TIME
/**
 * Возвращает текущие дату и время.
 *
 * @param dt [out] дата и время (!= NULL)
 */
static
void get_current_time(struct tag_log_datetime *dt) __attribute__((nonnull(1)));

/**
 * Преобразует дату и время log_datetime в строку
 * @param dt               [in]  преобразуемая дата и время
 * @param result           [out] строка
 * @param result_max_size  [in]  максимальный размер буфера для строки в байтах (не забудь учесть конечный 0)
 * @return result
 */
static
char *print_current_time(const struct tag_log_datetime *dt,
                         char                          *result,
                         size_t                         result_max_size) __attribute__((nonnull(1, 2)));
#endif

/**
 * Блокирует мьютекс контекста, если требуется
 *
 * @param ctx [in/out] контекст системы логгирования (!= NULL)
 */
static
void lock_mutex_if_it_needs(log_ctx_t *ctx) __attribute__((nonnull(1)));

/**
 * Разблокирует мьютекс контекста, если требуется
 *
 * @param ctx [in/out] контекст системы логгирования (!= NULL)
 */
static
void unlock_mutex_if_it_needs(log_ctx_t *ctx) __attribute__((nonnull(1)));

/**
 * Извлекает имя файла на основе полного пути.
 *
 * @param file_path [in] Полный путь к файлу (!= NULL)
 * @return имя файла
 */
static
const char *extract_file_name(const char *file_path) __attribute__((nonnull(1))) __attribute__((warn_unused_result));

/**
 * Проверяет, позволяет ли requested уровень лога логгировать, если установлен current уровень лога.
 *
 * @param requested [in] запрашиваемый уровень лога.
 * @param current   [in] установленный уровень лога.
 * @return true - Да, false - Нет.
 */
static
bool check_log_level(log_level_t requested, log_level_t current) __attribute__((warn_unused_result));

/**
 * Проверяет, позволено ли логгирование для указанного источника с заданным уровнем лога
 *
 * @param source    [in] источник (!= NULL)
 * @param log_level [in] уровень лога ( LL_INVALID < log_level < LL_CNT ).
 * @return true - позволено, false - не позволено.
 */
static
bool is_log_allowed(const char  *source,
                    log_level_t  log_level) __attribute__((nonnull(1))) __attribute__((warn_unused_result));

/**
 * Производит hexdump буфера в строку (без символа перевода строки). Максимальная длина дампа - 16 байт
 *
 * @param buf_in      [in]  буфер, дамп котогого требуется сделать (!= NULL)
 * @param buf_in_size [in]  размер буфера в байтах
 * @param offset      [in]  смещение в буфере (байты / 16)
 * @param buf_out     [out] результирующий текстовый буфер с дампом строки. Минимальный размер - 100 байт (!= NULL)
 */
static
void compose_hexdump_line(const void *buf_in,
                          size_t      buf_in_size,
                          size_t      offset,
                          char       *buf_out) __attribute__((nonnull(1,4)));

/**
 * Генерирует префикс лога.
 *
 * @param prefix_buf      [out] буфер для строки со сформированным префиксом (!= NULL).
 * @param prefix_buf_size [in]  размер буфера выше в байтах.
 * @param source          [in]  источник (строка - источника лога) (максимальная длина LOG_SRC_MAX_SIZE остальное обрезается) (!= NULL)
 * @param file            [in]  имя файла (максимальная длина LOG_FUNCTION_NAME_MAX_SIZE).
 * @param line            [in]  номер строки в файле.
 * @param function        [in]  имя функции (максимальная длина LOG_FILE_NAME_MAX_SIZE).
 * @param log_level       [in]  уровень выводимого лога (< LL_CNT).
 * @return log_buf.
 */
static
char *compose_log_prefix(char        *prefix_buf,
                         size_t       prefix_buf_size,
                         const char  *source,
                         const char  *file,
                         const char  *line,
                         const char  *function,
                         log_level_t  log_level) __attribute__((nonnull(1, 3, 4, 5, 6)));

/**
 * Генерирует префикс лога.
 *
 * @param prefix_buf      [out] буфер для строки со сформированным префиксом (!= NULL).
 * @param prefix_buf_size [in]  размер буфера выше в байтах.
 * @param source          [in]  источник (строка - источника лога) (максимальная длина LOG_SRC_MAX_SIZE остальное обрезается) (!= NULL)
 * @param file            [in]  имя файла (максимальная длина LOG_FUNCTION_NAME_MAX_SIZE).
 * @param line            [in]  номер строки в файле.
 * @param function        [in]  имя функции (максимальная длина LOG_FILE_NAME_MAX_SIZE).
 * @param log_level       [in]  уровень выводимого лога (< LL_CNT).
 * @return log_buf.
 */
static
char *compose_log_prefix(char        *prefix_buf,
                         size_t       prefix_buf_size,
                         const char  *source,
                         const char  *file,
                         const char  *line,
                         const char  *function,
                         log_level_t  log_level)
{
    #if DO_LOG_CURRENT_TIME
    struct tag_log_datetime dt;
    char time_buf[32];
    #endif

    assert(prefix_buf != NULL);
    assert(source != NULL);
    assert(file != NULL);
    assert(line != NULL);
    #if DO_LOG_FUNCTION_NAME
    assert(function != NULL);
    #endif

    UNUSED_PARAM(function);
    #if DO_LOG_CURRENT_TIME
    get_current_time(&dt);
    #endif
    snprintf(prefix_buf,
             prefix_buf_size,
             #if DO_LOG_CURRENT_TIME
             "%s:"
             #endif
             "[%-1.1s][%-"STRX(LOG_SRC_MAX_SIZE)"."STRX(LOG_SRC_MAX_SIZE)"s][%-"STRX(LOG_FILE_NAME_MAX_SIZE)"."STRX(LOG_FILE_NAME_MAX_SIZE)"s:%5s]"
             #if DO_LOG_FUNCTION_NAME
             " in %-"STRX(LOG_FUNCTION_NAME_MAX_SIZE)"."STRX(LOG_FUNCTION_NAME_MAX_SIZE)"s()",
             #else
             "",
             #endif
             #if DO_LOG_CURRENT_TIME
             print_current_time(&dt, time_buf, sizeof(time_buf)),
             #endif
             log_level_map[log_level],
             source,
             extract_file_name(file),
             #if DO_LOG_FUNCTION_NAME
             line,
             function);
             #else
             line);
             #endif
    return prefix_buf;
}

#if DO_LOG_CURRENT_TIME
/**
 * Возвращает текущие дату и время.
 *
 * @param dt [out] дата и время (!= NULL)
 */
static
void get_current_time(struct tag_log_datetime *dt)
{
    time_t _time;

    assert(dt != NULL);

    memset(dt, 0, sizeof(struct tag_log_datetime));
    _time = time(NULL);
    if (_time != (time_t)(-1))
    {
        struct tm *local_time;

        local_time = localtime(&_time);
        if (local_time)
        {
            struct timeval tv;

            dt->year  = 1900 + local_time->tm_year;
            dt->month = local_time->tm_mon + 1;
            dt->day   = local_time->tm_mday;
            dt->hour  = local_time->tm_hour;
            dt->min   = local_time->tm_min;
            dt->sec   = local_time->tm_sec;
            if (gettimeofday(&tv, NULL) == 0)
            {
                dt->usec = (int)(tv.tv_usec);
            }
        }
    }
}

/**
 * Преобразует дату и время log_datetime в строку ГГГГ.ММ.ДД-ЧЧ:ММ:СС:ХХХ
 * @param dt               [in]  преобразуемая дата и время
 * @param result           [out] строка
 * @param result_max_size  [in]  максимальный размер буфера для строки в байтах (не забудь учесть конечный 0)
 * @return result
 */
static
char *print_current_time(const struct tag_log_datetime *dt,
                         char                          *result,
                         size_t                         result_max_size)
{
    assert(dt != NULL);
    assert(result != NULL);

    snprintf(result,
             result_max_size,
             "%.4d.%.2d.%.2d-%.2d:%.2d:%.2d:%-4.3d",
             dt->year,
             dt->month,
             dt->day,
             dt->hour,
             dt->min,
             dt->sec,
             dt->usec/1000);
    return result;
}
#endif

/**
 * Блокирует мьютекс контекста, если требуется
 *
 * @param ctx [in/out] контекст системы логгирования (!= NULL)
 */
static
void lock_mutex_if_it_needs(log_ctx_t *ctx)
{
    assert(ctx != NULL);

    if (ctx->use_mutex) MUTEX_CHECK_LOCK(&ctx->mutex);
}

/**
 * Разблокирует мьютекс контекста, если требуется
 *
 * @param ctx [in/out] контекст системы логгирования (!= NULL)
 */
static
void unlock_mutex_if_it_needs(log_ctx_t *ctx)
{
    assert(ctx != NULL);

    if (ctx->use_mutex) MUTEX_CHECK_UNLOCK(&ctx->mutex);
}

/**
 * Извлекает имя файла на основе полного пути.
 *
 * @param file_path [in] Полный путь к файлу (!= NULL)
 * @return имя файла
 */
static
const char *extract_file_name(const char *file_path)
{
    assert(file_path != NULL);

    const char *pos;

    pos = strrchr(file_path,'/');
    if (pos)
    {
        return pos + 1;
    }
    pos = strrchr(file_path,'\\');
    if (pos)
    {
        return pos + 1;
    }
    return file_path;
}

/**
 * Проверяет, позволяет ли requested уровень лога логгировать, если установлен current уровень лога.
 *
 * @param requested [in] запрашиваемый уровень лога.
 * @param current   [in] установленный уровень лога.
 * @return true - Да, false - Нет.
 */
static
bool check_log_level(log_level_t requested, log_level_t current)
{
    return (requested >= current);
}

/**
 * Проверяет, позволено ли логгирование для указанного источника с заданным уровнем лога
 *
 * @param source    [in] источник (!= NULL)
 * @param log_level [in] уровень лога ( LL_INVALID < log_level < LL_CNT ).
 * @return true - позволено, false - не позволено.
 */
static
bool is_log_allowed(const char  *source,
                    log_level_t  log_level)
{
    assert(source != NULL);
    assert(log_level > LL_INVALID);
    assert(log_level < LL_CNT);

    /* проверить сперва глобальную настройку */
    if (check_log_level(log_level, log_ctx.min_log_level))
    {
        log_source_hm_elt_t *elt = NULL;
        /* найти в Хэше соответствующий источник */
        HASH_FIND_STR(log_ctx.source_hm, source, elt);
        if (elt)
        {
            if (check_log_level(log_level, elt->min_log_level)) return true;
        }
    }
    return false;
}

/**
 * Инициализирует систему логгирования.
 * Данный вызов не допускается 2 раза подряд.
 *
 * @param min_log_level  [in] глобально (для всех источников) минимально выводимый уровень логов (LL_NONE - отключает вывод).
 * @param is_thread_safe [in] флаг необходимости использовать примитивы синхронизации при каждом логгировании
 * @return true - OK, false - Fail
 */
extern
bool log_init(log_level_t min_log_level,
              bool        is_thread_safe)
{
    if (log_ctx.initialized == false)
    {
        /* проверка невалиндых параметров */
        if ((min_log_level <= LL_INVALID) || (min_log_level >= LL_CNT)) return false;

        log_ctx.min_log_level = min_log_level;
        log_ctx.use_mutex = is_thread_safe;
        if (is_thread_safe)
        {
            if (pthread_mutex_init(&(log_ctx.mutex), NULL) != 0)
            {
                return false;
            }
        }
        log_ctx.initialized = true;
        return true;
    }
    return false;
}

/**
 * Устанавливает глобальный уровень логгирования.
 *
 * @param min_log_level [in] глобально (для всех источников) минимально выводимый уровень логов (LL_NONE - отключает вывод).
 * @return true - OK, false - Fail
 */
extern
bool log_set_log_level(log_level_t min_log_level)
{
    if ((min_log_level <= LL_INVALID) || (min_log_level >= LL_CNT)) return false;
    if (log_ctx.initialized == false) return false;
    lock_mutex_if_it_needs(&log_ctx);
    log_ctx.min_log_level = min_log_level;
    unlock_mutex_if_it_needs(&log_ctx);
    return true;
}

/**
 * Регистрирует новый источник лога в системе логгирования.
 * Если заданный источник уже зерегистрирован, он перезаписывается.
 *
 * @param source        [in] источник лога (максимальная длина LOG_SRC_MAX_SIZE остальное обрезается) (!= NULL)
 * @param min_log_level [in] минимальный уровень выводимого лога (LL_NONE - отключает вывод).
 * @return true - OK, false - fail.
 */
extern
bool log_register(const char  *source,
                  log_level_t  min_log_level)
{
    bool result = false;
    log_source_hm_elt_t *source_hm_elt = NULL;
    log_source_hm_elt_t *existing = NULL;

    assert(source != NULL);

    /* проверка невалиндых параметров */
    if ((min_log_level <= LL_INVALID) || (min_log_level >= LL_CNT)) return false;
    if (log_ctx.initialized == false) return false;
    /* проверка на длину источника */
    if (strlen(source) > LOG_SRC_STORED_MAX_SIZE)
    {
        return false;
    }
    lock_mutex_if_it_needs(&log_ctx);
    /* если в хэше уже имеется данный источник */
    HASH_FIND_STR(log_ctx.source_hm, source, existing);
    if (existing)
    {
        /* скопировать источник в новый элемент */
        existing->min_log_level = min_log_level;
        result = true;
    }
    else
    {
        /* выделить новый элемент */
        source_hm_elt = calloc(1, sizeof(log_source_hm_elt_t));
        if (source_hm_elt)
        {
            /* скопировать источник в новый элемент */
            strncpy(source_hm_elt->source, source, LOG_SRC_STORED_MAX_SIZE-1);
            source_hm_elt->source[LOG_SRC_STORED_MAX_SIZE-1] = '\0'; // гарантированное NULL-териминирование
            source_hm_elt->min_log_level = min_log_level;
            HASH_ADD_STR(log_ctx.source_hm, source, source_hm_elt);
            result = true;
        }
    }
    unlock_mutex_if_it_needs(&log_ctx);
    return result;
}

/**
 * Регистрирует новые источники лога в системе логгирования.
 * Если один из источников уже зерегистрирован, он перезаписывается.
 * @param descr      [in] Массив дескрипторов источника лога.
 * @param num_descrs [in] Количество элементов в массиве num_descrs.
 * @return true - OK, false - не удалось зарегистрировать хотябы 1 источник.
 */
extern
bool log_register_ex(const log_src_descr_t *descr,
                     size_t                 num_descrs)
{
    size_t i;

    if (log_ctx.initialized == false) return false;
    if (num_descrs && !descr) return false;
    for (i = 0; i < num_descrs; i++)
    {
        if (!log_register(descr[i].source, descr[i].min_log_level))
        {
            return false;
        }
    }
    return true;
}

/**
 * Удаляет регистрацию источника (если зарегистрирован) в системе логгирования.
 *
 * @param source [in] источник лога (!= NULL)
 */
extern
void log_unregister(const char *source)
{
    log_source_hm_elt_t *elt = NULL;

    assert(source != NULL);

    if (log_ctx.initialized == false) return;
    lock_mutex_if_it_needs(&log_ctx);
    HASH_FIND_STR(log_ctx.source_hm, source, elt);
    if (elt)
    {
        HASH_DEL(log_ctx.source_hm, elt);
        free(elt);
    }
    unlock_mutex_if_it_needs(&log_ctx);
}

/**
 * Удаляет регистрацию всех источников в системе логгирования.
 *
 * @return true - OK, false -Fail
 */
extern
bool log_destroy()
{
    log_source_hm_elt_t *elt = NULL, *tmp = NULL;

    if (log_ctx.initialized)
    {
        lock_mutex_if_it_needs(&log_ctx);
        HASH_ITER(hh, log_ctx.source_hm, elt, tmp)
        {
            HASH_DEL(log_ctx.source_hm, elt);
            free(elt);
        }
        log_ctx.source_hm = NULL;
        if (log_ctx.use_mutex)
        {
            MUTEX_CHECK_UNLOCK(&log_ctx.mutex);
            if (pthread_mutex_destroy(&(log_ctx.mutex)) != 0)
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * Логгирует в стиле printf.
 * Печатает prefix и время перед логом.
 *
 * @param source    [in] источник (строка - источника лога) (максимальная длина LOG_SRC_MAX_SIZE остальное обрезается) (!= NULL)
 * @param file      [in] имя файла (максимальная длина LOG_FUNCTION_NAME_MAX_SIZE).
 * @param line      [in] номер строки в файле.
 * @param function  [in] имя функции (максимальная длина LOG_FILE_NAME_MAX_SIZE).
 * @param log_level [in] уровень выводимого лога (LL_INVALID < log_level < LL_CNT).
 * @param fmt       [in] (!= NULL).
 */
extern
void log_log(const char  *source,
             const char  *file,
             const char  *line,
             const char  *function,
             log_level_t  log_level,
             const char  *fmt, ...)
{
    assert(source != NULL);
    assert(file != NULL);
    assert(line != NULL);
    assert(function != NULL);
    assert(log_level > LL_INVALID);
    assert(log_level < LL_CNT);
    assert(fmt != NULL);

    if (log_ctx.initialized == false) return;
    lock_mutex_if_it_needs(&log_ctx);
    if (is_log_allowed(source, log_level))
    {
        char prefix[128];
        va_list args;

        snprintf(log_buf,
                 sizeof(log_buf),
                 "%s | %s\n",
                 compose_log_prefix(prefix, sizeof(prefix), source, file, line, function, log_level),
                 fmt);
        va_start(args, fmt);
        vfprintf(stderr, log_buf, args);
        va_end(args);
    }
    unlock_mutex_if_it_needs(&log_ctx);
}

/**
 * Производит hexdump буфера в строку (без символа перевода строки). Максимальная длина дампа - 16 байт
 *
 * @param buf_in      [in]  буфер, дамп котогого требуется сделать (!= NULL)
 * @param buf_in_size [in]  размер буфера в байтах
 * @param offset      [in]  смещение в буфере (байты / 16)
 * @param buf_out     [out] результирующий текстовый буфер с дампом строки. Минимальный размер - 100 байт (!= NULL)
 */
static
void compose_hexdump_line(const void *buf_in,
                          size_t      buf_in_size,
                          size_t      offset,
                          char       *buf_out)
{
    size_t i, j;
    size_t limit;

    const unsigned char *ptr = (const unsigned char *)buf_in;
    const char *cptr = (const char *)buf_in;

    assert(buf_in != NULL);
    assert(buf_out != NULL);

    ptr += offset*16;
    cptr += offset*16;

    sprintf(buf_out,"%."STRX(LOG_RAW_ADDR_FIELD_WIDTH)"zX  ", offset*16);
    buf_out += LOG_RAW_ADDR_FIELD_WIDTH + 2;

    /* если до конца буфера еще > 16 байт */
    if ((buf_in_size - (offset*16)) > 16)
    {
        limit = 16;
    }
    else
    {
        limit = buf_in_size - (offset*16);
    }
    /* HEX представления байт */
    for (i = 0 ; i < limit; i++)
    {
        sprintf(buf_out," %.2hhX",*ptr);
        ptr++;
        buf_out+= 3;
    }
    /* добиваем оставшуюся часть (если есть) пробелами */
    for (j = i ; j < 16; j++)
    {
        buf_out[0] = ' ';
        buf_out[1] = ' ';
        buf_out[2] = ' ';
        ptr++;
        buf_out+= 3;
    }
    buf_out[0] = ' ';
    buf_out[1] = '|';
    buf_out[2] = ' ';
    buf_out+= 3;

    /* текстовое представление байт */
    for (i = 0 ; i < limit; i++)
    {
        *buf_out = (char)(isprint(*cptr) ? (*cptr) : '.');
        cptr++;
        buf_out++;
    }
    /* не добиваем пробелами текстовое представление, так как это последний элемент строки */
    #if 0
    for (j = i ; j < 16; j++)
    {
        *buf_out = ' ';
        cptr++;
        buf_out++;
    }
    #endif
    *buf_out = 0;
    assert(strlen(buf_out) < 100);
}

/**
 * Логгирует RAW буфер.
 * Печатает prefix и время перед логом.
 *
 * @param source   [in] источник (строка - источника лога) (максимальная длина LOG_SRC_MAX_SIZE остальное обрезается) (!= NULL)
 * @param file     [in] имя файла  (максимальная длина LOG_FUNCTION_NAME_MAX_SIZE).
 * @param line     [in] номер строки в файле
 * @param function [in] имя функции (максимальная длина LOG_FILE_NAME_MAX_SIZE).
 * @param buffer   [in] указатель на буфер (может быть NULL)
 * @param length   [in] размер буфера в байтах
 */
extern
void log_raw(const char *source,
             const char *file,
             const char *line,
             const char *function,
             const void *buffer,
             size_t      length)
{
    size_t i = 0;
    char hexdump_line[100];

    assert(source != NULL);
    assert(file != NULL);
    assert(line != NULL);
    assert(function != NULL);

    if (log_ctx.initialized == false) return;
    lock_mutex_if_it_needs(&log_ctx);
    if (is_log_allowed(source, LL_RAW))
    {
        char prefix[128];

        snprintf(log_buf,
                 sizeof(log_buf),
                 "%s\n",
                 compose_log_prefix(prefix, sizeof(prefix), source, file, line, function, LL_RAW));
        fprintf(stderr,"%s",log_buf);
        if (buffer)
        {
            for (i = 0; i < length; i+=16)
            {
                compose_hexdump_line(buffer, length, i/16, hexdump_line);
                fprintf(stderr,"%s\n",hexdump_line);
            }
        }
        else
        {
            fprintf(stderr,"NULL\n");
        }
    }
    unlock_mutex_if_it_needs(&log_ctx);
}

/**
 * Преобразует строку в элемент множества log_level_t.
 * Нечувствительна к регистру.
 *
 * @param str [in] одна из строк  строк "ERROR", "WARNING", ...
 * @return log_level_t, соответствующий строке или LL_INVALID в случае неизвестной строки.
 */
extern
log_level_t log_str_to_ll(const char *str)
{
    log_level_t ll;

    assert(str != NULL);

    for (ll = LL_INVALID; ll < LL_CNT; ll++)
    {
        if (!strcasecmp(str, log_level_map[ll]))
        {
            return ll;
        }
    }
    return LL_INVALID;
}

/**
 * Преобразует элемент множества log_level_t в строковое представление.
 *
 * @param log_level [in] log level
 * @return строка, соответствующая log_level.
 */
extern
const char *log_ll_to_str(log_level_t log_level)
{
    if (log_level >= LL_INVALID && log_level < LL_CNT)
    {
        return log_level_map[log_level];
    }
    return log_level_map[LL_INVALID];
}

/**
 * Сообщает будет ли выведен лог от данного источника с данным уровнем.
 *
 * @param source [in] источник (строка - источника лога) (максимальная длина LOG_SRC_MAX_SIZE остальное обрезается) (!= NULL)
 * @param ll     [in] уровень выводимого лога (LL_INVALID < log_level < LL_CNT).
 *
 * @return true - будет выведен, false - не будет.
 */
extern
bool log_will_be_printed(const char  *source,
                         log_level_t  log_level)
{
    assert(source != NULL);

    bool res = false;
    lock_mutex_if_it_needs(&log_ctx);
    res = is_log_allowed(source, log_level);
    unlock_mutex_if_it_needs(&log_ctx);
    return res;
}

/**
 * Возвращает минимальный уровень логгирования для указанного источника.
 *
 * @param source [in] Источник (!= NULL).
 *
 * @return уровень логгирования для указанного источника, или LL_INVALID, если такого источника не зарегистрировано.
 */
extern
log_level_t log_get_src_level(const char *source)
{
    assert(source != NULL);
    log_source_hm_elt_t *elt = NULL;
    lock_mutex_if_it_needs(&log_ctx);
    /* найти в Хэше соответствующий источник */
    HASH_FIND_STR(log_ctx.source_hm, source, elt);
    log_level_t res = LL_INVALID;
    if (elt)
    {
        res =  elt->min_log_level;
    }
    unlock_mutex_if_it_needs(&log_ctx);
    return res;
}

/**
 * Возвращает глобальный уровень логирования.
 *
 * @return глобальный уровень логирования
 */
extern
log_level_t log_get_global_level(void)
{
    log_level_t res;
    lock_mutex_if_it_needs(&log_ctx);
    res = log_ctx.min_log_level;
    unlock_mutex_if_it_needs(&log_ctx);
    return res;
}

/**
 * Выполняет дамп источников лога.
 * Вызывающая сторона обязана после прекращения использования вызвать free().
 *
 * @return массив дескрипторов источников логгирования или NULL в случае ошибки.
 */
extern
log_src_dump_t *log_src_dump()
{
    log_src_dump_t *res = NULL;
    if (!log_ctx.initialized) return NULL;
    lock_mutex_if_it_needs(&log_ctx);
    size_t sz = HASH_COUNT(log_ctx.source_hm);
    res = malloc(sizeof(log_src_dump_t) + sz*sizeof(log_src_descr_t));
    if (!res)
    {
        unlock_mutex_if_it_needs(&log_ctx);
        return NULL;
    }
    res->global_level = log_ctx.min_log_level;
    res->num_log_src_descr = sz;
    const log_source_hm_elt_t *elt = log_ctx.source_hm;
    for (size_t i = 0 ; i < sz && elt ; i++)
    {
        res->log_src_descrs[i].source        = elt->source;
        res->log_src_descrs[i].min_log_level = elt->min_log_level;
        elt = elt->hh.next;
    }
    unlock_mutex_if_it_needs(&log_ctx);
    return res;
}

/**
 * Удаляет дамп источников лога, сгенерированных функцией log_src_dump().
 *
 * @param dump источников лога, результат log_src_dump() (может быть NULL)
 */
extern
void log_src_dump_delete(log_src_dump_t *dump) {
    if (dump)
    {
        free(dump);
    }
}
