#include "zv_cc1101.h"

#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "freertos/queue.h"

#define FREQ_433_HZ                         433920000
#define CC1101_CRYSTAL_FRECUENCY_HZ         26000000ULL
#define CC1101_RESOLUTION_HZ                1 * 1000 * 1000    // 1 MHz = 1 µs/tick
#define CC1101_RMT_SYMBOL_COUNT             128
#define CC1101_CAL_MAX_ATTEMPTS             3
#define CC1101_RMT_MAX_DURATION_TICKS       32767U
#define CC1101_RX_WAIT_MAX_ATTEMPTS         100
#define CC1101_RX_WAIT_INTERVAL_US          100
#define CC1101_ANALYZER_CONFIRM_SWEEPS       2U
#define CC1101_ANALYZER_RELEASE_SWEEPS       2U
#define CC1101_ANALYZER_MATCH_HZ             200000U

static const char *TAG = "ZV_CC1101";

typedef struct {
    spi_device_handle_t cc_device_handle;
    rmt_channel_handle_t s_rx_chan_handle;
    QueueHandle_t sr_rx_queue;

    rmt_channel_handle_t tx_chan_handle;
    rmt_encoder_handle_t encoder_handle;
    uint32_t frequency;
    bool frequency_valid;
} cc1101_states_ctx;

typedef struct {
    const char *name;
    uint8_t address;
    uint8_t value;
} cc1101_register_config_t;

typedef struct {
    zv_cc1101_modulation type;
    uint8_t value;
} cc1101_modulation_t;

// TODO: revisar smartRFStudio para tener valores correctos
static const cc1101_modulation_t default_modulations[] = {
    { OOK_ASK, 0x30 },
    { FSK, 0x00 },
    { GFSK, 0x10 }
};

typedef struct {
    uint32_t frequency_hz;
    uint8_t test0;
    uint8_t fscal2;
} cc1101_tuning_profile_t;

static const cc1101_tuning_profile_t tuning_profiles[] = {
    { 315000000U, 0x0B, 0x0A },
    { 380000000U, 0x0B, 0x0A },
    { 381000000U, 0x0B, 0x0A },
    { 433920000U, 0x09, 0x2A },
    { 868000000U, 0x09, 0x2A },
    { 915000000U, 0x09, 0x2A },
};

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
    { "FREQ2",    CC1101_FREQ2_ADDRESS, 0x10 }, // the three FREQ registers tune the radio to 433.92 MHz
    { "FREQ1",    CC1101_FREQ1_ADDRESS, 0xB0 },
    { "FREQ0",    CC1101_FREQ0_ADDRESS, 0x71 },
    { "MDMCFG4",  CC1101_MDMCFG4_ADDRESS, 0x67 }, // receiver bandwidth is about 270 kHz
    { "MDMCFG3",  0x11, 0x83 },
    { "MDMCFG2",  CC1101_MDMCFG2_ADDRESS, 0x30 }, // ASK/OOK modulation, with sync-word detection disabled
    { "MCSM0",    0x18, 0x18 },
    { "FOCCFG",   0x19, 0x14 }, // ASK/OOK requires FOC_LIMIT=0
    { "AGCCTRL2", 0x1B, 0x03 },
    { "AGCCTRL1", 0x1C, 0x40 },
    { "AGCCTRL0", 0x1D, 0x91 },
    { "FREND0",   0x22, 0x11 }, // 0x11 = 0001 0001 - OOK/ASK
    { "FSCAL2",   CC1101_FSCAL2_ADDRESS, 0x2A }, // Select the high VCO for 433.92 MHz with TEST0=0x09
    { "TEST2",    0x2C, 0x81 },
    { "TEST1",    0x2D, 0x35 },
    { "TEST0",    0x2E, 0x09 },
};

static cc1101_states_ctx context;

static int32_t sing_by_level (uint16_t level, uint16_t duration)
{
    return level ? (int32_t)duration : -(int32_t)duration;
}

