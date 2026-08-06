#include "zv_cc1101.h"

#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_rx.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#define CC1101_RMT_SYMBOL_COUNT 128
#define FREQ_433_HZ     433920000
#define CC1101_RX_WAIT_MAX_ATTEMPTS    100
#define CC1101_RX_WAIT_INTERVAL_US     100

static const char *TAG = "ZV_CC1101";

static spi_device_handle_t cc_device_handle = NULL;
static rmt_channel_handle_t s_rx_chan_handle = NULL;
static QueueHandle_t sr_rx_queue = NULL;

typedef struct {
    const char *name;
    uint8_t address;
    uint8_t value;
} cc1101_register_config_t;

static const cc1101_register_config_t cc1101_base_config[] = {
    { "IOCFG2",   0x00, 0x0E },
    { "IOCFG0",   0x02, 0x0D }, // data output
    { "FIFOTHR",  0x03, 0x47 },
    { "PKTCTRL0", 0x08, 0x32 },
    { "FREQ2",    0x0D, 0x10 }, // carry frecuency default 433.92
    { "FREQ1",    0x0E, 0xB0 }, // mid frecuency
    { "FREQ0",    0x0F, 0x71 }, // low frecuency
    { "MDMCFG4",  0x10, 0x67 }, // 0x10, 0xF7 RXBW = 58 Khz - 0x10, 0x67 }, /* ~270 kHz */
    { "MDMCFG3",  0x11, 0x83 },
    { "MDMCFG2",  0x12, 0x30 }, // protocol tu use default is OOK
    { "MCSM0",    0x18, 0x18 },
    { "FOCCFG",   0x19, 0x16 },
    { "AGCCTRL2", 0x1B, 0x03 },
    { "AGCCTRL1", 0x1C, 0x40 },
    { "AGCCTRL0", 0x1D, 0x91 },
    { "FREND0",   0x22, 0x11 },
    { "TEST2",    0x2C, 0x81 },
    { "TEST1",    0x2D, 0x35 },
    { "TEST0",    0x2E, 0x09 },
};

static int32_t sing_by_level (uint16_t level, uint16_t duration)
{
    return level ? (int32_t)duration : -(int32_t)duration;
}

static esp_err_t zv_cc1101_start_rx(void)
{
    esp_err_t result = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
    if (result != ESP_OK)
        return result;

    result = zv_cc1101_send_strobe(CC1101_STROBE_SFRX);
    if (result != ESP_OK)
        return result;

    result = zv_cc1101_send_strobe(CC1101_STROBE_SRX);
    if (result != ESP_OK)
        return result;

    return ESP_OK;
}

static esp_err_t zv_cc1101_read_register(uint8_t address, uint8_t burst, uint8_t *output_value)
{
    if (output_value == NULL)
        return ESP_ERR_INVALID_ARG;

    if (cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    // All transactions on the SPI interface start with a header byte containing a R/W bit, a burst
    // access bit (B), and a 6-bit address (A5 – A0).
    // 1st bit = read/write
    // 2nd bit = burst
    // 3rd/7th = addresses to read/write

    // CC1101_STATUS_PARTNUM/0x30   = 0 0 1 1 0 0 0 0
    // CC1101_READ_BURST/0xC0       = 1 1 0 0 0 0 0 0
    //                             ------------------ OR
    // PARTNUM byte buffer          = 1 1 1 1 0 0 0 0

    uint8_t rx[2] = { 0 };
    // tx[0]: header (read + burst); tx[1]: 0x00 padding so the chip can clock out the value into rx[1]
    uint8_t tx[2] = { (uint8_t)(address | burst), 0x00 };

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };

    esp_err_t err = spi_device_polling_transmit(cc_device_handle, &transaction);
    if (err != ESP_OK)
        return err;

    *output_value = rx[1];
    return ESP_OK;
}

