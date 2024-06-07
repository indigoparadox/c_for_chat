
#include "main.h"

#include <stdlib.h> /* for atoi() */

#include <openssl/rand.h>
#include <openssl/evp.h>

#define CHATDB_HASH_SZ 32
#define CHATDB_SALT_SZ 32

struct CHATDB_ARG {
   chatdb_iter_msg_cb_t cb_msg;
   chatdb_iter_user_cb_t cb_user;
   bstring page_text;
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

int chatdb_add_user(
   sqlite3* db, bstring user, bstring password, bstring* err_msg_p
) {
   int retval = 0;
   char* query = NULL;
   char* err_msg = NULL;
   unsigned char* hash_str = NULL;
   size_t hash_str_sz = 0;
   unsigned char* salt_str = NULL;
   size_t encoded_sz = 0;
   size_t salt_str_sz = 0;
   int password_iter = 2000;
   unsigned char salt[CHATDB_SALT_SZ] = { 0 };
   unsigned char hash[CHATDB_HASH_SZ] = { 0 };

   /* Generae a new salt. */
   if( 1 != RAND_bytes( salt, CHATDB_SALT_SZ ) ) {
      dbglog_error( "error generating random bytes!\n" );
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* Generate the hash. */
   if( !PKCS5_PBKDF2_HMAC(
      bdata( password ), blength( password ),
      salt, CHATDB_SALT_SZ, password_iter, EVP_sha256(),
      CHATDB_HASH_SZ, hash )
   ) {
      dbglog_error( "error generating password hash!\n" );
      retval = RETVAL_DB;
      goto cleanup;
   }

   /* Base64-encode the hash. */
   hash_str_sz = 4 * ((CHATDB_HASH_SZ + 2) / 3);
   hash_str = calloc( CHATDB_HASH_SZ + 1, 1 );
   if( NULL == hash_str ) {
      dbglog_error( "could not allocate hash encoded str!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   /* TODO: Buffer size limit? */
   encoded_sz = EVP_EncodeBlock( hash_str, hash, CHATDB_HASH_SZ );
   if( encoded_sz != hash_str_sz ) {
      dbglog_error( "predicted hash sz: %lu but was: %lu\n",
         hash_str_sz, encoded_sz );
   }

   /* Base64-encode the salt. */
   salt_str_sz = 4 * ((CHATDB_SALT_SZ + 2) / 3);
   salt_str = calloc( salt_str_sz + 1, 1 );
   if( NULL == salt_str ) {
      dbglog_error( "could not allocate hash encoded str!\n" );
      retval = RETVAL_ALLOC;
      goto cleanup;
   }

   /* TODO: Buffer size limit? */
   encoded_sz = EVP_EncodeBlock( salt_str, salt, CHATDB_SALT_SZ );
   if( encoded_sz != hash_str_sz ) {
      dbglog_error( "predicted salt sz: %lu but was: %lu\n",
         hash_str_sz, encoded_sz );
   }

   /* Store the user record. */
   query = sqlite3_mprintf(
      "insert into users "
      "(user_name, hash, salt, iters) "
      "values('%q', '%q', '%q', '%d')",
      bdata( user ), hash_str, salt_str, password_iter );
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

   if( NULL != salt_str ) {
      free( salt_str );
   }

   if( NULL != hash_str ) {
      free( hash_str );
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
   int msg_type, int dest_id, chatdb_iter_msg_cb_t cb
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
      dbglog_error( "could not execute database message query!\n" );
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
   if( NULL == email ) { 
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
   bstring user, chatdb_iter_user_cb_t cb
) {
   int retval = 0;
   char* err_msg = NULL;
   struct CHATDB_ARG arg_struct;

   arg_struct.cb_user = cb;
   arg_struct.page_text = page_text;

   /* Create schema table if it doesn't exist. */
   retval = sqlite3_exec( db,
      "select user_id, user_name, email, hash, salt, iters "
         "strftime('%s', join_time) from users",
      chatdb_dbcb_users, &arg_struct, &err_msg );
   if( SQLITE_OK != retval ) {
      /* TODO: Return err_msg. */
      dbglog_error( "could not execute database message query!\n" );
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