static esp_err_t zv_cc1101_start_rx(void)
{
    if (!context.frequency_valid)
        return ESP_ERR_INVALID_STATE;

    esp_err_t result = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
    if (result != ESP_OK)
        return result;

    result = zv_cc1101_wait_for_marc_state(CC1101_MARCSTATE_SIDLE);
    if (result != ESP_OK)
        return result;

    result = zv_cc1101_send_strobe(CC1101_STROBE_SFRX);
    if (result != ESP_OK)
        return result;

    result = zv_cc1101_send_strobe(CC1101_STROBE_SRX);
    if (result != ESP_OK)
        return result;

    result = zv_cc1101_wait_for_marc_state(CC1101_MARCSTATE_RX);
    if (result != ESP_OK)
        return result; 

    uint8_t fscal1 = 0;
    if (result == ESP_OK)
        result = zv_cc1101_read_config_register(CC1101_FSCAL1_ADDRESS, &fscal1);

    if (result == ESP_OK && (fscal1 & 0x3F) == 0x3F)
        result = ESP_ERR_INVALID_RESPONSE;

    if (result != ESP_OK)
    {
        context.frequency_valid = false;
        zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
    }

    return result;
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

static esp_err_t zv_cc1101_write_register(uint8_t *command, size_t command_bits)
{
    spi_transaction_t transaction = {
        .length = command_bits,
        .tx_buffer = command,
    };

    return spi_device_polling_transmit(context.cc_device_handle, &transaction);
}

static void zv_cc1101_rmt_tx_cleanup(void)
{
    if (context.tx_chan_handle != NULL)
    {
        rmt_disable(context.tx_chan_handle);
        rmt_del_channel(context.tx_chan_handle);
        context.tx_chan_handle = NULL;
    }

    if (context.encoder_handle != NULL)
    {
        rmt_del_encoder(context.encoder_handle);
        context.encoder_handle = NULL;
    }
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

static esp_err_t zv_cc1101_rmt_tx_setup()
{
    zv_cc1101_rmt_rx_cleanup();

    if (context.tx_chan_handle)
        return ESP_OK;

    rmt_tx_channel_config_t tx_channel_config = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = CC1101_RESOLUTION_HZ,
        .mem_block_symbols = CC1101_RMT_SYMBOL_COUNT,
        .gpio_num          = ZV_SUB_GHZ_PIN_GDO0,
        .trans_queue_depth = 2,
        .flags = {
            .invert_out = false,
            .with_dma   = false,
        }
    };

    esp_err_t err = rmt_new_tx_channel(&tx_channel_config, &context.tx_chan_handle);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_tx_cleanup();
        return err;
    }

    rmt_copy_encoder_config_t encoder_config = {};
    err = rmt_new_copy_encoder(&encoder_config, &context.encoder_handle);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_tx_cleanup();
        return err;
    }

    err = rmt_enable(context.tx_chan_handle);
    if (err != ESP_OK)
    {
        zv_cc1101_rmt_tx_cleanup();
        return err;
    }

    return ESP_OK;
}

static esp_err_t zv_cc1101_rmt_rx_setup()
{
    zv_cc1101_rmt_tx_cleanup();

    if (context.s_rx_chan_handle)
        return ESP_OK;

    context.sr_rx_queue = xQueueCreate(2, sizeof(rmt_rx_done_event_data_t));
    if (context.sr_rx_queue == NULL)
        return ESP_ERR_NO_MEM;
    
    rmt_rx_channel_config_t cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = CC1101_RESOLUTION_HZ,
        .mem_block_symbols = CC1101_RMT_SYMBOL_COUNT,
        .gpio_num          = ZV_SUB_GHZ_PIN_GDO0,
        .flags.invert_in   = false, 
        .flags.with_dma    = false,
    };

    esp_err_t err = rmt_new_rx_channel(&cfg, &context.s_rx_chan_handle);
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
    context.frequency_valid = false;
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

    context.frequency = FREQ_433_HZ;
    context.frequency_valid = true;

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

    if (!context.frequency_valid)
        return ESP_ERR_INVALID_STATE;

    uint8_t state;
    esp_err_t res = zv_cc1101_read_status_register(CC1101_STATUS_MARCSTATE, &state);
    if (res != ESP_OK)
        return res;

    if ((state & 0x1F) != CC1101_MARCSTATE_RX)
        return ESP_ERR_INVALID_STATE;

    esp_rom_delay_us(3000);

    uint8_t raw;
    res = zv_cc1101_read_status_register(CC1101_STATUS_RSSI, &raw);
    if (res != ESP_OK)
        return res;

    /*
     * RSSI is stored as an 8-bit two's-complement number. The CC1101
     * formula is RSSI_dBm = RSSI_dec / 2 - RSSI_offset. Around 433 MHz,
     * 74 dB is the recommended offset.
     */
    int8_t r = (int8_t)((raw >= 128) ? (int)raw - 256 : raw);

    int16_t dbm = (r / 2) - 74;
    // Preserve the public int8_t API without wrapping weak readings positive.
    *rssi_dbm = dbm < INT8_MIN ? INT8_MIN : (int8_t)dbm;

    return ESP_OK;
}

