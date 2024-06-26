
#ifndef RTPROTO_H
#define RTPROTO_H

#define RTPROTO_BUFFER_CT 10

struct RTPROTO_CLIENT {
   struct lws* wsi;
   struct bstrList* buffer;
   pthread_mutex_t buffer_mutex;
   int auth_user_id;
};

int rtproto_command( struct CCHAT_OP_DATA* op, int user_id, bstring line );

int rtproto_client_add(
   struct CCHAT_OP_DATA* op, int auth_user_id, ssize_t* out_idx_p );

int rtproto_client_write_all( struct CCHAT_OP_DATA* op, bstring buffer );

int rtproto_client_delete( struct CCHAT_OP_DATA* op, size_t idx );

#endif /* !RTPROTO_H */

