
#include "main.h"

#include <stdlib.h> /* for atoi() */

#include <openssl/rand.h>
#include <openssl/evp.h>

#define CHATDB_HASH_SZ 32
#define CHATDB_SALT_SZ 32
#define CHATDB_PASSWORD_ITER 2000

struct CHATDB_ARG {
   chatdb_iter_msg_cb_t cb_msg;
   chatdb_iter_user_cb_t cb_user;
   bstring page_text;
   bstring password_test;
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
         "user_name text not null, "
         "email text, "
         "hash text not null, "
         "salt text not null, "
         "iters integer not null, "
         "join_time datetime default current_timestamp );", NULL, 0, &err_msg );
   if( SQLITE_OK != retval ) {
      dbglog_error( "could not create database user table: %s\n", err_msg );
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
   bstring password, size_t password_iter,
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
   sqlite3* db, bstring user, bstring password, bstring* err_msg_p
) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;
   unsigned char* salt_str = NULL;
   unsigned char salt_bin[CHATDB_SALT_SZ] = { 0 };
   bstring hash = NULL;
   bstring salt = NULL;

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

   retval = chatdb_hash_password( password, CHATDB_PASSWORD_ITER, salt, &hash );
   if( retval ) {
      goto cleanup;
   }

   /* Store the user record. */
   query = sqlite3_mprintf(
      "insert into users "
      "(user_name, hash, salt, iters) "
      "values('%q', '%q', '%q', '%d')",
      bdata( user ), bdata( hash ), bdata( salt ), CHATDB_PASSWORD_ITER );
   if( NULL == query ) {
      dbglog_error( "could not allocate database user insert!\n" );
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

int chatdb_send_message( sqlite3* db, bstring msg, bstring* err_msg_p ) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;

   /* TODO: Actual to/from. */
   query = sqlite3_mprintf(
      "insert into messages "
      "(msg_type, user_from_id, room_or_user_to_id, msg_text) "
      "values(0, 0, 0, '%q')",
      bdata( msg ) );
   if( NULL == query ) {
      dbglog_error( "could not allocate database chat insert!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = sqlite3_exec( db, query, NULL, NULL, &err_msg );
   if( SQLITE_OK != retval ) {
      /* retval = RETVAL_DB; */
      retval = 0;
      *err_msg_p = bfromcstr( err_msg );
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
   bstring msg_text = NULL;

   msg_text = bfromcstr( argv[4] );
   if( NULL == msg_text ) { 
      dbglog_error( "error allocating msg_text!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   if( 6 > argc ) {
      retval = 1;
      goto cleanup;
   }

   retval = arg_struct->cb_msg(
      arg_struct->page_text,
      atoi( argv[0] ), /* msg_id */
      atoi( argv[1] ), /* msg_type */
      atoi( argv[2] ), /* user_from_id */
      atoi( argv[3] ), /* room_or_user_to_id */
      msg_text,
      atoi( argv[5] ) ); /* msg_time */

cleanup:

   if( NULL != msg_text ) {
      bdestroy( msg_text );
   }

   return retval;
}

int chatdb_iter_messages(
   bstring page_text, sqlite3* db,
   int msg_type, int dest_id, chatdb_iter_msg_cb_t cb, bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;

   arg_struct.cb_msg = cb;
   arg_struct.page_text = page_text;

   /* Create schema table if it doesn't exist. */
   retval = sqlite3_exec( db,
      "select msg_id, msg_type, user_from_id, room_or_user_to_id, "
         "msg_text, strftime('%s', msg_time) from messages",
      chatdb_dbcb_messages, &arg_struct, &err_msg );
   if( SQLITE_OK != retval ) {
      /* TODO: Return err_msg. */
      dbglog_error( "could not execute database message query: %s\n",
         err_msg );
      *err_msg_p = bfromcstr( err_msg );
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

   if( 6 > argc ) {
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

   salt = bfromcstr( argv[4] );
   if( NULL == salt ) { 
      dbglog_error( "error allocating salt!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   retval = arg_struct->cb_user(
      arg_struct->page_text,
      arg_struct->password_test,
      atoi( argv[0] ), /* user_id */
      user_name, /* user_name */
      email, /* email */
      hash, /* hash */
      salt, /* salt */
      atoi( argv[5] ), /* iters */
      atoi( argv[6] ) ); /* join_time */

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
   bstring page_text, sqlite3* db,
   bstring user, bstring password_test, chatdb_iter_user_cb_t cb,
   bstring* err_msg_p
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;
   char* query = NULL;
   char* dyn_query = NULL;

   arg_struct.cb_user = cb;
   arg_struct.page_text = page_text;
   arg_struct.password_test = password_test;

   if( NULL != user ) {
      dyn_query = sqlite3_mprintf(
         "select user_id, user_name, email, hash, salt, iters, "
            "strftime('%%s', join_time) from users where user_name = '%q'",
         bdata( user ) );
      query = dyn_query;
   } else {
      query = "select user_id, user_name, email, hash, salt, iters, "
         "strftime('%s', join_time) from users";
   }

   if( NULL == query ) {
      dbglog_error( "could not allocate database user select!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   /* Create schema table if it doesn't exist. */
   retval = sqlite3_exec( db, query, chatdb_dbcb_users, &arg_struct, &err_msg );
   if( SQLITE_OK != retval ) {
      /* TODO: Return err_msg. */
      dbglog_error(
         "could not execute database user query: %s\n", err_msg );
      *err_msg_p = bfromcstr( err_msg );
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

