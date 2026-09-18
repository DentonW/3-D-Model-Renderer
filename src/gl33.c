#include "gl33.h"

/* The header aliases every GL name to its gl33_ pointer, but that does not
 * interfere here: an argument used only with # or ## is substituted without
 * being macro-expanded first, so X(glEnable) still yields the pointer
 * gl33_glEnable and the literal "glEnable" to look up. */

#define GL33_DEFINE(name) PFN_##name gl33_##name = 0;
GL33_FUNCTIONS(GL33_DEFINE)
#undef GL33_DEFINE

const char *gl33_load(void *(*getproc)(const char *)) {
#define GL33_LOAD(name)                                                              \
  gl33_##name = (PFN_##name)getproc(#name);                                          \
  if (!gl33_##name) return #name;

  GL33_FUNCTIONS(GL33_LOAD)
#undef GL33_LOAD
  return 0;
}