esp_err_t zv_cc1101_init(spi_device_handle_t handle)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;

    context.cc_device_handle = handle;
    context.frequency = 0;
    context.frequency_valid = false;

    return ESP_OK;
}

esp_err_t zv_cc1101_wait_for_marc_state(uint8_t waiting_state)
{
    // Confirm two consecutive matches to tolerate transient SPI status reads.

    bool previous_match = false;
    esp_err_t regis_result = ESP_OK;
    for (int i = 0; i < CC1101_RX_WAIT_MAX_ATTEMPTS; i++)
    {
        uint8_t state;
        regis_result = zv_cc1101_read_status_register(CC1101_STATUS_MARCSTATE, &state);
        if (regis_result != ESP_OK)
            return regis_result;

        state &= 0x1F;
        if (state == waiting_state && previous_match)
            return ESP_OK;

        previous_match = state == waiting_state;
        esp_rom_delay_us(CC1101_RX_WAIT_INTERVAL_US);
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t zv_cc1101_set_modulation(zv_cc1101_modulation new_modulation)
{
    uint8_t modulation_value = 0;
    size_t modulation_amount = sizeof (default_modulations) / sizeof (default_modulations[0]);
    for (size_t i = 0; i < modulation_amount; i++)
    {
        if (default_modulations[i].type == new_modulation)
        {
            modulation_value = default_modulations[i].value;
            break;
        }
    }

    esp_err_t success = zv_cc1101_write_config_register(CC1101_MDMCFG2_ADDRESS, modulation_value);
    if (success != ESP_OK)
        return success;

    return ESP_OK;
}

esp_err_t zv_cc1101_set_fine_tuning(uint32_t freq_hz)
{
    // by default is 433 frecuency
    cc1101_tuning_profile_t profile = {
        .frequency_hz = FREQ_433_HZ,
        .fscal2 = tuning_profiles[3].fscal2,
        .test0 = tuning_profiles[3].test0
    };

    size_t tuning_length = sizeof(tuning_profiles) / sizeof(tuning_profiles[0]);
    for (size_t i = 0; i < tuning_length; i++)
    {
        if (freq_hz == tuning_profiles[i].frequency_hz)
        {
            profile = tuning_profiles[i];
            break;
        }
    }

    // TODO: Esto es esta para validar, estas frecuencias el datahsheet dice que estan fuera del rango del modulo
    // Son valores para probar que funcionan.
    if (freq_hz >= 380000000U && freq_hz <= 381000000U)
    {
        esp_err_t result_wr = zv_cc1101_write_config_register(CC1101_MDMCFG4_ADDRESS, 0x57);
        if (result_wr != ESP_OK)
            return result_wr;

        uint8_t new_value;
        result_wr = zv_cc1101_read_config_register(CC1101_MDMCFG4_ADDRESS, &new_value);
        if (result_wr != ESP_OK)
            return result_wr;

        if (0x57 != new_value)
            return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

esp_err_t zv_cc1101_set_frequency(uint32_t freq_hz)
{
    if (context.cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    context.frequency_valid = false;

    // f_base = (f_xosc / 2^16) * FREQ
    // Referencia: CC1101 datasheet, revision I, pagina 75,
    // seccion 29.1, registro 0x0D: FREQ2
    uint32_t word = (uint32_t)((((uint64_t)freq_hz << 16) + CC1101_CRYSTAL_FRECUENCY_HZ / 2) / CC1101_CRYSTAL_FRECUENCY_HZ);

    cc1101_register_config_t reg_value[] = {
        { "FREQ2", CC1101_FREQ2_ADDRESS, (uint8_t)(word >> 16) },
        { "FREQ1", CC1101_FREQ1_ADDRESS, (uint8_t)(word >> 8) },
        { "FREQ0", CC1101_FREQ0_ADDRESS, (uint8_t)word },
    };

    esp_err_t result = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
    if (result != ESP_OK)
        return result;

    result = zv_cc1101_wait_for_marc_state(CC1101_MARCSTATE_SIDLE);
    if (result != ESP_OK)
        return result;

    size_t registers_length = sizeof (reg_value) / sizeof(reg_value[0]);
    for (size_t i = 0; i < registers_length; i++)
    {
        esp_err_t result_wr = zv_cc1101_write_config_register(reg_value[i].address, reg_value[i].value);
        if (result_wr != ESP_OK)
            return result_wr;

        uint8_t new_value;
        result_wr = zv_cc1101_read_config_register(reg_value[i].address, &new_value);
        if (result_wr != ESP_OK)
            return result_wr;

        if (reg_value[i].value != new_value)
            return ESP_ERR_INVALID_RESPONSE;
    }

    context.frequency = freq_hz;
    context.frequency_valid = true;

    return ESP_OK;
}

esp_err_t zv_cc1101_frecuency_analyzer(void)
{
    // TODO: valores de prueba solo haciendo barrido de frecuencias en 433 mhz
    uint32_t start_hz = 433000000U;
    uint32_t end_hz = 435000000U;
    uint32_t step_hz = 100000U;
    uint32_t duration_ms = 15000U;
    // int8_t threshold_dbm = -70;

    if (step_hz == 0 || start_hz > end_hz || duration_ms == 0)
        return ESP_ERR_INVALID_ARG;

    if (context.cc_device_handle == NULL || !context.frequency_valid)
        return ESP_ERR_INVALID_STATE;
  
    esp_err_t result = ESP_OK;
    while (result == ESP_OK )
    {
        uint64_t frequency;
        for (frequency = start_hz; frequency <= end_hz; frequency += step_hz)
        {
            //TODO:
            // 1.- hacer set de la nueva frecuencia
            // 2.- comenzar a escuchar rx
            // 3.- leer valor de rssi
            // 4.- encontrar el valor de rssi mas alto y dejarlo como frecuencia encontrada.
        }

        if (result != ESP_OK || frequency <= end_hz)
            break;

        vTaskDelay(1);
    }

    // TODO: valver a la frecuencia inicial y dejar en escucha el modulo

    return ESP_OK;
}


static esp_err_t zv_cc1101_finish_transmission (esp_err_t error)
{
    esp_err_t result = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
    if (result == ESP_OK)
        result = zv_cc1101_wait_for_marc_state(CC1101_MARCSTATE_SIDLE);

    zv_cc1101_rmt_tx_cleanup();
    return  error != ESP_OK ? error : result;
}

esp_err_t zv_cc1101_transmit(const zv_cc1101_capture_t *capture, uint16_t repetitions)
{
    if (capture == NULL || repetitions == 0)
        return ESP_ERR_INVALID_ARG;

    if (!context.frequency_valid || capture->freq_hz != context.frequency)
        return ESP_ERR_INVALID_STATE;

    if (capture->count == 0 || capture->count > CC1101_CAPTURE_MAX_DURATIONS)
        return ESP_ERR_INVALID_SIZE;

    size_t symbols_count = 0;
    static rmt_symbol_word_t tx_symbols[CC1101_RMT_SYMBOL_COUNT];    
    for (uint16_t i = 0; i < capture->count; i += 2)
    {
        if (symbols_count >= CC1101_RMT_SYMBOL_COUNT)
            return ESP_ERR_INVALID_SIZE;

        int32_t pulse0 = capture->durations_us[i];
        bool has_second = i + 1 < capture->count;
        int32_t pulse1 = has_second ? capture->durations_us[i + 1] : 0;
        uint32_t ticks0 = pulse0 < 0 ? (uint32_t)(-(int64_t)pulse0) : (uint32_t)pulse0;
        uint32_t ticks1 = pulse1 < 0 ? (uint32_t)(-(int64_t)pulse1) : (uint32_t)pulse1;

        /*
        * Each RMT symbol stores two pulse durations, each using a 15-bit field.
        * Valid durations range from 1 to 32767 ticks; zero marks the end of a transmission.
        *
        * With the RMT configured at 1 MHz, each tick represents 1 microsecond (µs).
        * This allows a maximum pulse duration of 32767 µs (~32.7 ms).
        *
        * If the RMT resolution changes, pulse durations must be converted from microseconds to ticks before validation. 
        * The 32767-tick hardware limit remains unchanged, but the maximum representable time changes accordingly.
        */
        if (ticks0 == 0 || (has_second && ticks1 == 0) || ticks0 > CC1101_RMT_MAX_DURATION_TICKS || ticks1 > CC1101_RMT_MAX_DURATION_TICKS)
            return ESP_ERR_INVALID_ARG;

        rmt_symbol_word_t symbol = {
            .level0 = pulse0 > 0, 
            .duration0 = ticks0,
            .level1 = pulse1 > 0, 
            .duration1 = ticks1,
        };

        tx_symbols[symbols_count++] = symbol;
    }

    esp_err_t result = zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);
    if (result != ESP_OK)
        return zv_cc1101_finish_transmission(result);

    result = zv_cc1101_wait_for_marc_state(CC1101_MARCSTATE_SIDLE);
    if (result != ESP_OK)
        return zv_cc1101_finish_transmission(result);

    result = zv_cc1101_rmt_tx_setup();
    if (result != ESP_OK)
        return zv_cc1101_finish_transmission(result);

    uint8_t command[] = { CC1101_PATABLE_ADDRESS | CC1101_WRITE_BURST, 0x00, 0x60 };
    result = zv_cc1101_write_register(command, sizeof(command) * 8);
    if (result != ESP_OK)
        return zv_cc1101_finish_transmission(result);
    
    result = zv_cc1101_send_strobe(CC1101_STROBE_STX);
    if (result != ESP_OK)
        return zv_cc1101_finish_transmission(result);

    result = zv_cc1101_wait_for_marc_state(CC1101_MARCSTATE_TX);
    if (result != ESP_OK)
        return zv_cc1101_finish_transmission(result);

    rmt_transmit_config_t tx_config = { .loop_count = 0, .flags.eot_level = 0 };
    for (uint16_t repeat = 0; repeat < repetitions; repeat++)
    {
        result = rmt_transmit(context.tx_chan_handle, context.encoder_handle, tx_symbols,
                              symbols_count * sizeof(tx_symbols[0]), &tx_config);

        if (result != ESP_OK)
            return zv_cc1101_finish_transmission(result);

        result = rmt_tx_wait_all_done(context.tx_chan_handle, 2000);
        if (result != ESP_OK)
            return zv_cc1101_finish_transmission(result);
    }

    return zv_cc1101_finish_transmission(result);
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

    ESP_RETURN_ON_ERROR(zv_cc1101_rmt_rx_setup(), TAG, "rmt rx install");

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

    out->freq_hz = context.frequency;
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
    zv_cc1101_send_strobe(CC1101_STROBE_SIDLE);

    zv_cc1101_rmt_rx_cleanup();
    zv_cc1101_rmt_tx_cleanup();

    context.cc_device_handle = NULL;
    context.frequency = 0;
    context.frequency_valid = false;
}
