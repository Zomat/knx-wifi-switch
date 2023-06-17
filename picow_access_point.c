/**
 * @file picow_access_point.c
 * @author Mateusz Zolisz <mateusz.zolisz@gmail.com>
 * @brief 
 * @version 0.4
 * @date 2023-06-15
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include <string.h>
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/resets.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "hardware/uart.h"
#include "knxTelegram.h"

#define AP_NAME "Zolisz KNX Switch"
#define AP_PASSWORD "password123"

#define LED_GPIO 0

#define TCP_PORT 80
#define DEBUG_printf printf
#define POLL_TIME_S 5
#define HTTP_GET "GET"
#define HTTP_RESPONSE_HEADERS "HTTP/1.1 %d OK\nContent-Length: %d\nContent-Type: text/html; charset=utf-8\nConnection: close\n\n"
#define HTTP_RESPONSE_REDIRECT "HTTP/1.1 302 Redirect\nLocation: http://%s\n\n"

#define KNX_SOURCE_ADDRESS "0.0.1"
#define KNX_DEFAULT_TARGET_ADDRESS "0.0.2"

/**
 * WebServer Routes with Params
 */
#define KNX_SWITCH_ROUTE "/switch"
#define KNX_SWITCH_PARAM "value=%d"
#define KNX_TARGET_ROUTE "/target"
#define KNX_TARGET_PARAM "main=%d&middle=%d&sub=%d"
#define KNX_DIMMING_ROUTE "/dimming"
#define KNX_DIMMING_PARAM "value=%d"

/**
 * WebServer Templates
 */
#define TEMPLATE_HEADER "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><style>body{font-family: Arial, sans-serif; background-color: #f2f2f2; text-align: center;} h1{color: #333;} p{color: #666;} a{color: #007bff; text-decoration: none;} a:hover{color: #0056b3; text-decoration: underline;} input{border-radius: 4px; padding: 4px;} @media (max-width: 480px) { body{padding: 20px;} h{font-size: 24px;} p{font-size: 16px;} }</style></head><body><h1>KNX WiFi Switch.</h1><p>by Mateusz Zolisz 2023</p><p><a href=\"" KNX_SWITCH_ROUTE "\">Switch</a> | <a href=\"" KNX_TARGET_ROUTE "\">Target</a> | <a href=\"" KNX_DIMMING_ROUTE "\">Dimming</a></p></br>"

#define TEMPLATE_SWITCH_BODY TEMPLATE_HEADER "<p><a href=\"" KNX_SWITCH_ROUTE "?" KNX_SWITCH_PARAM "\"><button>Switch KNX %s</button></a></p></body></html>"

#define TEMPLATE_ADDRESS_BODY TEMPLATE_HEADER "<form action=\"" KNX_TARGET_ROUTE "\"><label for=\"main\">Main</label></br><input type=\"number\" id=\"main\" name=\"main\" value=\"%d\" placeholder=\"main\" required><br><br><label for=\"middle\">Middle</label></br><input type=\"number\" name=\"middle\" id=\"middle\" placeholder=\"middle\" value=\"%d\" required><br><br><label for=\"sub\">Sub</label></br><input type=\"number\" id=\"sub\" name=\"sub\" value=\"%d\" placeholder=\"sub\" required><br><br><input type=\"submit\" value=\"Change Address\"></form></body></html>"

#define TEMPLATE_DIMMING_BODY TEMPLATE_HEADER "<form action=\"" KNX_DIMMING_ROUTE "\"><label for=\"value\">Value (0-255)</label></br><input type=\"number\" min=0 max=255 id=\"value\" name=\"value\" value=\"%d\" placeholder=\"value\" required style=\"width: 100px\"><br><br><input type=\"submit\" value=\"Set Dimmer\"></form></body></html>"

/**
 * UART Settings
 */
#define UART_ID uart1
#define BAUD_RATE 19200
#define UART_TX_PIN 4
#define UART_RX_PIN 5

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

static int knxState = 0;
static int knxDimmingValue = 0;
char knxTargetAddr[11] = KNX_DEFAULT_TARGET_ADDRESS;

