#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "nrf.h"
#include "nrf_soc.h"
#include "nordic_common.h"
#include "boards.h"
#include "app_timer.h"
#include "app_util.h"
#include "nrf_fstorage.h"

#ifdef SOFTDEVICE_PRESENT
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_fstorage_sd.h"
#else
#include "nrf_drv_clock.h"
#include "nrf_fstorage_nvmc.h"
#endif

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"


#define BUTTON_DETECTION_DELAY  APP_TIMER_TICKS(50)
#define APP_BLE_CONN_CFG_TAG    1


static void fstorage_evt_handler(nrf_fstorage_evt_t * p_evt);

#define REV(n) ((n << 24) | (((n>>16)<<24)>>16) |  (((n<<16)>>24)<<16) | (n>>24))


NRF_FSTORAGE_DEF(nrf_fstorage_t fstorage) =
{
    /* Set a handler for fstorage events. */
    .evt_handler = fstorage_evt_handler,

    /* These below are the boundaries of the flash space assigned to this instance of fstorage.
     * You must set these manually, even at runtime, before nrf_fstorage_init() is called.
     * The function nrf5_flash_end_addr_get() can be used to retrieve the last address on the
     * last page of flash available to write data. */
    .start_addr = 0x3e000,
    .end_addr   = 0x410FD,
};

/* Dummy data to write to flash. */
static uint32_t m_data          = 0x00;
static char     m_hello_world[] = "hello world";
static uint32_t m_data2          = 0xBADC0FFE;

#define F_ADDR1 0x3e000
#define F_ADDR2 0x3e100
#define F_ADDR3 0x3f000


/**@brief   Helper function to obtain the last address on the last page of the on-chip flash that
 *          can be used to write user data.
 */
static uint32_t nrf5_flash_end_addr_get()
{
    uint32_t const bootloader_addr = BOOTLOADER_ADDRESS;
    uint32_t const page_sz         = NRF_FICR->CODEPAGESIZE;
    uint32_t const code_sz         = NRF_FICR->CODESIZE;

    return (bootloader_addr != 0xFFFFFFFF ?
            bootloader_addr : (code_sz * page_sz));
}


#ifdef SOFTDEVICE_PRESENT
/**@brief   Function for initializing the SoftDevice and enabling the BLE stack. */
static void ble_stack_init(void)
{
    ret_code_t rc;
    uint32_t   ram_start;

    /* Enable the SoftDevice. */
    rc = nrf_sdh_enable_request();
    APP_ERROR_CHECK(rc);

    rc = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(rc);

    rc = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(rc);
}
#else
static void clock_init(void)
{
    /* Initialize the clock. */
    ret_code_t rc = nrf_drv_clock_init();
    APP_ERROR_CHECK(rc);

    nrf_drv_clock_lfclk_request(NULL);

    // Wait for the clock to be ready.
    while (!nrf_clock_lf_is_running()) {;}
}
#endif


/**@brief   Initialize the timer. */
static void timer_init(void)
{
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
}


/**@brief   Sleep until an event is received. */
static void power_manage(void)
{
#ifdef SOFTDEVICE_PRESENT
    (void) sd_app_evt_wait();
#else
    __WFE();
#endif
}


static void fstorage_evt_handler(nrf_fstorage_evt_t * p_evt)
{
    if (p_evt->result != NRF_SUCCESS)
    {
        NRF_LOG_INFO("--> Event received: ERROR while executing an fstorage operation.");
        return;
    }

    switch (p_evt->id)
    {
        case NRF_FSTORAGE_EVT_WRITE_RESULT:
        {
            NRF_LOG_INFO("--> Event received: wrote %d bytes at address 0x%x.",
                         p_evt->len, p_evt->addr);
        } break;

        case NRF_FSTORAGE_EVT_ERASE_RESULT:
        {
            NRF_LOG_INFO("--> Event received: erased %d page from address 0x%x.",
                         p_evt->len, p_evt->addr);
        } break;

        default:
            break;
    }
}


static void print_flash_info(nrf_fstorage_t * p_fstorage)
{
    printf("========| flash info |========\n");
    printf("erase unit: \t%d bytes\n",      p_fstorage->p_flash_info->erase_unit);
    printf("program unit: \t%d bytes\n",    p_fstorage->p_flash_info->program_unit);
    printf("==============================\n\n");
}


