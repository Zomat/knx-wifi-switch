#include <string.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "pico/stdlib.h"

#define TCP_PORT 80
#define POLL_TIME_S 5
#define HTTP_GET "GET"
#define HTTP_RESPONSE_HEADERS "HTTP/1.1 %d OK\nContent-Length: %d\nContent-Type: text/html; charset=utf-8\nConnection: close\n\n"
#define HTTP_RESPONSE_REDIRECT "HTTP/1.1 302 Redirect\nLocation: http://%s\n\n"
#define DEBUG_printf printf

typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    bool complete;
    ip_addr_t gw;
} TCP_SERVER_T;

typedef struct TCP_CONNECT_STATE_T_ {
    struct tcp_pcb *pcb;
    int sent_len;
    char headers[128];
    char result[2048];
    int header_len;
    int result_len;
    ip_addr_t *gw;
} TCP_CONNECT_STATE_T;

err_t tcp_close_client_connection(TCP_CONNECT_STATE_T *con_state, struct tcp_pcb *client_pcb, err_t close_err);
void tcp_server_close(TCP_SERVER_T *state);
err_t tcp_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len);
err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
err_t tcp_server_poll(void *arg, struct tcp_pcb *pcb);
void tcp_server_err(void *arg, err_t err);
err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err);
bool tcp_server_open(void *arg);
int server_content(const char *request, const char *params, char *result, size_t max_result_len);