static void zv_cc1101_rmt_rx_cleanup(void)
{
    if (s_rx_chan_handle != NULL)
    {
        rmt_disable(s_rx_chan_handle);
        rmt_del_channel(s_rx_chan_handle);
        s_rx_chan_handle = NULL;
    }

    if (sr_rx_queue != NULL)
    {
        vQueueDelete(sr_rx_queue);
        sr_rx_queue = NULL;
    }
}

static bool IRAM_ATTR rmt_rx_done_cb(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_woken = pdFALSE;
    QueueHandle_t q = (QueueHandle_t)user_data;
    xQueueSendFromISR(q, edata, &high_woken);
    return high_woken == pdTRUE;
}

static esp_err_t zv_cc1101_rmt_rx_setup()
{
    if (s_rx_chan_handle)
        return ESP_OK;

    sr_rx_queue = xQueueCreate(2, sizeof(rmt_rx_done_event_data_t));
    if (sr_rx_queue == NULL)
        return ESP_ERR_NO_MEM;

    gpio_config_t carrier_cfg = {
        .pin_bit_mask = (1ULL << ZV_SUB_GHZ_PIN_GDO2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&carrier_cfg);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_rx_cleanup();
        return err;
    }
    
    rmt_rx_channel_config_t cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 1 * 1000 * 1000,    /* 1 MHz = 1 µs/tick */
        .mem_block_symbols = CC1101_RMT_SYMBOL_COUNT,
        .gpio_num          = ZV_SUB_GHZ_PIN_GDO0,
        .flags.invert_in   = false, 
        .flags.with_dma    = false,
    };

    err = rmt_new_rx_channel(&cfg, &s_rx_chan_handle);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_rx_cleanup();
        return err;
    }

    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rmt_rx_done_cb };
    err = rmt_rx_register_event_callbacks(s_rx_chan_handle, &cbs, sr_rx_queue);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_rx_cleanup();
        return err;
    }

    err = rmt_enable(s_rx_chan_handle);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_rx_cleanup();
        return err;
    }

    return ESP_OK;
}

