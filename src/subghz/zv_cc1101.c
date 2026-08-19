#include "zv_cc1101.h"

#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_rx.h"
#include "freertos/queue.h"

#define CC1101_RMT_SYMBOL_COUNT 128
#define FREQ_433_HZ     433920000
#define CC1101_RX_WAIT_MAX_ATTEMPTS    100
#define CC1101_RX_WAIT_INTERVAL_US     100

static const char *TAG = "ZV_CC1101";

typedef struct {
    spi_device_handle_t cc_device_handle;
    rmt_channel_handle_t s_rx_chan_handle;
    QueueHandle_t sr_rx_queue;
    uint32_t frequency;
} cc1101_states_ctx;
typedef struct {
    const char *name;
    uint8_t address;
    uint8_t value;
} cc1101_register_config_t;

static cc1101_states_ctx context;

/*
 * This setup turns the CC1101 into a simple 433.92 MHz OOK receiver.
 * We deliberately bypass its packet decoder: GDO0 carries the raw
 * demodulated HIGH/LOW signal, and the ESP32 measures the pulse lengths.
 */
static const cc1101_register_config_t cc1101_base_config[] = {
    { "IOCFG2",   0x00, 0x0E }, // GDO2 reports carrier sense (not currently used)
    { "IOCFG0",   0x02, 0x0D }, // GDO0 outputs the raw demodulated data
    { "FIFOTHR",  0x03, 0x47 },
    { "PKTCTRL0", 0x08, 0x32 }, // asynchronous serial mode; no packet framing
    { "FREQ2",    0x0D, 0x10 }, // the three FREQ registers tune the radio to 433.92 MHz
    { "FREQ1",    0x0E, 0xB0 },
    { "FREQ0",    0x0F, 0x71 },
    { "MDMCFG4",  0x10, 0x67 }, // receiver bandwidth is about 270 kHz
    { "MDMCFG3",  0x11, 0x83 },
    { "MDMCFG2",  0x12, 0x30 }, // ASK/OOK modulation, with sync-word detection disabled
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
    // SIDLE stops the current radio operation
    esp_err_t result = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
    if (result != ESP_OK)
        return result;

    result = zv_cc1101_send_strobe(CC1101_STROBE_SFRX); // SFRX clears stale RX data
    if (result != ESP_OK)
        return result;

    result = zv_cc1101_send_strobe(CC1101_STROBE_SRX); // SRX starts reception
    if (result != ESP_OK)
        return result;

    return ESP_OK;
}

static esp_err_t zv_cc1101_read_register(uint8_t address, uint8_t burst, uint8_t *output_value)
{
    if (output_value == NULL)
        return ESP_ERR_INVALID_ARG;

    if (context.cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    /*
     * A CC1101 SPI header contains the read/write bit, the burst bit, and
     * a 6-bit register address. Status registers are a small oddity: even
     * when reading one byte, the datasheet requires the READ_BURST bit.
     *
     * For example, reading the PARTNUM status register builds the header
     * byte by combining its address with the read and burst bits:
     *
     *   PARTNUM address (0x30)  = 0011 0000
     *   READ_BURST flag (0xC0)  = 1100 0000
     *                            --------- OR
     *   SPI header byte         = 1111 0000 (0xF0)
     *
     * The first byte sent is this header. The second byte is only padding;
     * its clock pulses let the CC1101 shift the register value back to us.
     */

    uint8_t rx[2] = { 0 };
    // tx[0]: header (read + burst); tx[1]: 0x00 padding so the chip can clock out the value into rx[1]
    uint8_t tx[2] = { (uint8_t)(address | burst), 0x00 };

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };

    esp_err_t err = spi_device_polling_transmit(context.cc_device_handle, &transaction);
    if (err != ESP_OK)
        return err;

    *output_value = rx[1];
    return ESP_OK;
}

static void zv_cc1101_rmt_rx_cleanup(void)
{
    if (context.s_rx_chan_handle != NULL)
    {
        rmt_disable(context.s_rx_chan_handle);
        rmt_del_channel(context.s_rx_chan_handle);
        context.s_rx_chan_handle = NULL;
    }

    if (context.sr_rx_queue != NULL)
    {
        vQueueDelete(context.sr_rx_queue);
        context.sr_rx_queue = NULL;
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
    if (context.s_rx_chan_handle)
        return ESP_OK;

    context.sr_rx_queue = xQueueCreate(2, sizeof(rmt_rx_done_event_data_t));
    if (context.sr_rx_queue == NULL)
        return ESP_ERR_NO_MEM;
    
    rmt_rx_channel_config_t cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 1 * 1000 * 1000,    /* 1 MHz = 1 µs/tick */
        .mem_block_symbols = CC1101_RMT_SYMBOL_COUNT,
        .gpio_num          = ZV_SUB_GHZ_PIN_GDO0,
        .flags.invert_in   = false, 
        .flags.with_dma    = false,
    };

    esp_err_t err  = rmt_new_rx_channel(&cfg, &context.s_rx_chan_handle);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_rx_cleanup();
        return err;
    }

    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rmt_rx_done_cb };
    err = rmt_rx_register_event_callbacks(context.s_rx_chan_handle, &cbs, context.sr_rx_queue);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_rx_cleanup();
        return err;
    }

    err = rmt_enable(context.s_rx_chan_handle);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_rx_cleanup();
        return err;
    }

    return ESP_OK;
}

