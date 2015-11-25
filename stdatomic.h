#ifndef _STDATOMIC_H
#define _STDATOMIC_H

#if 0
#define __KEEP_alltypes_macros or something like that
#include <bits/alltypes.h>
#undef __KEEP_alltypes_macros
#else
#include <limits.h>
#if LONG_MAX == 0x7fffffffffffffff
#define _Int64 long
#else
#define _Int64 long long
#endif
#define _Addr long
#define _Wchar __typeof__(L'\0')
#endif

#ifndef __has_extension
#define __has_extension(x) 0
#define __musl_has_extension 1
#endif

#if defined(__clang__) && __has_extension(c_atomic)

#define kill_dependency(y) (y)

#define ATOMIC_VAR_INIT(value) (value)
#define atomic_init(obj, value) __c11_atomic_init(obj, value)

#define atomic_is_lock_free(obj) __c11_atomic_is_lock_free(sizeof(*(obj)))
#define __atomic_type_is_lock_free(type) \
    __c11_atomic_is_lock_free(sizeof(type))

#define atomic_thread_fence(order) __c11_atomic_thread_fence(order)
#define atomic_signal_fence(order) __c11_atomic_signal_fence(order)

#define atomic_store_explicit(object, desired, order) \
    __c11_atomic_store(object, desired, order)
#define atomic_load_explicit(object, order) \
    __c11_atomic_load(object, order)

#define atomic_exchange_explicit(object, value, order) \
    __c11_atomic_exchange(object, value, order)
#define atomic_compare_exchange_strong_explicit(object, expected, desired, \
                                                success, failure) \
    __c11_atomic_compare_exchange_strong(object, expected, desired, \
                                         success, failure)
#define atomic_compare_exchange_weak_explicit(object, expected, desired, \
                                              success, failure) \
    __c11_atomic_compare_exchange_weak(object, expected, desired, \
                                       success, failure)

#define atomic_fetch_add_explicit(object, operand, order) \
    __c11_atomic_fetch_add(object, operand, order)
#define atomic_fetch_sub_explicit(object, operand, order) \
    __c11_atomic_fetch_sub(object, operand, order)
#define atomic_fetch_or_explicit(object, operand, order) \
    __c11_atomic_fetch_or(object, operand, order)
#define atomic_fetch_xor_explicit(object, operand, order) \
    __c11_atomic_fetch_xor(object, operand, order)
#define atomic_fetch_and_explicit(object, operand, order) \
    __c11_atomic_fetch_and(object, operand, order)

#define atomic_flag_test_and_set_explicit(object, order) \
    __c11_atomic_exchange(&(object)->_Value, 1, order)
#define atomic_flag_clear_explicit(object, order) \
    __c11_atomic_store(&(object)->_Value, 0, order)

#elif (__GNUC__ == 4 && __GNUC_MINOR__ >= 9) || __GNUC__ > 4

#define ATOMIC_VAR_INIT(value) (value)
#define atomic_init(obj, value) do { *(obj) = (value); } while (0)

#define kill_dependency(y) __extension__({__auto_type __y = (y); __y;})

#define atomic_is_lock_free(obj) \
    __extension__({ \
        __auto_type __obj = (obj); \
        __atomic_is_lock_free(sizeof(*__obj), __obj); \
    })
#define __atomic_type_is_lock_free(type) \
    __atomic_always_lock_free(sizeof(type), (void *)0)

#define atomic_thread_fence(order) __atomic_thread_fence(order)
#define atomic_signal_fence(order) __atomic_signal_fence(order)

#define atomic_store_explicit(object, value, order) \
    __atomic_store_n(object, value, order)
#define atomic_load_explicit(object, order) \
    __atomic_load_n(object, order)

#define atomic_exchange_explicit(object, desired, order) \
    __atomic_exchange_n(object, desired, order)
#define atomic_compare_exchange_strong_explicit(object, expected, desired, \
                                                success, failure) \
    __atomic_compare_exchange_n(object, expected, desired, 0, success, failure)
#define atomic_compare_exchange_weak_explicit(object, expected, desired, \
                                              success, failure) \
    __atomic_compare_exchange_n(object, expected, desired, 1, success, failure)

#define atomic_fetch_add_explicit(object, operand, order) \
    __atomic_fetch_add(object, operand, order)
#define atomic_fetch_sub_explicit(object, operand, order) \
    __atomic_fetch_sub(object, operand, order)
#define atomic_fetch_or_explicit(object, operand, order) \
    __atomic_fetch_or(object, operand, order)
#define atomic_fetch_xor_explicit(object, operand, order) \
    __atomic_fetch_xor(object, operand, order)
