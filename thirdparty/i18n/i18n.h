
#ifndef I18N_H
#define I18N_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define I18N_DEBUG (1)
#define I18N_FILE_MAX_SIZE (65536)

typedef struct i18n_handle i18n_handle_t;
i18n_handle_t *i18n_open(const char *dir, const char *lang);
const char *i18n_query(i18n_handle_t *h, const char *key);
void i18n_close(i18n_handle_t *h);
char *i18n_get_single_string(const char *dir, const char *lang, const char *key);

#ifdef __cplusplus
}
#endif

#endif // I18N_H
