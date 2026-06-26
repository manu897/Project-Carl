#include "captive_portal.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "carl-dns";

// SoftAP gateway address the AP netif hands out (esp-idf default).
#define AP_IP_0 192
#define AP_IP_1 168
#define AP_IP_2 4
#define AP_IP_3 1

// Minimal DNS: parse the 12-byte header + question, then append an A answer
// pointing at the hub. We don't inspect the qname — every A query resolves to
// us, which is exactly the captive-portal behaviour we want.
static void dns_task(void *arg) {
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed; captive DNS disabled");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind :53 failed; captive DNS disabled");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "captive DNS up on :53");

    uint8_t buf[512];
    while (1) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
        if (n < (int)sizeof(uint16_t) * 6) continue;  // smaller than a DNS header

        // Turn the query into a response in place.
        buf[2] |= 0x80;  // QR = response
        buf[3] |= 0x80;  // RA = recursion available
        buf[6] = 0x00; buf[7] = 0x01;  // ANCOUNT = 1 (one answer)

        // The question section starts at offset 12 and runs to the end of the
        // received packet (qname + qtype + qclass). Append the answer after it.
        int qlen = n - 12;
        if (qlen < 0 || 12 + qlen + 16 > (int)sizeof(buf)) continue;
        uint8_t *a = buf + n;
        *a++ = 0xC0; *a++ = 0x0C;             // name: pointer to the question's qname
        *a++ = 0x00; *a++ = 0x01;             // type A
        *a++ = 0x00; *a++ = 0x01;             // class IN
        *a++ = 0x00; *a++ = 0x00; *a++ = 0x00; *a++ = 0x3C;  // TTL 60s
        *a++ = 0x00; *a++ = 0x04;             // RDLENGTH 4
        *a++ = AP_IP_0; *a++ = AP_IP_1; *a++ = AP_IP_2; *a++ = AP_IP_3;

        int resp_len = a - buf;
        sendto(sock, buf, resp_len, 0, (struct sockaddr *)&src, slen);
    }
}

void carl_captive_portal_start(void) {
    xTaskCreate(dns_task, "carl_dns", 3072, NULL, 4, NULL);
}
