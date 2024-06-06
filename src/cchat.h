
#ifndef CCHAT_H
#define CCHAT_H

int cchat_query_key( struct bstrList* array, const char* key, bstring* val_p );

int cchat_handle_req( FCGX_Request* req, sqlite3* db );

#endif /* !CCHAT_H */