#define atomic_fetch_and_explicit(object, operand, order) \
    __atomic_fetch_and(object, operand, order)

#define atomic_flag_test_and_set_explicit(object, order) \
    __atomic_test_and_set(&(object)->_Value, order)
#define atomic_flag_clear_explicit(object, order) \
    __atomic_clear(&(object)->_Value, order)

#elif __GNUC__ == 4 && __GNUC_MINOR__ >= 7

#define __NEED__Atomic

#define kill_dependency(y) (y)

#define ATOMIC_VAR_INIT(value) { value }
#define atomic_init(obj, value) do { (obj)->__value = (value); } while (0)

#define atomic_is_lock_free(obj) \
    __atomic_is_lock_free(sizeof((obj)->__value), (void *)0)
#define __atomic_type_is_lock_free(type) \
    __atomic_always_lock_free(sizeof(type), (void *)0)

#define atomic_thread_fence(order) __atomic_thread_fence(order)
#define atomic_signal_fence(order) __atomic_signal_fence(order)

#define atomic_store_explicit(object, value, order) \
    __atomic_store_n(&(object)->__value, value, order)
#define atomic_load_explicit(object, order) \
    __atomic_load_n(&(object)->__value, order)

#define atomic_exchange_explicit(object, desired, order) \
    __atomic_exchange_n(&(object)->__value, desired, order)
#define atomic_compare_exchange_strong_explicit(object, expected, desired, \
                                                success, failure) \
    __atomic_compare_exchange_n(&(object)->__value, expected, desired, \
                                0, success, failure)
#define atomic_compare_exchange_weak_explicit(object, expected, desired, \
                                              success, failure) \
    __atomic_compare_exchange_n(&(object)->__value, expected, desired, \
                                1, success, failure)

#define atomic_fetch_add_explicit(object, operand, order) \
    __atomic_fetch_add(&(object)->__value, operand, order)
#define atomic_fetch_sub_explicit(object, operand, order) \
    __atomic_fetch_sub(&(object)->__value, operand, order)
#define atomic_fetch_or_explicit(object, operand, order) \
    __atomic_fetch_or(&(object)->__value, operand, order)
#define atomic_fetch_xor_explicit(object, operand, order) \
    __atomic_fetch_xor(&(object)->__value, operand, order)
#define atomic_fetch_and_explicit(object, operand, order) \
    __atomic_fetch_and(&(object)->__value, operand, order)

#define atomic_flag_test_and_set_explicit(object, order) \
    __atomic_test_and_set(&(object)->_Value.__value, order)
#define atomic_flag_clear_explicit(object, order) \
    __atomic_clear(&(object)->_Value.__value, order)

#else

#error "Musl's stdatomic.h does not support your compiler"

#endif

#ifdef __musl_has_extension
#undef __musl_has_extension
#undef __has_extension
#endif

#ifdef __NEED__Atomic
#undef __NEED__Atomic
#define _Atomic(type) struct { type volatile __value; }
#endif

typedef enum {
    memory_order_relaxed = 0,
    memory_order_consume = 1,
    memory_order_acquire = 2,
    memory_order_release = 3,
    memory_order_acq_rel = 4,
    memory_order_seq_cst = 5
} memory_order;

#ifdef __cplusplus
typedef _Atomic(bool) atomic_bool;
#else
typedef _Atomic(_Bool) atomic_bool;
#endif
typedef _Atomic(char) atomic_char;
typedef _Atomic(signed char) atomic_schar;
typedef _Atomic(short) atomic_short;
typedef _Atomic(int) atomic_int;
typedef _Atomic(long) atomic_long;
typedef _Atomic(long long) atomic_llong;
typedef _Atomic(unsigned char) atomic_uchar;
typedef _Atomic(unsigned short) atomic_ushort;
typedef _Atomic(unsigned int) atomic_uint;
typedef _Atomic(unsigned long) atomic_ulong;
typedef _Atomic(unsigned long long) atomic_ullong;
typedef _Atomic(unsigned short) atomic_char16_t;
typedef _Atomic(unsigned int) atomic_char32_t;
#ifdef __cplusplus
typedef _Atomic(wchar_t) atomic_wchar_t;
#else
typedef _Atomic(_Wchar) atomic_wchar_t;
#endif
typedef _Atomic(signed char) atomic_int_least8_t;
typedef _Atomic(short) atomic_int_least16_t;
typedef _Atomic(int) atomic_int_least32_t;
typedef _Atomic(_Int64) atomic_int_least64_t;
typedef _Atomic(signed char) atomic_int_fast8_t;
typedef _Atomic(int) atomic_int_fast16_t;
typedef _Atomic(int) atomic_int_fast32_t;
typedef _Atomic(_Int64) atomic_int_fast64_t;
typedef _Atomic(unsigned char) atomic_uint_least8_t;
typedef _Atomic(unsigned short) atomic_uint_least16_t;
typedef _Atomic(unsigned int) atomic_uint_least32_t;
typedef _Atomic(unsigned _Int64) atomic_uint_least64_t;
typedef _Atomic(unsigned char) atomic_uint_fast8_t;
typedef _Atomic(unsigned int) atomic_uint_fast16_t;
typedef _Atomic(unsigned int) atomic_uint_fast32_t;
typedef _Atomic(unsigned _Int64) atomic_uint_fast64_t;
typedef _Atomic(_Addr) atomic_intptr_t;
typedef _Atomic(unsigned _Addr) atomic_uintptr_t;
typedef _Atomic(unsigned _Addr) atomic_size_t;
typedef _Atomic(_Addr) atomic_ptrdiff_t;
typedef _Atomic(_Addr) atomic_intmax_t;
typedef _Atomic(_Addr) atomic_uintmax_t;

