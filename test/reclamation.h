#ifndef SHIRKA_TEST_RECLAMATION_H
#define SHIRKA_TEST_RECLAMATION_H

#include <stddef.h>

void *sk_test_alloc (size_t size);
void sk_test_free (void *ptr);

#endif
