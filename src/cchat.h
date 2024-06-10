
#ifndef CCHAT_H
#define CCHAT_H

#define CCHAT_LWS_BUFFER_SZ_MAX 4000

int cchat_auth_session_cb(
   struct WEBUTIL_PAGE* page, int* user_id_out_p,
   int session_id, int user_id,
   bstring hash, size_t hash_sz, bstring remote_host, time_t start_time );

int cchat_handle_req( struct CCHAT_OP_DATA* op );

int cchat_lws_cb(
   struct lws* wsi, enum lws_callback_reasons reason, void *user, void *in,
   size_t len );

#endif /* !CCHAT_H */

