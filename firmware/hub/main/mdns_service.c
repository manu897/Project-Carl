#include "mdns_service.h"

#include "esp_log.h"
#include "mdns.h"
#include "sdkconfig.h"

static const char *TAG = "carl-mdns";

void carl_mdns_start(void) {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_CARL_MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("Project-Carl Hub"));

    // _carl-hub._tcp — what the iOS app's Bonjour discovery looks for.
    mdns_txt_item_t carl_txt[] = {
        { "version", "0.1.0" },
        { "api",     "/api"  },
    };
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_carl-hub", "_tcp",
                                     CONFIG_CARL_HTTP_PORT,
                                     carl_txt,
                                     sizeof(carl_txt) / sizeof(carl_txt[0])));

    // Generic _http._tcp so browsers + Bonjour-aware apps see the dashboard.
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp",
                                     CONFIG_CARL_HTTP_PORT, NULL, 0));

    ESP_LOGI(TAG, "%s.local advertised on _carl-hub._tcp:%d",
             CONFIG_CARL_MDNS_HOSTNAME, CONFIG_CARL_HTTP_PORT);
}
