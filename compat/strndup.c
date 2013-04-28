#include <string.h>
#include <stdlib.h>

#include "strndup.h"
#include "strnlen.h"

char *
strndup (const char *s, size_t n)
{
  size_t len = strnlen (s, n);
  char *new = malloc (len + 1);

  if (new == NULL)
    return NULL;

  new[len] = '\0';
  return memcpy (new, s, len);
}
