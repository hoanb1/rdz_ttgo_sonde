/*!
 * \file      sx126x_long_pkt.c
 *
 * \brief     sx126x long packet library
 *
 * \copyright Revised BSD License, see section \ref LICENSE.
 *
 * \code
 *                ______                              _
 *               / _____)             _              | |
 *              ( (____  _____ ____ _| |_ _____  ____| |__
 *               \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 *               _____) ) ____| | | || |_| ____( (___| | | |
 *              (______/|_____)_|_|_| \__)_____)\____)_| |_|
 *              (C)2019-2019 Semtech
 *
 * \endcode
 *
 * \authors    Semtech WSP Applications Team
 */
#include "sx126x_long_pkt.h"
#include "sx126x_regs.h"

/// @cond

#define SX126X_REG_RX_ADDR_POINTER 0x0803
#define SX126X_REG_RXTX_PAYLOAD_LEN 0x06BB

/// @endcond

static sx126x_status_t sx126x_long_pkt_check_gfsk_pkt_params( const sx126x_pkt_params_gfsk_t* params );

sx126x_status_t sx126x_long_pkt_rx_set_gfsk_pkt_params( const void* context, const sx126x_pkt_params_gfsk_t* params )
{
    sx126x_status_t status = sx126x_long_pkt_check_gfsk_pkt_params( params );
    if( status != SX126X_STATUS_OK )
    {
        return status;
    }

    sx126x_pkt_params_gfsk_t pkt_params = *params;

    // Tell the receiver that it should receive as many packets as possible
    pkt_params.pld_len_in_bytes = SX126X_SHORT_PACKET_MAX;

    return sx126x_set_gfsk_pkt_params( context, &pkt_params );
}

