#include <stddef.h> // for size_t and NULL
#include <stdio.h>
#include <stdlib.h> // for malloc
#include <string.h> // for strchr, strlen, strncpy

char *prefix_name(const char *def_name, const char *signal_name) {
  char *prefixed =
      malloc(strlen(def_name) + strlen(signal_name) + 2); // ':' + '\0'
  sprintf(prefixed, "%s:%s", def_name, signal_name);
  return prefixed;
}

char *extract_left_of_colon(const char *str) {
  if (!str)
    return NULL;

  const char *colon = strchr(str, ':');
  size_t len = colon ? (size_t)(colon - str) : strlen(str);

  char *left = malloc(len + 1);
  if (!left)
    return NULL;

  strncpy(left, str, len);
  left[len] = '\0';

  return left;
}