static bool sendKnxTelegram(const char telegram[], int messageSize) {
    uint8_t sendbuf[2];
    for (int i = 0; i < messageSize; i++) {
        if (i == (messageSize - 1)) {
            sendbuf[0] = TPUART_DATA_END;
        }  else {
            sendbuf[0] = TPUART_DATA_START_CONTINUE;
        }

        sendbuf[0] |= i;
        sendbuf[1] = telegram[i];

        uart_putc_raw(UART_ID, sendbuf[0]);
        uart_putc_raw(UART_ID, sendbuf[1]);
    }

    return true;
}

void blinkLed(uint8_t count, uint time) {
    for (size_t i = 0; i < count; i++) {
        cyw43_arch_gpio_put(LED_GPIO, !knxState);
        sleep_ms(time);
        cyw43_arch_gpio_put(LED_GPIO, knxState);
        sleep_ms(time);
    }
}

static int server_content(const char *request, const char *params, char *result, size_t max_result_len) {
    int len = 0;
   
    /* Default Route */
    if (strncmp(request, KNX_SWITCH_ROUTE, sizeof(KNX_SWITCH_ROUTE) - 1) == 0 
        || strncmp(request, "/", 1) == 0) {
        uint8_t telegram[9];
        uint16_t data;
        uint8_t checksum;
        uint8_t controlByte = knxCreateControlField(false, "auto");
        uint16_t sourceAddress = knxCreateSourceAddressFieldFromString(KNX_SOURCE_ADDRESS);
        uint16_t targetAddress = knxCreateTargetGroupAddressFieldFromString(knxTargetAddr);
        uint8_t byte5 = 0x00;
        knxSetTargetAddressType(&byte5, true);
        knxSetRoutingCounter(&byte5, 6);  
        knxSetDataLength(&byte5, 1);  

        /* === Fill telegram === */
        telegram[0] = controlByte;
        telegram[1] = (sourceAddress >> 8) & 0x00FF;
        telegram[2] = (sourceAddress & 0x00FF);
        telegram[3] = (targetAddress >> 8) & 0x00FF;
        telegram[4] = (targetAddress) & 0x00FF;
        telegram[5] = byte5;
        data = knxCreateDataSwitchField(KNX_CMD_VALUE_WRITE, !knxState);
        telegram[6] = (data >> 8) & 0x00FF;
        telegram[7] = data & 0x00FF;
        telegram[8] = knxCalculateChecksum(telegram, sizeof(telegram)/sizeof(uint8_t));

        if (params) {
            int knxSwitchParam = sscanf(params, KNX_SWITCH_PARAM, &knxState);
            if (knxSwitchParam == 1) {
                if (knxState) {
                    knxState = true;
                } else {
                    knxState = false;
                }
            }

            bool sendTelegram = sendKnxTelegram(telegram, 9);
            if (sendTelegram) {
                cyw43_arch_gpio_put(LED_GPIO, knxState);
            }
        }

        if (knxState) {
            return snprintf(result, max_result_len, TEMPLATE_SWITCH_BODY, 0, "OFF");
        } else {
            return snprintf(result, max_result_len, TEMPLATE_SWITCH_BODY, 1, "ON");
        }
    }

    if (strncmp(request, KNX_DIMMING_ROUTE, sizeof(KNX_DIMMING_ROUTE) - 1) == 0) {
        if (params) {
            sscanf(params, KNX_DIMMING_PARAM, &knxDimmingValue);

            uint8_t telegram[10];
            uint32_t data = knxCreateDataDimmingField(KNX_CMD_VALUE_WRITE, knxDimmingValue);
            uint8_t checksum;
            uint8_t controlByte = knxCreateControlField(false, "auto");
            uint16_t sourceAddress = knxCreateSourceAddressFieldFromString(KNX_SOURCE_ADDRESS);
            uint16_t targetAddress = knxCreateTargetGroupAddressFieldFromString(knxTargetAddr);
            uint8_t byte5 = 0x00;
            knxSetTargetAddressType(&byte5, true);
            knxSetRoutingCounter(&byte5, 6);  
            knxSetDataLength(&byte5, 1);  

            /* === Fill telegram === */
            telegram[0] = controlByte;
            telegram[1] = (sourceAddress >> 8) & 0x00FF;
            telegram[2] = (sourceAddress & 0x00FF);
            telegram[3] = (targetAddress >> 8) & 0x00FF;
            telegram[4] = (targetAddress) & 0x00FF;
            telegram[5] = byte5;
            telegram[6] = (data >> 16) & 0x00FF;
            telegram[7] = (data >> 8) & 0x00FF;
            telegram[8] = data & 0x00FF;
            telegram[9] = knxCalculateChecksum(telegram, sizeof(telegram)/sizeof(uint8_t));

            bool sendTelegram = sendKnxTelegram(telegram, 10);

            if (sendTelegram) {
                blinkLed(10, 30);
            }
        }

        return snprintf(result, max_result_len, TEMPLATE_DIMMING_BODY, knxDimmingValue);
    }

    if (strncmp(request, KNX_TARGET_ROUTE, sizeof(KNX_TARGET_ROUTE) - 1) == 0) {
        int main, middle, sub;
        if (params) {
            sscanf(params, KNX_TARGET_PARAM, &main, &middle, &sub);
            sprintf(knxTargetAddr, "%d.%d.%d", main, middle, sub);
            blinkLed(3, 100);
            DEBUG_printf("ADDR: %s \n", knxTargetAddr);
        } else {
            sscanf(knxTargetAddr, "%d.%d.%d", &main, &middle, &sub);
        }

        return snprintf(result, max_result_len, TEMPLATE_ADDRESS_BODY, main, middle, sub);
    }
}

