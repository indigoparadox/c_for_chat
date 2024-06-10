
# C is for Chat

C is for Chat is a web-based chat server designed to be simple and self-reliant. It uses the Better String Library for handling strings, and also supports modern security features like CSRF protections and ReCAPTCHA.

# Roadmap

 * Sticker support.
 * Websockets for smooth chat history updates.
 * Automatic deletion of expired sessions on the server.
 * OAuth/2-Factor options.
 * External protocol interaction (e.g. IRC).
 * Profile pictures.
 * Administrative commands.
 * Configuration panel.
 * Client timezone display.
 * Automatic message loading with scrolling.

# Compiling

## Dependencies

 * libcurl-dev
 * fcgi-dev
 * openssl-dev
 * sqlite3-dev
 * libwebsockets-dev

## FreeBSD

Compiling on FreeBSD requires gmake.

### Static

 * Compile devel/unistring as a port, manually editing the Makefile to --enable-static.
 * Compile databases/sqlite3 as a port, enabling static support.
 * Compile ftp/curl as a port, disabling libssh2 and gss3 support and enabling static support.
 * Install pkg-config.

# Running

Once you have a working binary, you can configure C for Chat to run as a daemon (either through OpenRC, systemd, or another service manager appropriate for your system).

You will also need a proxy server to interpret HTTP requests and pass them to the FastCGI interface. The nginx configuration below assumes the domain cchat.example.com and the default listening port of 9000, but you can specify a new port with the -s switch to the cchat binary (e.g. -s 127.0.0.1:8080 for port 8080).

```
include fastcgi.conf;

server {
	listen 80;

	server_name cchat.example.com;

	location / {
		fastcgi_pass 127.0.0.1:9000;
	}
}
```

