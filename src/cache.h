#ifndef MINI_DICT_CACHE_H
#define MINI_DICT_CACHE_H

#include <glib.h>

typedef struct _LookupCache LookupCache;

LookupCache *lookup_cache_new(GError **error);
void lookup_cache_free(LookupCache *cache);

gboolean lookup_cache_get(LookupCache *cache,
                          const char *query,
                          int max_age_days,
                          char **response_json,
                          GError **error);

gboolean lookup_cache_put(LookupCache *cache,
                          const char *query,
                          const char *response_json,
                          GError **error);

gboolean lookup_cache_clear(LookupCache *cache, GError **error);

#endif
