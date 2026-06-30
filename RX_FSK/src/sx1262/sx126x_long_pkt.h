/*!
 * \file      sx126x_long_pkt.h
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
#ifndef SX126X_LONG_PACKET_H_
#define SX126X_LONG_PACKET_H_

#include <stdint.h>
#include <stdbool.h>

#include "sx126x.h"

/**
 * Maximum number of bytes receivable in hardware
 */
#define SX126X_SHORT_PACKET_MAX 255

/**
 * Used to keep track of received chunks of data
 */
struct sx126x_long_pkt_rx_state
{
    uint8_t index;  //!< last retrieved rxAddrPtr
};

/**
 * Used to keep track of packet parts as they are transmitted
 */
enum sx126x_long_pkt_tx_part
{
    SX126X_LONG_PACKET_TX_PART_PREAMBLE,
    SX126X_LONG_PACKET_TX_PART_SYNC_WORD,
    SX126X_LONG_PACKET_TX_PART_PAYLOAD,
    SX126X_LONG_PACKET_TX_PART_DONE,
};

/**
 * pkt_params structure that adds long payload field
 */
typedef struct sx126x_long_pkt_pkt_params_gfsk_s
{
    const sx126x_pkt_params_gfsk_t* pkt_params;             //!< Pointer to standard packet parameters structure
    uint16_t                        long_pld_len_in_bytes;  //!< Long packet payload length, for tx
} sx126x_long_pkt_pkt_params_gfsk_t;

/**
 * This structure points to the next bit which will be returned by
 * sx126x_long_pkt_tx_bits_get()
 */
struct sx126x_long_pkt_tx_state
{
    const sx126x_long_pkt_pkt_params_gfsk_t* long_pkt_params;  //!< Points to long packet parameters
    const uint8_t*                           sync_word;        //!< Points to sync word
    const uint8_t*                           payload;          //!< Points to payload
    enum sx126x_long_pkt_tx_part             part;             //!< Indicates which part is currently being streamed
    unsigned int                             offset;           //!< Indicates byte offset in part current being streamed
};

/**
 * Stores the hardware configuration needed by
 * sx126x_long_pkt_tx_bitbang_restore()
 */
struct sx126x_long_pkt_tx_io_storage
{
    uint8_t state1;  //!< Opaque data structure for storage
    uint8_t state2;  //!< Opaque data structure for storage
};

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Configure the GFSK packet parameters for reception. Use this instead
 * of sx126x_set_gfsk_pkt_params().
 *
 * \param [in]  context Transceiver context.
 *
 * \param [in]  params  packet params structure.
 */
sx126x_status_t sx126x_long_pkt_rx_set_gfsk_pkt_params( const void* context, const sx126x_pkt_params_gfsk_t* params );

/*!
 * \brief Start reception. Use this instead of sx126x_set_rx().
 *
 * \param [in]  context Transceiver context.
 *
 * \param [out] state Reception state structure. Initialized here.
 *
 * \param [in]  timeout Reception timeout, passed to sx126x_set_rx().
 */
sx126x_status_t sx126x_long_pkt_set_rx( const void* context, struct sx126x_long_pkt_rx_state* state, uint32_t timeout );
sx126x_status_t sx126x_long_pkt_set_rx_with_timeout_in_rtc_step( const void* context, struct sx126x_long_pkt_rx_state* state, uint32_t timeout );
/*!
 * \brief Move bytes from the transceiver buffer to dest, making space in the
 * transceiver for future data.
 *
 * \param [in]  context Transceiver context.
 *
 * \param [in, out] state Reception state structure. Initialized by
 * sx126x_long_pkt_set_rx().
 *
 * \param [in]  dest Destination buffer for data transfer.
 *
 * \param [in]  max_len Number of bytes available in dest. Note that if the
 * transceiver contains more than max_len bytes of readable data, this data will
 * be left in the transceiver buffer, leaving less space available for new data
 * than if dest were large enough.
 *
 * \param [out] num_bytes_read Number of bytes that were actually transferred
 * from the transceiver buffer to dest.
 */
sx126x_status_t sx126x_long_pkt_rx_get_partial_payload( const void* context, struct sx126x_long_pkt_rx_state* state,
                                                        uint8_t* dest, unsigned int max_len, uint8_t* num_bytes_read );

