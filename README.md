<h1 align="center">
  HTTP-SERVER
</h1>

This is a simple http server implemented in c.

Features
--------

- **Echo Route**: The server can echo back any message sent to the `/echo` route, supporting the gzip compression.

- **User-Agent Detection**: It detects and returns the `User-Agent` string of the client.

- **File Handling**: Capable of serving files from a specified directory and handling `GET` and `POST` requests to read and write file contents.

- **Concurrent Connections**: Uses non-blocking sockets to handle multiple connections concurrently with _POSIX thread(pthread)_.

Get started
-----------

Compile the source with this command:

``` sh
./compile.sh
```

> requires GCC compiler, and a UNIX environment to compile

Usage
-----

```sh
./server [--directory path]
```

Routes
------

* `/` support `GET` method, return `200`
* `/user-agent` support `GET` method, return the user agent of the client
* `/echo/{str}` support `GET` method, return the `{str}`

The following requires the `--directory` to be set:

* `/files/{filename}` support `GET` method, return the content of `filename` if exists
* `/files/{filename}` support `POST` method, create `filename` in `path` with the content of the request's body