sx126x_status_t sx126x_long_pkt_set_rx( const void* context, struct sx126x_long_pkt_rx_state* state, uint32_t timeout )
{
    uint8_t tmp = SX126X_SHORT_PACKET_MAX;

    state->index = 0;

    sx126x_status_t status = sx126x_write_register( context, SX126X_REG_RXTX_PAYLOAD_LEN, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    return sx126x_set_rx( context, timeout );
}

sx126x_status_t sx126x_long_pkt_set_rx_with_timeout_in_rtc_step( const void* context, struct sx126x_long_pkt_rx_state* state, uint32_t timeout )
{
    uint8_t tmp = SX126X_SHORT_PACKET_MAX;

    state->index = 0;

    sx126x_status_t status = sx126x_write_register( context, SX126X_REG_RXTX_PAYLOAD_LEN, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    return sx126x_set_rx_with_timeout_in_rtc_step( context, timeout );
}

sx126x_status_t sx126x_long_pkt_rx_get_partial_payload( const void* context, struct sx126x_long_pkt_rx_state* state,
                                                        uint8_t* dest, unsigned int max_len, uint8_t* num_bytes_read )
{
    uint8_t num_bytes_available;
    uint8_t current_index;

    // Find how many bytes of data are available
    sx126x_status_t status = sx126x_read_register( context, SX126X_REG_RX_ADDR_POINTER, &current_index, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    num_bytes_available = current_index - state->index;

    if( num_bytes_available > max_len )
    {
        num_bytes_available = max_len;
        current_index       = state->index + num_bytes_available;
    }

    // Fool the transceiver into keeping rx on for 255 more bytes.
    uint8_t tmp = current_index - 1;
    status      = sx126x_write_register( context, SX126X_REG_RXTX_PAYLOAD_LEN, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    if( num_bytes_available > 0 )
    {
        status = sx126x_read_buffer( context, state->index, dest, num_bytes_available );

        state->index = current_index;
    }

    *num_bytes_read = num_bytes_available;

    return status;
}

uint8_t sx126x_long_pkt_rx_check_for_last( unsigned int num_remaining, unsigned int stop_margin )
{
    if( stop_margin > SX126X_SHORT_PACKET_MAX )
    {
        stop_margin = SX126X_SHORT_PACKET_MAX;
    }
    if( num_remaining < stop_margin )
    {
        num_remaining = stop_margin;
    }
    if( num_remaining >= 255 )
    {
        return 0;
    }
    return num_remaining;
}

sx126x_status_t sx126x_long_pkt_rx_prepare_for_last( const void* context, struct sx126x_long_pkt_rx_state* state,
                                                     uint8_t stop_offset )
{
    uint8_t end = state->index + stop_offset;
    return sx126x_write_register( context, SX126X_REG_RXTX_PAYLOAD_LEN, &end, 1 );
}

sx126x_status_t sx126x_long_pkt_rx_complete( const void* context )
{
    return sx126x_set_standby( context, SX126X_STANDBY_CFG_RC );
}

sx126x_status_t sx126x_long_pkt_tx_bitbang_activate( const void*                           context,
                                                     struct sx126x_long_pkt_tx_io_storage* storage )
{
    uint8_t         tmp;
    sx126x_status_t status;

    status = sx126x_read_register( context, 0x680, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;
    if( storage != 0 )
    {
        storage->state1 = tmp & 0x70;
    }
    tmp    = ( tmp & ~0x70 ) | 0x10;
    status = sx126x_write_register( context, 0x680, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    status = sx126x_read_register( context, 0x0587, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;
    if( storage != 0 )
    {
        storage->state1 |= tmp & 0x0F;
    }
    tmp    = ( tmp & ~0x0F ) | 0x0C;
    status = sx126x_write_register( context, 0x587, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    status = sx126x_read_register( context, 0x0580, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;
    if( storage != 0 )
    {
        storage->state2 |= tmp & ( 1 << 3 );
    }
    tmp    = tmp | 1 << 3;
    status = sx126x_write_register( context, 0x580, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    status = sx126x_read_register( context, 0x0583, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;
    if( storage != 0 )
    {
        storage->state2 |= ( tmp & ( 1 << 3 ) ) << 1;
    }
    tmp    = tmp | 1 << 3;
    status = sx126x_write_register( context, 0x583, &tmp, 1 );

    return status;
}

sx126x_status_t sx126x_long_pkt_tx_bitbang_restore( const void*                                 context,
                                                    const struct sx126x_long_pkt_tx_io_storage* storage )
{
    uint8_t         tmp;
    sx126x_status_t status;

    status = sx126x_read_register( context, 0x680, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;
    tmp    = ( tmp & ~0x70 ) | ( storage->state1 & 0x70 );
    status = sx126x_write_register( context, 0x680, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    status = sx126x_read_register( context, 0x0587, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;
    tmp    = ( tmp & ~0x0F ) | ( storage->state1 & 0x0F );
    status = sx126x_write_register( context, 0x587, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    status = sx126x_read_register( context, 0x0580, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;
    tmp    = ( tmp & ~( 1 << 3 ) ) | ( storage->state2 & ( 1 << 3 ) );
    status = sx126x_write_register( context, 0x580, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;

    status = sx126x_read_register( context, 0x0583, &tmp, 1 );
    if( status != SX126X_STATUS_OK ) return status;
    tmp    = ( tmp & ~( 1 << 3 ) ) | ( ( storage->state2 & ( 1 << 4 ) ) >> 1 );
    status = sx126x_write_register( context, 0x583, &tmp, 1 );

    return status;
}

sx126x_status_t sx126x_long_pkt_tx_set_gfsk_pkt_params( const void*                              context,
                                                        const sx126x_long_pkt_pkt_params_gfsk_t* params )
{
    sx126x_status_t status = sx126x_long_pkt_check_gfsk_pkt_params( params->pkt_params );
    if( status != SX126X_STATUS_OK )
    {
        return status;
    }

    sx126x_pkt_params_gfsk_t mod_params = *params->pkt_params;

    // Use payload and sync_word reservations for large packets.
    if( params->long_pld_len_in_bytes >= 512 )
    {
        mod_params.pld_len_in_bytes      = 255;
        mod_params.sync_word_len_in_bits = 64;
    }
    else
    {
        mod_params.pld_len_in_bytes      = 1;
        mod_params.sync_word_len_in_bits = 0;
    }

    unsigned int pbl_len_in_bits = params->pkt_params->sync_word_len_in_bits + params->pkt_params->pld_len_in_bytes*8 +
                                   ( params->long_pld_len_in_bytes << 3 ) - ( mod_params.pld_len_in_bytes << 3 ) -
                                   mod_params.sync_word_len_in_bits;

    if( pbl_len_in_bits > 65535 )
    {
        return SX126X_STATUS_ERROR;
    }

    mod_params.pld_len_in_bytes = pbl_len_in_bits/8;

    return sx126x_set_gfsk_pkt_params( context, &mod_params );
}

void sx126x_long_pkt_tx_bits_init( struct sx126x_long_pkt_tx_state*         state,
                                   const sx126x_long_pkt_pkt_params_gfsk_t* params, const uint8_t* sync_word,
                                   const uint8_t* payload )
{
    state->part            = SX126X_LONG_PACKET_TX_PART_PREAMBLE;
    state->offset          = 0;
    state->long_pkt_params = params;
    state->sync_word       = sync_word;
    state->payload         = payload;
}

bool sx126x_long_pkt_tx_bits_get( struct sx126x_long_pkt_tx_state* state, bool* bit_to_send )
{
    if( state->part == SX126X_LONG_PACKET_TX_PART_PREAMBLE )
    {
        *bit_to_send = state->offset % 2;
        if( state->offset++ < state->long_pkt_params->pkt_params->pld_len_in_bytes*8 )
        {
            return true;
        }
        state->part   = SX126X_LONG_PACKET_TX_PART_SYNC_WORD;
        state->offset = 0;
    }

    if( state->part == SX126X_LONG_PACKET_TX_PART_SYNC_WORD )
    {
        unsigned int byte_num = state->offset / 8;
        unsigned int bit_num  = state->offset % 8;
        if( state->offset++ < state->long_pkt_params->pkt_params->sync_word_len_in_bits )
        {
            *bit_to_send = ( ( state->sync_word[byte_num] >> ( 7 - bit_num ) ) & 0x01 ) != 0;
            return true;
        }
        state->part   = SX126X_LONG_PACKET_TX_PART_PAYLOAD;
        state->offset = 0;
    }

    if( state->part == SX126X_LONG_PACKET_TX_PART_PAYLOAD )
    {
        unsigned int byte_num = state->offset / 8;
        unsigned int bit_num  = state->offset % 8;
        if( state->offset++ < ( state->long_pkt_params->long_pld_len_in_bytes << 3 ) )
        {
            *bit_to_send = ( ( state->payload[byte_num] >> ( 7 - bit_num ) ) & 0x01 ) != 0;
            return true;
        }
        state->part   = SX126X_LONG_PACKET_TX_PART_DONE;
        state->offset = 0;
    }

    return false;
}

static sx126x_status_t sx126x_long_pkt_check_gfsk_pkt_params( const sx126x_pkt_params_gfsk_t* params )
{
    if( params->crc_type != SX126X_GFSK_CRC_OFF )
    {
        return SX126X_STATUS_UNSUPPORTED_FEATURE;
    }
    if( params->address_filtering != SX126X_GFSK_ADDRESS_FILTERING_DISABLE )
    {
        return SX126X_STATUS_UNSUPPORTED_FEATURE;
    }

    return SX126X_STATUS_OK;
}
