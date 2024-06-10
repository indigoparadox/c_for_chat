
#ifndef CCHAT_H
#define CCHAT_H

#define CCHAT_LWS_BUFFER_SZ_MAX 4000

int cchat_handle_req( FCGX_Request* req, sqlite3* db );

int cchat_lws_cb(
   struct lws* wsi, enum lws_callback_reasons reason, void *user, void *in,
   size_t len );

#endif /* !CCHAT_H */

