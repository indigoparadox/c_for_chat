
#include "main.h"

#include <stdlib.h> /* for atoi() */

#include <openssl/rand.h>
#include <openssl/evp.h>

#define CHATDB_HASH_SZ 32
#define CHATDB_SESSION_HASH_SZ 32
#define CHATDB_SALT_SZ 32
#define CHATDB_PASSWORD_ITER 2000

#define chatdb_argv( field_name, idx ) \
   field_name = bfromcstr( argv[idx] ); \
   if( NULL == field_name ) { \
      dbglog_error( "error allocating " #field_name "!\n" ); \
      retval = RETVAL_ALLOC; \
      goto cleanup; \
   }

struct CHATDB_ARG {
   chatdb_iter_msg_cb_t cb_msg;
   chatdb_iter_user_cb_t cb_user;
   chatdb_iter_session_cb_t cb_session;
   struct WEBUTIL_PAGE* page;
   bstring password_test;
   int* user_id_out_p;
   FCGX_Request* req;
};

int chatdb_init( bstring path, sqlite3** db_p ) {
   int retval = 0;
   char* err_msg = NULL;

   retval = sqlite3_open( bdata( path ), db_p );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not open database!\n" );
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* Create message table if it doesn't exist. */
   retval = sqlite3_exec( *db_p,
      "create table if not exists messages( "
         "msg_id integer primary key,"
         "msg_type integer not null,"
         "user_from_id integer not null,"
         "room_or_user_to_id integer not null,"
         "msg_text text,"
         "msg_time datetime default current_timestamp );", NULL, 0, &err_msg );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not create database message table: %s\n", err_msg );
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* Create user table if it doesn't exist. */
   retval = sqlite3_exec( *db_p,
      "create table if not exists users( "
         "user_id integer primary key, "
         "user_name text not null unique, "
         "email text, "
         "hash text not null, "
         "hash_sz integer not null, "
         "salt text not null, "
         "iters integer not null, "
         "join_time datetime default current_timestamp );", NULL, 0, &err_msg );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not create database user table: %s\n", err_msg );
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* Create user table if it doesn't exist. */
   retval = sqlite3_exec( *db_p,
      "create table if not exists sessions( "
         "session_id integer primary key, "
         "user_id integer not null, "
         "hash text not null, "
         "hash_sz integer not null, "
         "remote_host text not null, "
         "start_time datetime default current_timestamp );",
      NULL, 0, &err_msg );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not create database session table: %s\n", err_msg );
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* Create schema table if it doesn't exist. */
   retval = sqlite3_exec( *db_p,
      "create table if not exists chat_schema( version int );",
      NULL, 0, &err_msg );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not create database system table: %s\n", err_msg );
      retval = RETVAL_DB;
      sqlite3_free( err_msg );
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   return retval;
}

void chatdb_close( sqlite3** db_p ) {

   sqlite3_close( *db_p );
   *db_p = NULL;

}

int chatdb_b64_decode( bstring in, unsigned char** out_p, size_t* out_sz_p ) {
   int retval = 0;
   size_t decoded_sz = 0;

   /* Base64-decode the salt. */
   *out_sz_p = 3 * blength( in ) / 4;
   *out_p = calloc( *out_sz_p, 1 );
   if( NULL == *out_p ) {
      dbglog_error( "could not allocate decoded str!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   /* TODO: Buffer size limit? */
   decoded_sz = EVP_DecodeBlock(
      *out_p, (unsigned char*)bdata( in ), blength( in ) );
   if( decoded_sz != *out_sz_p ) {
      dbglog_error( "predicted sz: %lu but was: %lu\n",
         *out_sz_p, decoded_sz );
   }

cleanup:

   return retval;
}

int chatdb_b64_encode( unsigned char* in, size_t in_sz, bstring* out_p ) {
   int retval = 0;
   unsigned char* str = NULL;
   size_t str_sz = 0;
   size_t encoded_sz = 0;

   /* Base64-encode the hash. */
   str_sz = 4 * ((in_sz + 2) / 3);
   str = calloc( str_sz + 1, 1 );
   if( NULL == str ) {
      dbglog_error( "could not allocate hash encoded str!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   /* TODO: Buffer size limit? */
   encoded_sz = EVP_EncodeBlock( str, in, in_sz );
   if( encoded_sz != str_sz ) {
      dbglog_error( "predicted hash sz: %lu but was: %lu\n",
         str_sz, encoded_sz );
   }

   *out_p = bfromcstr( (char*)str );
   if( NULL == *out_p ) {
      dbglog_error( "could not allocate encoded bstring!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }
 
cleanup:

   if( NULL != str ) {
      free( str );
   }

   return retval;
}

int chatdb_hash_password(
   bstring password, size_t password_iter, size_t hash_sz,
   bstring salt, bstring* hash_out_p
) {
   unsigned char hash_bin[CHATDB_HASH_SZ] = { 0 };
   size_t salt_bin_sz = 0;
   int retval = 0;
   unsigned char* salt_bin = NULL;

   retval = chatdb_b64_decode( salt, &salt_bin, &salt_bin_sz );
   if( retval ) {
      goto cleanup;
   }

   /* Generate the hash. */
   if( !PKCS5_PBKDF2_HMAC(
      bdata( password ), blength( password ),
      salt_bin, salt_bin_sz, password_iter, EVP_sha256(),
      CHATDB_HASH_SZ, hash_bin )
   ) {
      dbglog_error( "error generating password hash!\n" );
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* TODO: Push hash size in DB. */
   retval = chatdb_b64_encode( hash_bin, CHATDB_HASH_SZ, hash_out_p );
   if( retval ) {
      goto cleanup;
   }

cleanup:

   if( NULL != salt_bin ) {
      free( salt_bin );
   }

   return retval;
}

int chatdb_add_user(
   sqlite3* db, int user_id, bstring user, bstring password, bstring email,
   bstring* err_msg_p
) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;
   unsigned char* salt_str = NULL;
   unsigned char salt_bin[CHATDB_SALT_SZ] = { 0 };
   bstring hash = NULL;
   bstring salt = NULL;

   if( 0 == blength( user ) ) {
      *err_msg_p = bfromcstr( "Username cannot be empty!" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   dbglog_error( "%d\n", user_id );

   if( 0 > user_id && 0 == blength( password ) ) {
      *err_msg_p = bfromcstr( "New password cannot be empty!" );
      retval = RETVAL_PARAMS;
      goto cleanup;
   }

   if( 0 > user_id || 0 < blength( password ) ) {
   
      /* Generate a new salt. */
      if( 1 != RAND_bytes( salt_bin, CHATDB_SALT_SZ ) ) {
         dbglog_error( "error generating random bytes!\n" );
         retval = RETVAL_DB;
         goto cleanup;
      }

      retval = chatdb_b64_encode( salt_bin, CHATDB_SALT_SZ, &salt );
      if( retval ) {
         goto cleanup;
      }

      /* Hash the provided password. */
      retval = chatdb_hash_password(
         password, CHATDB_PASSWORD_ITER, CHATDB_HASH_SZ, salt, &hash );
      if( retval ) {
         goto cleanup;
      }

   }

   if( 0 > user_id ) {
      /* Generate an "add user" query. */
      query = sqlite3_mprintf(
         "insert into users "
         "(user_name, email, hash, hash_sz, salt, iters) "
         "values('%q', '%q', '%q', '%d', '%q', '%d')",
         bdata( user ), bdata( email ), bdata( hash ), CHATDB_HASH_SZ,
         bdata( salt ), CHATDB_PASSWORD_ITER );

      dbglog_debug( 1, "attempting to add user %s...\n", bdata( user ) );
   } else {
      /* Generate an "edit user" query. */
      /* TODO: Add password if provided. */
      query = sqlite3_mprintf(
         "update users set "
            "user_name = '%q', "
            "email = '%q' "
            "where user_id = '%d'",
         bdata( user ), bdata( email ), user_id );

      dbglog_debug( 1, "updating user %d...\n", user_id );
   }

   /* Check query. */
   if( NULL == query ) {
      dbglog_error( "could not allocate database user insert!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = sqlite3_exec( db, query, NULL, NULL, &err_msg );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      goto cleanup;
   }

cleanup:

   if( NULL != hash ) {
      bdestroy( hash );
   }

   if( NULL != salt ) {
      bdestroy( salt );
   }

   if( NULL != salt_str ) {
      free( salt_str );
   }

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}

int chatdb_send_message(
   sqlite3* db, int user_id, bstring msg, bstring* err_msg_p
) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;

   /* TODO: Actual to/from. */
   query = sqlite3_mprintf(
      "insert into messages "
      "(msg_type, user_from_id, room_or_user_to_id, msg_text) "
      "values(0, '%d', 0, '%q')",
      user_id, bdata( msg ) );
   if( NULL == query ) {
      dbglog_error( "could not allocate database chat insert!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = sqlite3_exec( db, query, NULL, NULL, &err_msg );
   if( SQLITE_OK != retval ) {
      /* retval = RETVAL_DB; */
      retval = 0;
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      goto cleanup;
   }

cleanup:

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}

static
int chatdb_dbcb_messages( void* arg, int argc, char** argv, char **col ) {
   struct CHATDB_ARG* arg_struct = (struct CHATDB_ARG*)arg;
   int retval = 0;
   bstring user_name = NULL;
   bstring msg_text = NULL;

   if( 6 > argc ) {
      retval = 1;
      goto cleanup;
   }

   chatdb_argv( user_name, 2 );
   chatdb_argv( msg_text, 4 );

   retval = arg_struct->cb_msg(
      arg_struct->page,
      atoi( argv[0] ), /* msg_id */
      atoi( argv[1] ), /* msg_type */
      user_name, /* user_from_id */
      atoi( argv[3] ), /* room_or_user_to_id */
      msg_text,
      atoi( argv[5] ) ); /* msg_time */

cleanup:

   if( NULL != msg_text ) {
      bdestroy( msg_text );
   }

   if( NULL != user_name ) {
      bdestroy( user_name );
   }

   return retval;
}

int chatdb_iter_messages(
   struct WEBUTIL_PAGE* page, sqlite3* db,
   int msg_type, int dest_id, chatdb_iter_msg_cb_t cb, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;

   arg_struct.cb_msg = cb;
   arg_struct.page = page;

   retval = sqlite3_exec( db,
      "select m.msg_id, m.msg_type, u.user_name, m.room_or_user_to_id, "
         "m.msg_text, strftime('%s', m.msg_time) from messages m "
         "inner join users u on u.user_id = m.user_from_id",
      chatdb_dbcb_messages, &arg_struct, &err_msg );
   if( SQLITE_OK != retval ) {
      /* TODO: Return err_msg. */
      dbglog_error( "could not execute database message query: %s\n",
         err_msg );
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}

static
int chatdb_dbcb_users( void* arg, int argc, char** argv, char **col ) {
   struct CHATDB_ARG* arg_struct = (struct CHATDB_ARG*)arg;
   int retval = 0;
   bstring user_name = NULL;
   bstring email = NULL;
   bstring hash = NULL;
   bstring salt = NULL;

   if( 7 > argc ) {
      dbglog_error( "incorrect number of user fields!\n" );
      retval = 1;
      goto cleanup;
   }

   user_name = bfromcstr( argv[1] );
   if( NULL == user_name ) { 
      dbglog_error( "error allocating user_name!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   email = bfromcstr( argv[2] );
   if( NULL == email && NULL != argv[2] ) { 
      dbglog_error( "error allocating email!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   hash = bfromcstr( argv[3] );
   if( NULL == hash ) { 
      dbglog_error( "error allocating hash!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   salt = bfromcstr( argv[5] );
   if( NULL == salt ) { 
      dbglog_error( "error allocating salt!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = arg_struct->cb_user(
      arg_struct->page,
      arg_struct->req,
      arg_struct->password_test,
      arg_struct->user_id_out_p,
      atoi( argv[0] ), /* user_id */
      user_name, /* user_name */
      email, /* email */
      hash, /* hash */
      atoi( argv[4] ),
      salt, /* salt */
      atoi( argv[6] ), /* iters */
      atoi( argv[7] ) ); /* join_time */

cleanup:

   if( NULL != user_name ) {
      bdestroy( user_name );
   }

   if( NULL != email ) {
      bdestroy( email );
   }

   if( NULL != hash ) {
      bdestroy( hash );
   }

   if( NULL != salt ) {
      bdestroy( salt );
   }

   return retval;
}

int chatdb_iter_users(
   struct WEBUTIL_PAGE* page, sqlite3* db, FCGX_Request* req,
   bstring user_name, int user_id, bstring password_test, int* user_id_out_p,
   chatdb_iter_user_cb_t cb, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;
   char* query = NULL;
   char* dyn_query = NULL;

   arg_struct.cb_user = cb;
   arg_struct.page = page;
   arg_struct.password_test = password_test;
   arg_struct.user_id_out_p = user_id_out_p;
   arg_struct.req = req;

   if( NULL != user_name ) {
      assert( 0 > user_id );
      dyn_query = sqlite3_mprintf(
         "select user_id, user_name, email, hash, hash_sz, salt, iters, "
            "strftime('%%s', join_time) from users where user_name = '%q'",
         bdata( user_name ) );
      query = dyn_query;
   } else if( 0 <= user_id ) {
      dyn_query = sqlite3_mprintf(
         "select user_id, user_name, email, hash, hash_sz, salt, iters, "
            "strftime('%%s', join_time) from users where user_id = %d",
         user_id );
      query = dyn_query;
   } else {
      query = "select user_id, user_name, email, hash, hash_sz, salt, iters, "
         "strftime('%s', join_time) from users";
   }

   if( NULL == query ) {
      dbglog_error( "could not allocate database user select!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = sqlite3_exec( db, query, chatdb_dbcb_users, &arg_struct, &err_msg );
   if( SQLITE_OK != retval ) {
      /* Return err_msg. */
      dbglog_error(
         "could not execute database user query: %s\n", err_msg );
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   if( NULL != dyn_query ) {
      sqlite3_free( dyn_query );
   }

   return retval;
}

int chatdb_add_session(
   sqlite3* db, int user_id, bstring remote_host, bstring* hash_p,
   bstring* err_msg_p
) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;
   unsigned char hash_bin[CHATDB_SESSION_HASH_SZ] = { 0 };

   /* Generate a new salt. */
   if( 1 != RAND_bytes( hash_bin, CHATDB_SESSION_HASH_SZ ) ) {
      dbglog_error( "error generating random bytes!\n" );
      retval = RETVAL_DB;
      goto cleanup;
   }

   retval = chatdb_b64_encode( hash_bin, CHATDB_SESSION_HASH_SZ, hash_p );
   if( retval ) {
      goto cleanup;
   }

   /* Trim off trailing '='. */
   while( BSTR_ERR != bstrrchr( *hash_p, '=' ) ) {
      retval = btrunc( *hash_p, blength( *hash_p ) - 1 );
      if( BSTR_ERR == retval ) {
         dbglog_error( "error truncating hash!\n" );
         retval = RETVAL_ALLOC;
         goto cleanup;
      }
   }
   retval = 0;

   /* Store the user record. */
   query = sqlite3_mprintf(
      "insert into sessions "
      "(user_id, hash, hash_sz, remote_host) "
      "values(%d, '%q', %d, '%q')",
      user_id, bdata( *hash_p ), CHATDB_SESSION_HASH_SZ, bdata( remote_host ) );
   if( NULL == query ) {
      dbglog_error( "could not allocate database session insert!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = sqlite3_exec( db, query, NULL, NULL, &err_msg );
   if( SQLITE_OK != retval ) {
      retval = RETVAL_DB;
      *err_msg_p = bfromcstr( err_msg );
      goto cleanup;
   }

cleanup:

   return retval;
}

static
int chatdb_dbcb_sessions( void* arg, int argc, char** argv, char **col ) {
   struct CHATDB_ARG* arg_struct = (struct CHATDB_ARG*)arg;
   int retval = 0;
   bstring hash = NULL;
   bstring remote_host = NULL;

   if( 6 > argc ) {
      retval = 1;
      goto cleanup;
   }

   chatdb_argv( hash, 2 );
   chatdb_argv( remote_host, 4 );

   retval = arg_struct->cb_session(
      arg_struct->page,
      arg_struct->user_id_out_p,
      atoi( argv[0] ), /* session_id */
      atoi( argv[1] ), /* user_id */
      hash,
      atoi( argv[3] ), /* hash_sz */
      remote_host,
      atoi( argv[5] ) ); /* start_time */

cleanup:

   if( NULL != hash ) {
      bdestroy( hash );
   }

   if( NULL != remote_host ) {
      bdestroy( remote_host );
   }

   return retval;
}

int chatdb_iter_sessions(
   struct WEBUTIL_PAGE* page, int* user_id_out_p, sqlite3* db,
   bstring hash, bstring remote_host,
   chatdb_iter_session_cb_t cb, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;
   char* query = NULL;

   arg_struct.cb_session = cb;
   arg_struct.page = page;
   arg_struct.user_id_out_p = user_id_out_p;

   query = sqlite3_mprintf(
      "select session_id, user_id, hash, hash_sz, remote_host, "
         "strftime('%%s', start_time) from sessions "
         "where hash = '%q' and remote_host = '%q'",
      bdata( hash ), bdata( remote_host ) );
   if( NULL == query ) {
      dbglog_error( "could not allocate database session select!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = sqlite3_exec(
      db, query, chatdb_dbcb_sessions, &arg_struct, &err_msg );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not execute database session query: %s\n",
         err_msg );
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;
}


int chatdb_remove_session(
   struct WEBUTIL_PAGE* page, sqlite3* db, bstring hash, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   char* query = NULL;

   query = sqlite3_mprintf(
      "delete from sessions where hash = '%q'", bdata( hash ) );
   if( NULL == query ) {
      dbglog_error( "could not allocate database session delete!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = sqlite3_exec( db, query, NULL, NULL, &err_msg );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not execute database session delete: %s\n",
         err_msg );
      if( NULL != err_msg_p ) {
         *err_msg_p = bfromcstr( err_msg );
      }
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* We got this far, so everything's OK? */
   retval = 0;

cleanup:

   if( NULL != query ) {
      sqlite3_free( query );
   }

   if( NULL != err_msg ) {
      sqlite3_free( err_msg );
   }

   return retval;

}

