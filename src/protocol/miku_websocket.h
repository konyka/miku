#ifndef MIKU_WEBSOCKET_H
#define MIKU_WEBSOCKET_H

#include "miku_common.h"
#include "miku_io.h"

typedef enum {
    MK_WS_CONTINUATION = 0x0,
    MK_WS_TEXT          = 0x1,
    MK_WS_BINARY        = 0x2,
    MK_WS_CLOSE         = 0x8,
    MK_WS_PING          = 0x9,
    MK_WS_PONG          = 0xA
} miku_ws_opcode_t;

typedef struct {
    miku_ws_opcode_t opcode;
    bool             fin;
    bool             masked;
    uint64_t         payload_len;
    uint8_t          masking_key[4];
    uint8_t         *payload;
} miku_ws_frame_t;

typedef struct miku_ws_server_s miku_ws_server_t;
typedef struct miku_ws_conn_s   miku_ws_conn_t;

typedef void (*miku_ws_on_connect_fn)(miku_ws_conn_t *conn, void *ctx);
typedef void (*miku_ws_on_message_fn)(miku_ws_conn_t *conn, miku_ws_opcode_t opcode,
                                       const uint8_t *data, size_t len, void *ctx);
typedef void (*miku_ws_on_close_fn)(miku_ws_conn_t *conn, void *ctx);

struct miku_ws_conn_s {
    int                fd;
    char               addr[64];
    miku_ws_server_t  *server;
    void              *user_data;
};

MIKU_API miku_ws_frame_t *miku_ws_frame_create(void);
MIKU_API void             miku_ws_frame_destroy(miku_ws_frame_t *f);
MIKU_API int              miku_ws_frame_encode(const miku_ws_frame_t *f, uint8_t *out, size_t out_cap, size_t *out_len);
MIKU_API int              miku_ws_frame_decode(miku_ws_frame_t *f, const uint8_t *data, size_t len, size_t *consumed);
MIKU_API int              miku_ws_frame_read(int fd, miku_ws_frame_t *f);

MIKU_API int  miku_ws_send_text(int fd, const char *text, size_t len);
MIKU_API int  miku_ws_send_binary(int fd, const uint8_t *data, size_t len);
MIKU_API int  miku_ws_send_close(int fd, uint16_t code, const char *reason);
MIKU_API int  miku_ws_send_pong(int fd, const uint8_t *data, size_t len);

MIKU_API int  miku_ws_handshake(const char *sec_ws_key, char *accept_out, size_t accept_cap);

MIKU_API miku_ws_server_t *miku_ws_server_create(const char *host, int port);
MIKU_API void  miku_ws_server_on_connect(miku_ws_server_t *srv, miku_ws_on_connect_fn fn, void *ctx);
MIKU_API void  miku_ws_server_on_message(miku_ws_server_t *srv, miku_ws_on_message_fn fn, void *ctx);
MIKU_API void  miku_ws_server_on_close(miku_ws_server_t *srv, miku_ws_on_close_fn fn, void *ctx);
MIKU_API int   miku_ws_server_start(miku_ws_server_t *srv);
MIKU_API void  miku_ws_server_stop(miku_ws_server_t *srv);
MIKU_API void  miku_ws_server_destroy(miku_ws_server_t *srv);

#endif