void wait_for_flash_ready(nrf_fstorage_t const * p_fstorage)
{
    /* While fstorage is busy, sleep and wait for an event. */
    while (nrf_fstorage_is_busy(p_fstorage))
    {
        power_manage();
    }
}


static void log_init(void)
{
    ret_code_t rc = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(rc);
}

void flash_write(uint32_t addr, void const * data) {
    ret_code_t rc;
    printf("Writing to addr: %x\n", addr);
    printf("DATA: %x\n", data);
    printf("LEN: %d\n\n", sizeof(data));

    rc = nrf_fstorage_write(&fstorage, addr, &data, sizeof(data), NULL);
    APP_ERROR_CHECK(rc);

    wait_for_flash_ready(&fstorage);
}

void flash_read(uint32_t addr, uint32_t len) {
    printf("Reading addr: %x\r\n", addr);
    ret_code_t rc;
    uint8_t    data[256] = {0};

    if (len > sizeof(data))
    {
        len = sizeof(data);
    }
    rc = nrf_fstorage_read(&fstorage, addr, data, len);
    if (rc != NRF_SUCCESS) {
      printf("unsuccessful\r\n");
    }

    //printf("STR DATA: %s\n", data);

    printf("\nHEX DATA: 0x");
    for (int32_t i = (len -1); i >= 0; i--)
    {
      printf("%x", data[i]);
    }
    printf("\n\n\n");
}

void flash_erase(uint32_t addr, uint32_t page) {
    ret_code_t rc;
    rc = nrf_fstorage_erase(&fstorage, addr, page, NULL);
    if (rc != NRF_SUCCESS)
    {
        printf("nrf_fstorage_erase() returned: %s\n",
                        nrf_strerror_get(rc));
    } else {
        printf("Flash erased\n");
    }
}

int main(void)
{
    ret_code_t rc;

#ifndef SOFTDEVICE_PRESENT
    clock_init();
#endif

    timer_init();
    log_init();

    nrf_fstorage_api_t * p_fs_api;

#ifdef SOFTDEVICE_PRESENT
    NRF_LOG_INFO("Initializing nrf_fstorage_sd implementation...");
    /* Initialize an fstorage instance using the nrf_fstorage_sd backend.
     * nrf_fstorage_sd uses the SoftDevice to write to flash. This implementation can safely be
     * used whenever there is a SoftDevice, regardless of its status (enabled/disabled). */
    p_fs_api = &nrf_fstorage_sd;
#else
    NRF_LOG_INFO("SoftDevice not present.");
    NRF_LOG_INFO("Initializing nrf_fstorage_nvmc implementation...");
    /* Initialize an fstorage instance using the nrf_fstorage_nvmc backend.
     * nrf_fstorage_nvmc uses the NVMC peripheral. This implementation can be used when the
     * SoftDevice is disabled or not present.
     *
     * Using this implementation when the SoftDevice is enabled results in a hardfault. */
    p_fs_api = &nrf_fstorage_nvmc;
#endif

    rc = nrf_fstorage_init(&fstorage, p_fs_api, NULL);
    APP_ERROR_CHECK(rc);

    print_flash_info(&fstorage);
    
    (void) nrf5_flash_end_addr_get();


#ifdef SOFTDEVICE_PRESENT
    /* Enable the SoftDevice and the BLE stack. */
    ble_stack_init();

#endif

    uint32_t bson_s_1 = strtoul("0x64a65009", NULL, 16);
    uint32_t bson_s_2 = strtoul("0xfee0844a", NULL, 16);
    uint32_t bson_s_3 = strtoul("0xd77da995", NULL, 16);

    printf("=============================\n");
    printf("STARTING WRITE OPERATIONS\n");
    printf("=============================\n\n");

    //flash_write(F_ADDR1, bson_s_1);
    //flash_write(F_ADDR2, bson_s_2);
    //flash_write(F_ADDR3, bson_s_3);

    printf("=============================\n");
    printf("STARTING READ OPERATIONS\n");
    printf("=============================\n\n");

    flash_read(F_ADDR1, 4);
    flash_read(F_ADDR2, 4);
    flash_read(F_ADDR3, 4);

    printf("=============================\n");
    printf("STARTING ERASURE OPERATIONS\n");
    printf("=============================\n\n");

    flash_erase(F_ADDR1,2);

    /* Enter main loop. */
    for (;;)
    {
        if (!NRF_LOG_PROCESS())
        {
            power_manage();
        }
    }
}