#ifdef __cplusplus
#define ATOMIC_BOOL_LOCK_FREE __atomic_type_is_lock_free(bool)
#else
#define ATOMIC_BOOL_LOCK_FREE __atomic_type_is_lock_free(_Bool)
#endif
#define ATOMIC_CHAR_LOCK_FREE __atomic_type_is_lock_free(char)
#define ATOMIC_CHAR16_T_LOCK_FREE __atomic_type_is_lock_free(unsigned short)
#define ATOMIC_CHAR32_T_LOCK_FREE __atomic_type_is_lock_free(unsigned int)
#ifdef __cplusplus
#define ATOMIC_WCHAR_T_LOCK_FREE __atomic_type_is_lock_free(wchar_t)
#else
static const _Bool __ATOMIC_WCHAR_T_LOCK_FREE = __atomic_type_is_lock_free(_Wchar);
#define ATOMIC_WCHAR_T_LOCK_FREE __ATOMIC_WCHAR_T_LOCK_FREE
#endif
#define ATOMIC_SHORT_LOCK_FREE __atomic_type_is_lock_free(short)
#define ATOMIC_INT_LOCK_FREE __atomic_type_is_lock_free(int)
#define ATOMIC_LONG_LOCK_FREE __atomic_type_is_lock_free(long)
#define ATOMIC_LLONG_LOCK_FREE __atomic_type_is_lock_free(long long)
#define ATOMIC_POINTER_LOCK_FREE __atomic_type_is_lock_free(void *)

#define atomic_store(object, desired) \
    atomic_store_explicit(object, desired, memory_order_seq_cst)
#define atomic_load(object) \
    atomic_load_explicit(object, memory_order_seq_cst)

#define atomic_exchange(object, desired) \
    atomic_exchange_explicit(object, desired, memory_order_seq_cst)
#define atomic_compare_exchange_strong(object, expected, desired) \
    atomic_compare_exchange_strong_explicit(object, expected, desired, \
                                            memory_order_seq_cst, \
                                            memory_order_seq_cst)
#define atomic_compare_exchange_weak(object, expected, desired) \
    atomic_compare_exchange_weak_explicit(object, expected, desired, \
                                          memory_order_seq_cst, \
                                          memory_order_seq_cst)

#define atomic_fetch_add(object, operand) \
    atomic_fetch_add_explicit(object, operand, memory_order_seq_cst)
#define atomic_fetch_sub(object, operand) \
    atomic_fetch_sub_explicit(object, operand, memory_order_seq_cst)
#define atomic_fetch_or(object, operand) \
    atomic_fetch_or_explicit(object, operand, memory_order_seq_cst)
#define atomic_fetch_xor(object, operand) \
    atomic_fetch_xor_explicit(object, operand, memory_order_seq_cst)
#define atomic_fetch_and(object, operand) \
    atomic_fetch_and_explicit(object, operand, memory_order_seq_cst)

typedef struct { atomic_bool _Value; } atomic_flag;

#define ATOMIC_FLAG_INIT { ATOMIC_VAR_INIT(0) }

#define atomic_flag_test_and_set(object) \
    atomic_flag_test_and_set_explicit(object, memory_order_seq_cst)
#define atomic_flag_clear(object) \
    atomic_flag_clear_explicit(object, memory_order_seq_cst)

#undef _Int64
#undef _Addr
#undef _Reg
#undef _Wchar

#endif