static err_t tcp_close_client_connection(TCP_CONNECT_STATE_T *con_state, struct tcp_pcb *client_pcb, err_t close_err) {
    if (client_pcb) {
        assert(con_state && con_state->pcb == client_pcb);
        tcp_arg(client_pcb, NULL);
        tcp_poll(client_pcb, NULL, 0);
        tcp_sent(client_pcb, NULL);
        tcp_recv(client_pcb, NULL);
        tcp_err(client_pcb, NULL);
        err_t err = tcp_close(client_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(client_pcb);
            close_err = ERR_ABRT;
        }
        if (con_state) {
            free(con_state);
        }
    }
    return close_err;
}

static void tcp_server_close(TCP_SERVER_T *state) {
    if (state->server_pcb) {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }
}

static err_t tcp_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    DEBUG_printf("tcp_server_sent %u\n", len);
    con_state->sent_len += len;
    if (con_state->sent_len >= con_state->header_len + con_state->result_len) {
        DEBUG_printf("all done\n");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }
    return ERR_OK;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    if (!p) {
        DEBUG_printf("connection closed\n");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }
    assert(con_state && con_state->pcb == pcb);
    if (p->tot_len > 0) {
        DEBUG_printf("tcp_server_recv %d err %d\n", p->tot_len, err);

        // Copy the request into the buffer
        pbuf_copy_partial(p, con_state->headers, p->tot_len > sizeof(con_state->headers) - 1 ? sizeof(con_state->headers) - 1 : p->tot_len, 0);

        // Handle GET request
        if (strncmp(HTTP_GET, con_state->headers, sizeof(HTTP_GET) - 1) == 0) {
            char *request = con_state->headers + sizeof(HTTP_GET); // + space
            char *params = strchr(request, '?');
            if (params) {
                if (*params) {
                    char *space = strchr(request, ' ');
                    *params++ = 0;
                    if (space) {
                        *space = 0;
                    }
                } else {
                    params = NULL;
                }
            }

            // Generate content
            con_state->result_len = server_content(request, params, con_state->result, sizeof(con_state->result));
            DEBUG_printf("Request: %s?%s\n", request, params);
            DEBUG_printf("Result: %d\n", con_state->result_len);

            // Check we had enough buffer space
            if (con_state->result_len > sizeof(con_state->result) - 1) {
                DEBUG_printf("Too much result data %d\n", con_state->result_len);
                return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
            }

            // Generate web page
            if (con_state->result_len > 0) {
                con_state->header_len = snprintf(con_state->headers, sizeof(con_state->headers), HTTP_RESPONSE_HEADERS,
                    200, con_state->result_len);
                if (con_state->header_len > sizeof(con_state->headers) - 1) {
                    DEBUG_printf("Too much header data %d\n", con_state->header_len);
                    return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
                }
            } else {
                // Send redirect
                con_state->header_len = snprintf(con_state->headers, sizeof(con_state->headers), HTTP_RESPONSE_REDIRECT,
                    ipaddr_ntoa(con_state->gw));
                DEBUG_printf("Sending redirect %s", con_state->headers);
            }

            // Send the headers to the client
            con_state->sent_len = 0;
            err_t err = tcp_write(pcb, con_state->headers, con_state->header_len, 0);
            if (err != ERR_OK) {
                DEBUG_printf("failed to write header data %d\n", err);
                return tcp_close_client_connection(con_state, pcb, err);
            }

            // Send the body to the client
            if (con_state->result_len) {
                err = tcp_write(pcb, con_state->result, con_state->result_len, 0);
                if (err != ERR_OK) {
                    DEBUG_printf("failed to write result data %d\n", err);
                    return tcp_close_client_connection(con_state, pcb, err);
                }
            }
        }
        tcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_server_poll(void *arg, struct tcp_pcb *pcb) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    DEBUG_printf("tcp_server_poll_fn\n");
    return tcp_close_client_connection(con_state, pcb, ERR_OK); // Just disconnect clent?
}

