#ifndef PLIBC_STDBOOL_H
#define PLIBC_STDBOOL_H

#if defined(_Bool)
#    define bool _Bool
#elif defined(__bool_true_false_are_defined)
#    define bool bool
#else
#    define bool unsigned char
#endif

#if !defined(__bool_true_false_are_defined)
#    define true 1
#    define false 0
#    define __bool_true_false_are_defined 1
#endif

#endif /* PLIBC_STDBOOL_H */