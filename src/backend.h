#ifndef BACKEND_H
#define BACKEND_H

#include <stdbool.h>

/* Game state initialization */
bool backend_init(void);
void backend_shutdown(void);

/* API configuration */
bool backend_has_api(void);
void backend_set_api(const char *ep, const char *key, const char *md);
char *backend_get_api_status(void);   /* returns malloc'd JSON */
char *backend_get_profiles(void);     /* returns malloc'd JSON array */
char *backend_save_profile(const char *name, const char *ep,
                           const char *key, const char *md);
char *backend_delete_profile(const char *name);
char *backend_activate_profile(const char *name);

/* World creation */
char *backend_create_world(const char *name, const char *age,
                           const char *clothing, const char *money,
                           const char *app, const char *con, const char *intel,
                           const char *skills, const char *items,
                           const char *story);

/* Message / AI interaction */
char *backend_send_message(const char *text);   /* blocking! returns JSON */

/* Save / Load */
char *backend_quick_save(void);
char *backend_quick_load(void);
char *backend_list_saves(void);        /* returns JSON array */
char *backend_load_save(const char *filename);
char *backend_delete_save(const char *filename);

/* Travel */
char *backend_travel(const char *from, const char *to,
                     double dist, const char *method);

/* Get full game state (JSON) */
char *backend_get_state(void);

/* Check if world is ready */
bool backend_world_ready(void);

#endif