static void tcp_server_err(void *arg, err_t err) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err_fn %d\n", err);
        tcp_close_client_connection(con_state, con_state->pcb, err);
    }
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        DEBUG_printf("failure in accept\n");
        return ERR_VAL;
    }
    DEBUG_printf("client connected\n");

    // Create the state for the connection
    TCP_CONNECT_STATE_T *con_state = calloc(1, sizeof(TCP_CONNECT_STATE_T));
    if (!con_state) {
        DEBUG_printf("failed to allocate connect state\n");
        return ERR_MEM;
    }
    con_state->pcb = client_pcb; // for checking
    con_state->gw = &state->gw;

    // setup connection to client
    tcp_arg(client_pcb, con_state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK;
}

static bool tcp_server_open(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    DEBUG_printf("starting server on port %u\n", TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, IP_ANY_TYPE, TCP_PORT);
    if (err) {
        DEBUG_printf("failed to bind to port %d\n");
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        DEBUG_printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    return true;
}

int main() {
    stdio_init_all();

    uart_init(UART_ID, BAUD_RATE);

    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    uart_set_format(UART_ID, 8, 1, UART_PARITY_EVEN);

    TCP_SERVER_T *state = calloc(1, sizeof(TCP_SERVER_T));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return 1;
    }

    if (cyw43_arch_init()) {
        DEBUG_printf("failed to initialise\n");
        return 1;
    }

    cyw43_arch_enable_ap_mode(AP_NAME, AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

    ip4_addr_t mask;
    IP4_ADDR(ip_2_ip4(&state->gw), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);

    // Start the dhcp server
    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &state->gw, &mask);

    // Start the dns server
    dns_server_t dns_server;
    dns_server_init(&dns_server, &state->gw);

    if (!tcp_server_open(state)) {
        DEBUG_printf("failed to open server\n");
        return 1;
    }

    while(!state->complete) {
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer interrupt) to check for Wi-Fi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        // you can poll as often as you like, however if you have nothing else to do you can
        // choose to sleep until either a specified time, or cyw43_arch_poll() has work to do:
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000));
#else
        // if you are not using pico_cyw43_arch_poll, then Wi-FI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        sleep_ms(1000);
#endif
    }
    dns_server_deinit(&dns_server);
    dhcp_server_deinit(&dhcp_server);
    cyw43_arch_deinit();

    return 0;
}
