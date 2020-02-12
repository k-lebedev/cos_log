#ifndef LOG_H_
#define LOG_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#include "macros.h"

/**
 * Множество уровней логгирования
 */
typedef enum tag_log_level
{
    LL_INVALID,

    LL_RAW,
    LL_TRACE,
    LL_DEBUG,
    LL_INFO,
    LL_WARNING,
    LL_ERROR,
    LL_NONE,

    LL_CNT
}
log_level_t;

/**
 * Дескриптор источника лога.
 */
typedef struct tag_log_src_descr
{
    const char  *source;        ///< Источник лога (максимальная длина LOG_SRC_MAX_SIZE остальное обрезается) (!= NULL).
    log_level_t  min_log_level; ///< Минимальный уровень выводимого лога (LL_NONE - отключает вывод).
}
log_src_descr_t;

/**
 * Дамп источников лога.
 * Должен быть освобождён функцией log_src_dump_delete().
 */
typedef struct tag_log_src_dump
{
    log_level_t     global_level;      ///< Минимальный уровень выводимого лога (LL_NONE - отключает вывод).
    size_t          num_log_src_descr; ///< Количество дескрипторов источника лога.
    log_src_descr_t log_src_descrs[];  ///< Массив дескрипторов источников лога.
}
log_src_dump_t;

#ifdef __cplusplus
extern "C" {
#endif

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
              bool        is_thread_safe);

/**
 * Устанавливает глобальный уровень логгирования.
 *
 * @param min_log_level [in] глобально (для всех источников) минимально выводимый уровень логов (LL_NONE - отключает вывод).
 * @return true - OK, false - Fail
 */
extern
bool log_set_log_level(log_level_t min_log_level) __attribute__((warn_unused_result));

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
                  log_level_t  min_log_level) __attribute__((nonnull(1)));

/**
 * Регистрирует новые источники лога в системе логгирования.
 * Если один из источников уже зерегистрирован, он перезаписывается.
 * @param descr      [in] Массив дескрипторов источника лога.
 * @param num_descrs [in] Количество элементов в массиве num_descrs.
 * @return true - OK, false - не удалось зарегистрировать хотябы 1 источник.
 */
extern
bool log_register_ex(const log_src_descr_t *descr,
                     size_t                 num_descrs);

/**
 * Удаляет регистрацию источника (если зарегистрирован) в системе логгирования.
 *
 * @param source [in] источник лога (!= NULL)
 */
extern
void log_unregister(const char *source) __attribute__((nonnull(1)));

/**
 * Удаляет регистрацию всех источников в системе логгирования.
 *
 * @return true - OK, false -Fail
 */
extern
bool log_destroy();

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
             const char  *fmt, ...) __attribute__((format(printf, 6, 7), nonnull(1, 2, 3, 4, 6)));

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
             size_t      length) __attribute__((nonnull(1, 2, 3, 4)));

/**
 * Преобразует строку в элемент множества log_level_t.
 * Нечувствительна к регистру.
 *
 * @param str [in] одна из строк  строк "ERROR", "WARNING", ...
 * @return log_level_t, соответствующий строке или LL_INVALID в случае неизвестной строки.
 */
extern
log_level_t log_str_to_ll(const char *str) __attribute__((nonnull(1))) __attribute__((warn_unused_result));

/**
 * Преобразует элемент множества log_level_t в строковое представление.
 *
 * @param log_level [in] log level
 * @return строка, соответствующая log_level.
 */
extern
const char *log_ll_to_str(log_level_t log_level) __attribute__((warn_unused_result));

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
                         log_level_t  log_level) __attribute__((nonnull(1))) __attribute__((warn_unused_result));

/**
 * Возвращает минимальный уровень логгирования для указанного источника.
 *
 * @param source [in] Источник (!= NULL).
 *
 * @return уровень логгирования для указанного источника, или LL_INVALID, если такого источника не зарегистрировано.
 */
extern
log_level_t log_get_src_level(const char *source) __attribute__((nonnull(1))) __attribute__((warn_unused_result));

/**
 * Возвращает глобальный уровень логирования.
 *
 * @return глобальный уровень логирования
 */
extern
log_level_t log_get_global_level(void) __attribute__((warn_unused_result));

/**
 * Выполняет дамп источников лога.
 * Вызывающая сторона обязана после прекращения использования вызвать free().
 *
 * @return массив дескрипторов источников логгирования или NULL в случае ошибки.
 */
extern
log_src_dump_t *log_src_dump() __attribute__((warn_unused_result));

/**
 * Удаляет дамп источников лога, сгенерированных функцией log_src_dump().
 *
 * @param dump источников лога, результат log_src_dump() (может быть NULL)
 */
extern
void log_src_dump_delete(log_src_dump_t *dump);

#ifdef __cplusplus
}
#endif

/**
 * Источник лога по-умолчанию.
 * Перед использованием логгирующих макросов стоит сделать #define _LOG_SRC нужной строкой.
 */
#ifndef _LOG_SRC
#error "_LOG_SRC is not defined"
#endif

/**
 * Логгирующие макросы
 */
#define _LOG_RAW(buf,len) log_raw(_LOG_SRC, __FILE__, STRX(__LINE__), __FUNCTION__, buf, len)
#define _LOG_LEVEL(level, ...) log_log(_LOG_SRC, __FILE__, STRX(__LINE__), __FUNCTION__, level, __VA_ARGS__)
#define _LOG_TRACE(...)   _LOG_LEVEL(LL_TRACE,   __VA_ARGS__)
#define _LOG_DEBUG(...)   _LOG_LEVEL(LL_DEBUG,   __VA_ARGS__)
#define _LOG_INFO(...)    _LOG_LEVEL(LL_INFO,    __VA_ARGS__)
#define _LOG_WARNING(...) _LOG_LEVEL(LL_WARNING, __VA_ARGS__)
#define _LOG_ERROR(...)   _LOG_LEVEL(LL_ERROR,   __VA_ARGS__)

/**
 * Расширенный вывод лог сообщения об ошибке с кодом err_code и текстовым описанием err_text.
 */
#define _LOG_ERROR_EX(err_code, err_text, msg, ...) \
    _LOG_ERROR(msg" :[%6d]: %s", ##__VA_ARGS__, err_code, err_text)

/**
 * Использовать в случаях, когда нужно вывести errno
 */
#define _LOG_ERROR_ERRNO(msg, ...) \
    _LOG_ERROR_EX(errno, strerror(errno), msg, ##__VA_ARGS__)

/**
 * Использовать в случаях, когда нужно вывести строку с ошибкой, код которой явно передается в макрос
 */
#define _LOG_ERROR_ERRNO2(msg, err_status, ...) \
    _LOG_ERROR_EX(err_status, strerror(err_status), msg, ##__VA_ARGS__)

/**
 * Проверяет будет ли напечатан лог заданного уровня из текущего источника
 */
#define _LOG_WILL_BE_PRINTED(log_level) log_will_be_printed(_LOG_SRC, log_level)

#endif /* LOG_H_ */