/*!
 * \brief Check to see if this is the last ordinary long packet reception
 * iteration.
 *
 * \param [in]  num_remaining Indicates how many more bytes are desired.
 *
 * \param [in]  stop_margin Security margin [in bytes]
 *
 * stop_margin is used to avoid situations like the following, where the caller
 * requests one more byte but by the time this function has terminated that byte
 * has already been received. In this case, it is possible that the hardware
 * stop index index gets set too late, the radio misses it, and reads an entire
 * receive buffer of data before terminating (and clobbering the desired byte).
 * If, for instance, you are certain that a call to
 * sx126x_long_pkt_rx_prepare_for_last() will complete in 10 ms, convert
 * this time interval to bytes, using the data rate, and use it for stop_margin.
 *
 * \return stop_offset If this is the last ordinary iteration, return
 * stop_offset for sx126x_long_pkt_rx_prepare_for_last(). Otherwise,
 * return 0.
 */
uint8_t sx126x_long_pkt_rx_check_for_last( unsigned int num_remaining, unsigned int stop_margin );

/*!
 * \brief Prepare to read the final data packet and then trigger RX_DONE.
 *
 * \param [in]  context Transceiver context.
 *
 * \param [in, out] state Reception state structure. Initialized by
 * sx126x_long_pkt_set_rx().
 *
 * \param [in]  stop_offset Stop offset returned by
 * sx126x_long_pkt_rx_check_for_last().
 *
 * \note On RxDone, call sx126x_long_pkt_rx_get_partial_payload() to read the
 * final num_remaining bytes of data.
 */
sx126x_status_t sx126x_long_pkt_rx_prepare_for_last( const void* context, struct sx126x_long_pkt_rx_state* state,
                                                     uint8_t stop_offset );

/*!
 * \brief Cancel any reception that might be active, putting the transceiver
 * into standby more.
 *
 * \note If after a sequence of sx126x_long_pkt_rx_get_partial_payload() calls
 * no more data is needed, call this function to make sure that reception is
 * stopped and a spurious RxDone interrupt is not generated.
 *
 * \param [in]  context Transceiver context.
 */
sx126x_status_t sx126x_long_pkt_rx_complete( const void* context );

/*!
 * \brief Configure the transmitter to operate in long packet mode. This
 * configures DIO2 as a clock output, and DIO3 as a data input.
 *
 * \param [in]  context Transceiver context.
 *
 * \param [out] storage Optional structure used to store the current hardware
 * configuration. If storage is not needed, use 0. Otherwise, pass a pointerto
 * an uninitialized sx126x_long_pkt_tx_io_storage structure, and then pass the
 * same structure to sx126x_long_pkt_tx_bitbang_restore() to restore the
 * previous state.
 */
sx126x_status_t sx126x_long_pkt_tx_bitbang_activate( const void*                           context,
                                                     struct sx126x_long_pkt_tx_io_storage* storage );

/*!
 * \brief Deactivate the transmitter long packet configuration.
 *
 * \param [in]  context Transceiver context.
 *
 * \param [in]  storage Structure used to restore the hardware configuration
 * that was in use before calling sx126x_long_pkt_tx_bitbang_activate().
 */
sx126x_status_t sx126x_long_pkt_tx_bitbang_restore( const void*                                 context,
                                                    const struct sx126x_long_pkt_tx_io_storage* storage );

/*!
 * \brief Configure the GFSK packet parameters for transmission. Use this
 * instead of sx126x_set_gfsk_pkt_params().
 *
 * \param [in]  context Transceiver context.
 *
 * \param [in]  params  Augmented packet params structure.
 */
sx126x_status_t sx126x_long_pkt_tx_set_gfsk_pkt_params( const void*                              context,
                                                        const sx126x_long_pkt_pkt_params_gfsk_t* params );

/*!
 * \brief Initialize a data structure that manages bit-by-bit packet generation.
 *
 * \param [out] state Uninitialized transmission state structure.
 *
 * \param [in]  params Transmission packet parameters. Initialized by
 * sx126x_long_pkt_tx_set_gfsk_pkt_params().
 *
 * \param [in]  sync_word Pointer to sync word.
 *
 * \param [in]  payload Pointer to payload.
 */
void sx126x_long_pkt_tx_bits_init( struct sx126x_long_pkt_tx_state*         state,
                                   const sx126x_long_pkt_pkt_params_gfsk_t* params, const uint8_t* sync_word,
                                   const uint8_t* payload );

/*!
 * \brief Fetch a single bit of data to be transmitted.
 *
 * \param [in, out] state Transmission state structure.
 *
 * \param [out]  bit_to_send The bit to be transmitted.
 *
 * \return true if this is not the last bit.
 *
 * \note Transmission is mostly MCU-dependent. Depending on the hardware, it
 * might make sense to optimize this to return bytes of packet data instead of
 * individual bits.
 */
bool sx126x_long_pkt_tx_bits_get( struct sx126x_long_pkt_tx_state* state, bool* bit_to_send );

#ifdef __cplusplus
}
#endif

#endif  // SX126X_LONG_PACKET_H_
