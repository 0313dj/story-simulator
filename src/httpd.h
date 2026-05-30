#ifndef HTTPD_H
#define HTTPD_H

/* Start the HTTP server on the given port.
   Returns 0 on success, -1 on error. */
int httpd_start(int port);

/* Try ports 8765-8775, return the port used or -1. */
int httpd_autostart(void);

/* Stop the server. */
void httpd_stop(void);

#endif
