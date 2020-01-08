/*
 * Copyright (C) 2015-2018 Alibaba Group Holding Limited
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "infra_compat.h"
#include "mqtt_api.h"
#include "ota_api.h"
#include "wrappers.h"

#if defined(__MBED__)
#include "mbedbl-ucp.h"
#endif

char g_product_key[IOTX_PRODUCT_KEY_LEN + 1]       = "a1wOVhf0PmQ";
char g_product_secret[IOTX_PRODUCT_SECRET_LEN + 1] = "MJnWqe9khWg7v5aH";
char g_device_name[IOTX_DEVICE_NAME_LEN + 1]       = "T10YogSxts4YVtugH5at";
char g_device_secret[IOTX_DEVICE_SECRET_LEN + 1]   = "9pl920NeLA4D7vMgnGW8koCN8TaScglw";

#define OTA_MQTT_MSGLEN         (2048)

#define EXAMPLE_TRACE(fmt, ...)  \
    do { \
        HAL_Printf("%s|%03d :: ", __func__, __LINE__); \
        HAL_Printf(fmt, ##__VA_ARGS__); \
        HAL_Printf("%s", "\r\n"); \
    } while(0)

static int      user_argc;
static char   **user_argv;

void event_handle(void *pcontext, void *pclient, iotx_mqtt_event_msg_pt msg)
{
    uintptr_t packet_id = (uintptr_t)msg->msg;
    iotx_mqtt_topic_info_pt topic_info = (iotx_mqtt_topic_info_pt)msg->msg;

    switch (msg->event_type) {
        case IOTX_MQTT_EVENT_UNDEF:
            EXAMPLE_TRACE("undefined event occur.");
            break;

        case IOTX_MQTT_EVENT_DISCONNECT:
            EXAMPLE_TRACE("MQTT disconnect.");
            break;

        case IOTX_MQTT_EVENT_RECONNECT:
            EXAMPLE_TRACE("MQTT reconnect.");
            break;

        case IOTX_MQTT_EVENT_SUBCRIBE_SUCCESS:
            EXAMPLE_TRACE("subscribe success, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_SUBCRIBE_TIMEOUT:
            EXAMPLE_TRACE("subscribe wait ack timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_SUBCRIBE_NACK:
            EXAMPLE_TRACE("subscribe nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_UNSUBCRIBE_SUCCESS:
            EXAMPLE_TRACE("unsubscribe success, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_UNSUBCRIBE_TIMEOUT:
            EXAMPLE_TRACE("unsubscribe timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_UNSUBCRIBE_NACK:
            EXAMPLE_TRACE("unsubscribe nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_PUBLISH_SUCCESS:
            EXAMPLE_TRACE("publish success, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_PUBLISH_TIMEOUT:
            EXAMPLE_TRACE("publish timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_PUBLISH_NACK:
            EXAMPLE_TRACE("publish nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case IOTX_MQTT_EVENT_PUBLISH_RECEIVED:
            EXAMPLE_TRACE("topic message arrived but without any related handle: topic=%.*s, topic_msg=%.*s",
                          topic_info->topic_len,
                          topic_info->ptopic,
                          topic_info->payload_len,
                          topic_info->payload);
            break;

        default:
            EXAMPLE_TRACE("Should NOT arrive here.");
            break;
    }
}

static int _ota_mqtt_client(void)
{
#define OTA_BUF_LEN        (128)

    int rc = 0, ota_over = 0;
    void *pclient = NULL, *h_ota = NULL;
    iotx_conn_info_pt pconn_info;
    iotx_mqtt_param_t mqtt_params;
    char *msg_buf = NULL, *msg_readbuf = NULL;
#if defined(__MBED__)
    char *buf_ota = NULL;

    if (NULL == (buf_ota = (char *)HAL_Malloc(OTA_BUF_LEN))) {
        EXAMPLE_TRACE("not enough memory");
        rc = -1;
        goto do_exit;
    }
#else
    FILE *fp;
    char buf_ota[OTA_BUF_LEN];

    if (NULL == (fp = fopen("ota.bin", "wb+"))) {
        EXAMPLE_TRACE("open file failed");
        goto do_exit;
    }
#endif


    if (NULL == (msg_buf = (char *)HAL_Malloc(OTA_MQTT_MSGLEN))) {
        EXAMPLE_TRACE("not enough memory");
        rc = -1;
        goto do_exit;
    }

    if (NULL == (msg_readbuf = (char *)HAL_Malloc(OTA_MQTT_MSGLEN))) {
        EXAMPLE_TRACE("not enough memory");
        rc = -1;
        goto do_exit;
    }

    /**< get device info*/
    IOT_Ioctl(IOTX_IOCTL_SET_PRODUCT_KEY, g_product_key);
    IOT_Ioctl(IOTX_IOCTL_SET_DEVICE_NAME, g_device_name);
    IOT_Ioctl(IOTX_IOCTL_SET_DEVICE_SECRET, g_device_secret);
    /**< end*/

    /* Device AUTH */
    if (0 != IOT_SetupConnInfo(g_product_key, g_device_name, g_device_secret, (void **)&pconn_info)) {
        EXAMPLE_TRACE("AUTH request failed!");
        rc = -1;
        goto do_exit;
    }

    /* Initialize MQTT parameter */
    memset(&mqtt_params, 0x0, sizeof(mqtt_params));

    mqtt_params.port = pconn_info->port;
    mqtt_params.host = pconn_info->host_name;
    mqtt_params.client_id = pconn_info->client_id;
    mqtt_params.username = pconn_info->username;
    mqtt_params.password = pconn_info->password;
    mqtt_params.pub_key = pconn_info->pub_key;

    mqtt_params.request_timeout_ms = 2000;
    mqtt_params.clean_session = 0;
    mqtt_params.keepalive_interval_ms = 60000;
    mqtt_params.read_buf_size = OTA_MQTT_MSGLEN;
    mqtt_params.write_buf_size = OTA_MQTT_MSGLEN;

    mqtt_params.handle_event.h_fp = event_handle;
    mqtt_params.handle_event.pcontext = NULL;


    /* Construct a MQTT client with specify parameter */
    pclient = IOT_MQTT_Construct(&mqtt_params);
    if (NULL == pclient) {
        EXAMPLE_TRACE("MQTT construct failed");
        rc = -1;
        goto do_exit;
    }
    h_ota = IOT_OTA_Init(g_product_key, g_device_name, pclient);
    if (NULL == h_ota) {
        rc = -1;
        EXAMPLE_TRACE("initialize OTA failed");
        goto do_exit;
    }

    /* if (0 != IOT_OTA_ReportVersion(h_ota, "iotx_ver_1.1.0")) { */
    /* rc = -1; */
    /* EXAMPLE_TRACE("report OTA version failed"); */
    /* goto do_exit; */
    /* } */

    HAL_SleepMs(1000);

    do {
        uint32_t firmware_valid;

        EXAMPLE_TRACE("wait ota upgrade command....");

        /* handle the MQTT packet received from TCP or SSL connection */
        IOT_MQTT_Yield(pclient, 200);

        if (IOT_OTA_IsFetching(h_ota)) {
            uint32_t last_percent = 0, percent = 0;
            char md5sum[33];
            char version[128] = {0};
            int len, size_downloaded, size_file;
#if defined(__MBED__)
            mbedbl_ucp_wrtctx_t ucp_ctx;

            IOT_OTA_Ioctl(h_ota, IOT_OTAG_FILE_SIZE, &size_file, 4);
            IOT_OTA_Ioctl(h_ota, IOT_OTAG_VERSION, version, 128);
            EXAMPLE_TRACE("Firmware size: %d bytes, version: %s", size_file, version);
            if (MBEDBL_UCP_PrepareWrite(&ucp_ctx, 0, version, size_file) != 0) {
                EXAMPLE_TRACE("MBEDBL_UCP_PrepareWrite(...) failed");
                IOT_OTA_Ioctl(h_ota, IOT_OTAG_RESET_STATE, NULL, 0);
                rc = -1;
                break;
            }
#endif
            do {

                len = IOT_OTA_FetchYield(h_ota, buf_ota, OTA_BUF_LEN, 1);
                EXAMPLE_TRACE("IOT_OTA_FetchYield result: %d", len);
                if (len > 0) {
#if defined(__MBED__)
                    uint32_t fragment_size = len;
                    if (MBEDBL_UCP_Write(&ucp_ctx, (uint8_t *) buf_ota, &fragment_size) != 0) {
                        EXAMPLE_TRACE("MBEDBL_UCP_Write(...) failed");
                        IOT_OTA_Ioctl(h_ota, IOT_OTAG_RESET_STATE, NULL, 0);
                        rc = -1;
                        break;
                    }
#else
                    if (1 != fwrite(buf_ota, len, 1, fp)) {
                        EXAMPLE_TRACE("write data to file failed");
                        IOT_OTA_Ioctl(h_ota, IOT_OTAG_RESET_STATE, NULL, 0);
                        rc = -1;
                        break;
                    }
#endif
                } else {
                    HAL_SleepMs(500);
                    continue;
                }

                /* get OTA information */
                IOT_OTA_Ioctl(h_ota, IOT_OTAG_FETCHED_SIZE, &size_downloaded, 4);
                IOT_OTA_Ioctl(h_ota, IOT_OTAG_FILE_SIZE, &size_file, 4);
                IOT_OTA_Ioctl(h_ota, IOT_OTAG_MD5SUM, md5sum, 33);
                IOT_OTA_Ioctl(h_ota, IOT_OTAG_VERSION, version, 128);

                last_percent = percent;
                percent = (size_downloaded * 100) / size_file;
                if (percent - last_percent > 0) {
                    IOT_OTA_ReportProgress(h_ota, percent, NULL);
                    IOT_OTA_ReportProgress(h_ota, percent, "hello");
                }
                IOT_MQTT_Yield(pclient, 100);
            } while (!IOT_OTA_IsFetchFinish(h_ota));

            IOT_OTA_Ioctl(h_ota, IOT_OTAG_CHECK_FIRMWARE, &firmware_valid, 4);
            if (0 == firmware_valid) {
                EXAMPLE_TRACE("The firmware is invalid");
            } else {
                EXAMPLE_TRACE("The firmware is valid");
#if defined(__MBED__)
                /* We can commit UCP write only after downloaded firmware is verified. Otherwise, a corrupted
                 * firmware can go further update process as if it were integral. */
                if (MBEDBL_UCP_FinalizeWrite(&ucp_ctx) != 0) {
                    EXAMPLE_TRACE("MBEDBL_UCP_FinalizeWrite(...) failed");
                }
#endif
            }

            ota_over = 1;
        }
        HAL_SleepMs(2000);
    } while (!ota_over);

    HAL_SleepMs(200);



do_exit:

    if (NULL != h_ota) {
        IOT_OTA_Deinit(h_ota);
    }

    if (NULL != pclient) {
        IOT_MQTT_Destroy(&pclient);
    }

    if (NULL != msg_buf) {
        HAL_Free(msg_buf);
    }

    if (NULL != msg_readbuf) {
        HAL_Free(msg_readbuf);
    }

#if defined(__MBED__)
    if (NULL != buf_ota) {
        HAL_Free(buf_ota);
    }
#else
    if (NULL != fp) {
        fclose(fp);
    }
#endif

    return rc;
}

int main(int argc, char *argv[])
{
    EXAMPLE_TRACE("hello main func");
    IOT_SetLogLevel(IOT_LOG_DEBUG);

    user_argc = argc;
    user_argv = argv;

    _ota_mqtt_client();

    IOT_DumpMemoryStats(IOT_LOG_DEBUG);

    EXAMPLE_TRACE("out of sample!");
    return 0;
}