esp_err_t zv_cc1101_send_strobe(uint8_t command)
{
    if (cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    spi_transaction_t transaction = {
        .length    = 8,
        .tx_buffer = &command
    };

    return spi_device_polling_transmit(cc_device_handle, &transaction);
}

esp_err_t zv_cc1101_read_status_register(uint8_t address, uint8_t *output_value)
{
    return zv_cc1101_read_register(address, CC1101_READ_BURST, output_value);
}

esp_err_t zv_cc1101_read_config_register(uint8_t address, uint8_t *output_value)
{
    return zv_cc1101_read_register(address, CC1101_READ_SINGLE, output_value);
}

esp_err_t zv_cc1101_write_config_register(uint8_t address, uint8_t value)
{
    if (cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    uint8_t tx[2] = { address, value };
    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx,
    };

    return spi_device_polling_transmit(cc_device_handle, &transaction);
}

esp_err_t zv_cc1101_config_and_verify()
{
    size_t config_length = sizeof(cc1101_base_config) / sizeof((cc1101_base_config)[0]);
    for (size_t i= 0; i < config_length; i++)
    {
        cc1101_register_config_t reg_value = cc1101_base_config[i];

        esp_err_t success = zv_cc1101_write_config_register(reg_value.address, reg_value.value);
        if (success != ESP_OK)
            return success;

        uint8_t new_value;
        success = zv_cc1101_read_config_register(reg_value.address, &new_value);
        if (success != ESP_OK)
            return success;

        if (reg_value.value != new_value)
            return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t zv_cc1101_read_partnum(uint8_t *output_value)
{
    esp_err_t result = zv_cc1101_read_status_register(CC1101_STATUS_PARTNUM, output_value);
    if (result != ESP_OK)
        return result;

    return ESP_OK;
}

esp_err_t zv_cc1101_read_version(uint8_t *output_value)
{
    esp_err_t result = zv_cc1101_read_status_register(CC1101_STATUS_VERSION, output_value);
    if (result != ESP_OK)
        return result;

    return ESP_OK;
}

esp_err_t zv_cc1101_get_rssi_dbm(int8_t *rssi_dbm)
{
    if (rssi_dbm == NULL)
        return ESP_ERR_INVALID_ARG;

    uint8_t raw;
    esp_err_t res =zv_cc1101_read_status_register(CC1101_STATUS_RSSI, &raw);
    if (res != ESP_OK)
        return res;

    int8_t r = (int8_t)((raw >= 128) ? (int)raw - 256 : raw);

    *rssi_dbm = (r / 2) - 74;   /* offset = 74 dBm para 433 MHz */
    return ESP_OK;
}

esp_err_t zv_cc1101_init(spi_device_handle_t handle)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;

    cc_device_handle = handle;
    return ESP_OK;
}

esp_err_t zv_cc1101_wait_for_rx()
{
    esp_err_t regis_result = ESP_OK;
    for (int i = 0; i < CC1101_RX_WAIT_MAX_ATTEMPTS; i++)
    {
        uint8_t state;
        regis_result = zv_cc1101_read_status_register(CC1101_STATUS_MARCSTATE, &state);
        if (regis_result != ESP_OK)
            return regis_result;

        state &= 0x1F;
        if (state == CC1101_MARCSTATE_RX)
            return ESP_OK;

        esp_rom_delay_us(CC1101_RX_WAIT_INTERVAL_US);
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t zv_cc1101_capture(zv_cc1101_capture_t *out, uint32_t timeout_ms)
{
    if (out == NULL)
        return ESP_ERR_INVALID_ARG;

    ESP_RETURN_ON_ERROR(zv_cc1101_rmt_rx_setup(), TAG, "rmt install");

    out->count = 0;
    xQueueReset(sr_rx_queue);

    ESP_RETURN_ON_ERROR(zv_cc1101_start_rx(), TAG, "Problems starting module in RX");

    rmt_receive_config_t rxc = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = 15 * 1000 * 1000,
    };

    static rmt_symbol_word_t rx_symbols[CC1101_RMT_SYMBOL_COUNT];
    esp_err_t result = rmt_receive(s_rx_chan_handle, rx_symbols, sizeof(rx_symbols), &rxc);
    if (result != ESP_OK) 
    {
        ESP_LOGE(TAG, "rmt receive");

        zv_cc1101_rmt_rx_cleanup();
        esp_err_t listen_reset = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
        if (listen_reset != ESP_OK)
            return listen_reset;

        return result;
    }

    rmt_rx_done_event_data_t evt;
    if (xQueueReceive(sr_rx_queue, &evt, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        esp_err_t first_error = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
        esp_err_t cleanup_error = rmt_disable(s_rx_chan_handle);
        if (first_error == ESP_OK)
            first_error = cleanup_error;

        if (first_error != ESP_OK)
            return first_error;

        xQueueReset(sr_rx_queue);
        esp_err_t res = rmt_enable(s_rx_chan_handle);
        if (res != ESP_OK)
            return res;

        return ESP_ERR_TIMEOUT;
    }

    out->freq_hz = FREQ_433_HZ;
    for (size_t i = 0; i < evt.num_symbols && out->count < CC1101_CAPTURE_MAX_DURATIONS; i++)
    {
        rmt_symbol_word_t s = evt.received_symbols[i];
        if (s.duration0 > 0 && out->count < CC1101_CAPTURE_MAX_DURATIONS)
            out->durations_us[out->count++] = sing_by_level(s.level0, s.duration0);

        if (s.duration1 > 0 && out->count < CC1101_CAPTURE_MAX_DURATIONS)
            out->durations_us[out->count++] = sing_by_level(s.level1, s.duration1);
    }

    esp_err_t res = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
    if (res != ESP_OK)
        return res;

    return ESP_OK;
}

void zv_cc1101_deinit(void)
{
    zv_cc1101_rmt_rx_cleanup();
    cc_device_handle = NULL;
}