esp_err_t zv_cc1101_send_strobe(uint8_t command)
{
    if (context.cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    spi_transaction_t transaction = {
        .length    = 8,
        .tx_buffer = &command
    };

    return spi_device_polling_transmit(context.cc_device_handle, &transaction);
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
    if (context.cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    uint8_t tx[2] = { address, value };
    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx,
    };

    return spi_device_polling_transmit(context.cc_device_handle, &transaction);
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

    /*
     * RSSI is stored as an 8-bit two's-complement number. The CC1101
     * formula is RSSI_dBm = RSSI_dec / 2 - RSSI_offset. Around 433 MHz,
     * 74 dB is the recommended offset.
     */
    int8_t r = (int8_t)((raw >= 128) ? (int)raw - 256 : raw);

    *rssi_dbm = (r / 2) - 74;   /* offset = 74 dBm para 433 MHz */
    return ESP_OK;
}

esp_err_t zv_cc1101_init(spi_device_handle_t handle)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;

    context.cc_device_handle = handle;
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

void zv_cc1101_set_frequency(uint32_t frequency)
{
    context.frequency = frequency;
}

/**
 *  Data flow from a CC1101 capture -> RMT -> pulses:
 *
 *  RF signal
 *      |
 *      |---> CC1101 demodulates OOK and outputs the digital levels via GDO0 pin
 *            |
 *            |----> RMT RX measures the duration of each logic level (resolution: 1 us)
 *                  |
 *                  |---> rmt_rx_done_cb() copies the event to sr_rx_queue from the ISR
 *                       |
 *                       |---> This task receives the event and converts each RMT symbol:
 *                          |-------> high level    -> positive duration
 *                          |-------> low level     -> negative duration
 *                          |
 *                          |-----> out->durations_us[] contains the final sequence of pulses
 *
 * ======================================================================================================
 *
 *  Call sequence:
 *
 *    zv_cc1101_rmt_rx_setup()
 *     |- create sr_rx_queue
 *     |- configures GDO0 as the input for the RMT RX channel
 *     |- registers rmt_rx_done_cb()
 *     |- enables the RMT channel
 *
 *   xQueueReset()
 *     -> Exclude pending events from a previous capture
 *
 *   zv_cc1101_start_rx()
 *     -> SIDLE -> SFRX -> SRX
 *
 *   rmt_receive()
 *     -> Create an array `rx_symbols[]` to store the durations
 *
 *   xQueueReceive(timeout_ms)
 *     |- received event -> converts duration0/duration1 and ends in SIDLE
 *     `- timeout        -> stops RX, restarts RMT, and returns ESP_ERR_TIMEOUT
 */
esp_err_t zv_cc1101_capture(zv_cc1101_capture_t *out, uint32_t timeout_ms)
{
    if (out == NULL)
        return ESP_ERR_INVALID_ARG;

    ESP_RETURN_ON_ERROR(zv_cc1101_rmt_rx_setup(), TAG, "rmt install");

    out->count = 0;
    xQueueReset(context.sr_rx_queue);

    ESP_RETURN_ON_ERROR(zv_cc1101_start_rx(), TAG, "Problems starting module in RX");

    rmt_receive_config_t rxc = {
        /*
         * Pulses shorter than 1 us are treated as glitches. A level that
         * lasts 15 ms ends the current reception, which gives us a natural
         * gap between many common remote-control frames.
         */
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = 15 * 1000 * 1000,
    };

    static rmt_symbol_word_t rx_symbols[CC1101_RMT_SYMBOL_COUNT];
    esp_err_t result = rmt_receive(context.s_rx_chan_handle, rx_symbols, sizeof(rx_symbols), &rxc);
    if (result != ESP_OK) 
    {
        ESP_LOGE(TAG, "RMT could not start capturing pulses from CC1101 GDO0: %s",
                 esp_err_to_name(result));

        zv_cc1101_rmt_rx_cleanup();
        esp_err_t cleanup_result = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
        if (cleanup_result != ESP_OK)
        {
            ESP_LOGE(TAG, "CC1101 could not return to IDLE after the RMT failure: %s",
                     esp_err_to_name(cleanup_result));
        }

        return result;
    }

    rmt_rx_done_event_data_t evt;
    // Block the calling task until the RMT callback places a completed
    // reception event in the queue, or until timeout_ms expires.
    if (xQueueReceive(context.sr_rx_queue, &evt, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        esp_err_t first_error = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
        esp_err_t cleanup_error = rmt_disable(context.s_rx_chan_handle);
        if (first_error == ESP_OK)
            first_error = cleanup_error;

        if (first_error != ESP_OK)
            return first_error;

        xQueueReset(context.sr_rx_queue);
        esp_err_t res = rmt_enable(context.s_rx_chan_handle);
        if (res != ESP_OK)
            return res;

        return ESP_ERR_TIMEOUT;
    }

    out->freq_hz = context.frequency > 0 ? context.frequency : FREQ_433_HZ;
    // One RMT symbol holds two consecutive logic levels. Store both as signed pulse lengths:
    //  - positive means HIGH and negative means LOW.
    // This keeps the waveform ordering without needing a separate level array.

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
    context.cc_device_handle = NULL;
}
