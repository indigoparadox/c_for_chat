
# C is for Chat

# Compiling (Static)

## FreeBSD

 * Compile devel/unistring as a port, manually editing the Makefile to --enable-static.
 * Compile databases/sqlite3 as a port, enabling static support.
 * Compile ftp/curl as a port, disabling libssh2 and gss3 support and enabling static support.
 * Install pkg-config.

