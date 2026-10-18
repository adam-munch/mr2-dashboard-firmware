/*
 * CAN driver for the STM32G0B1's FDCAN, classic CAN only (pins in board.h).
 *
 *   receive:  hardware filters -> RX FIFO 0 (3 frames) -> interrupt ->
 *             can_queue_t (CAN_QUEUE_LEN frames) -> can_receive()
 *   transmit: can_send() -> TX FIFO (3 frames), automatic retransmission
 *
 * Register and message RAM layout: RM0444, "FD controller area network".
 */
#include "can.h"

#include "board.h"
#include "can_queue.h"
#include "gpio.h"
#include "systick.h"

/* The G0's message RAM has a fixed layout per FDCAN instance. */
#define MRAM_FLS_NBR   28u /* standard filters */
#define MRAM_FLE_NBR   8u  /* extended filters */
#define MRAM_RF0_NBR   3u
#define MRAM_RF1_NBR   3u
#define MRAM_TEF_NBR   3u
#define MRAM_TFQ_NBR   3u
#define MRAM_FLS_SIZE  4u
#define MRAM_FLE_SIZE  8u
#define MRAM_RX_SIZE   72u /* element sized for 64-byte CAN FD payloads */
#define MRAM_TEF_SIZE  8u
#define MRAM_TX_SIZE   72u
#define MRAM_FLSSA     0u
#define MRAM_FLESA     (MRAM_FLSSA + MRAM_FLS_NBR * MRAM_FLS_SIZE)
#define MRAM_RF0SA     (MRAM_FLESA + MRAM_FLE_NBR * MRAM_FLE_SIZE)
#define MRAM_RF1SA     (MRAM_RF0SA + MRAM_RF0_NBR * MRAM_RX_SIZE)
#define MRAM_TEFSA     (MRAM_RF1SA + MRAM_RF1_NBR * MRAM_RX_SIZE)
#define MRAM_TFQSA     (MRAM_TEFSA + MRAM_TEF_NBR * MRAM_TEF_SIZE)
#define MRAM_SIZE      (MRAM_TFQSA + MRAM_TFQ_NBR * MRAM_TX_SIZE) /* 0x350 */

/* Message RAM element fields (header words T0/R0 and T1/R1). */
#define ELEM_XTD       (1u << 30)
#define ELEM_RTR       (1u << 29)
#define ELEM_STD_SHIFT 18u
#define ELEM_DLC_SHIFT 16u

/* Filter elements: SFT/EFT, SFEC/EFEC. */
#define SFT_RANGE      (0u << 30)
#define SFEC_FIFO0     (1u << 27)
#define EFEC_FIFO0     (1u << 29)
#define EFT_RANGE      (3u << 30) /* range, XIDAM not applied */

#define RXGFC_REJECT   2u /* ANFS/ANFE: reject frames that match no filter */

/*
 * Bit timing: 16 time quanta per bit, sample point at 81.25 % (sync 1 +
 * TSEG1 12, TSEG2 3), resynchronisation jump width 3. The late-ish sample
 * point and wide SJW give some tolerance for an HSI16 clock; CiA suggests
 * 87.5 %, which a 2-node bus in a car doesn't need.
 */
#define TQ_PER_BIT     16u
#define TSEG1          12u
#define TSEG2          3u
#define SJW            3u

#define INIT_TIMEOUT_MS       10u
#define BUS_OFF_RETRY_MS      100u

static can_queue_t s_rx;
static volatile uint32_t s_rx_frames;
static volatile uint32_t s_rx_lost;
static uint32_t s_tx_frames;
static uint32_t s_tx_dropped;
static uint32_t s_bus_off_count;
static bool s_running;
static bool s_bus_off;
static uint32_t s_bus_off_ms;

static uint32_t mram_base(void)
{
    return SRAMCAN_BASE + (CAN_FDCAN == FDCAN2 ? MRAM_SIZE : 0u);
}

static volatile uint32_t *mram(uint32_t offset)
{
    return (volatile uint32_t *)(mram_base() + offset);
}

static bool wait_cccr(uint32_t mask, uint32_t want)
{
    const uint32_t start = millis();
    while ((CAN_FDCAN->CCCR & mask) != want) {
        if ((uint32_t)(millis() - start) > INIT_TIMEOUT_MS) {
            return false;
        }
    }
    return true;
}

static void pins_init(void)
{
#if BOARD_HAS_CAN_STBY
    gpio_clock_enable(CAN_STBY_PORT);
    gpio_clear(CAN_STBY_PORT, CAN_STBY_PIN); /* low = normal mode */
    gpio_config(CAN_STBY_PORT, CAN_STBY_PIN, PIN_OUTPUT, PIN_PULL_NONE, PIN_SPEED_LOW, 0u);
#endif
    /* Pull-up so RX reads recessive if the transceiver is unpowered. */
    gpio_config(CAN_RX_PORT, CAN_RX_PIN, PIN_AF, PIN_PULL_UP, PIN_SPEED_MEDIUM, CAN_RX_AF);
    gpio_config(CAN_TX_PORT, CAN_TX_PIN, PIN_AF, PIN_PULL_NONE, PIN_SPEED_MEDIUM, CAN_TX_AF);
}

static bool write_filters(const can_filter_t *filters, size_t n)
{
    uint32_t n_std = 0u;
    uint32_t n_ext = 0u;

    for (size_t i = 0; i < n; i++) {
        const can_filter_t *f = &filters[i];
        if (f->first > f->last) {
            return false;
        }
        if (f->ext) {
            if (f->last > CAN_EXT_ID_MAX || n_ext >= MRAM_FLE_NBR) {
                return false;
            }
            volatile uint32_t *e = mram(MRAM_FLESA + n_ext * MRAM_FLE_SIZE);
            e[0] = EFEC_FIFO0 | f->first;
            e[1] = EFT_RANGE | f->last;
            n_ext++;
        } else {
            if (f->last > CAN_STD_ID_MAX || n_std >= MRAM_FLS_NBR) {
                return false;
            }
            *mram(MRAM_FLSSA + n_std * MRAM_FLS_SIZE) = SFT_RANGE | SFEC_FIFO0 | (f->first << 16) | f->last;
            n_std++;
        }
    }

    /* Reject everything that matches no filter, and all remote frames.
     * FIFO 0 in blocking mode: when full, new frames are lost (and counted). */
    CAN_FDCAN->RXGFC = (n_std << FDCAN_RXGFC_LSS_Pos) | (n_ext << FDCAN_RXGFC_LSE_Pos) |
                       (RXGFC_REJECT << FDCAN_RXGFC_ANFS_Pos) | (RXGFC_REJECT << FDCAN_RXGFC_ANFE_Pos) |
                       FDCAN_RXGFC_RRFS | FDCAN_RXGFC_RRFE;
    return true;
}

bool can_init(uint32_t bitrate, const can_filter_t *filters, size_t n_filters)
{
    FDCAN_GlobalTypeDef *const can = CAN_FDCAN;

    s_running = false;
    can_queue_init(&s_rx);

    const uint32_t clk = SystemCoreClock; /* FDCAN kernel clock = PCLK (APB prescaler 1) */
    if (bitrate == 0u || n_filters > CAN_MAX_FILTERS || clk % (bitrate * TQ_PER_BIT) != 0u) {
        return false;
    }
    const uint32_t presc = clk / (bitrate * TQ_PER_BIT);
    if (presc < 1u || presc > 512u) {
        return false;
    }

    /* Reset both FDCANs (CKDIV back to /1) and clock them from PCLK. */
    RCC->APBENR1 |= RCC_APBENR1_FDCANEN;
    RCC->APBRSTR1 |= RCC_APBRSTR1_FDCANRST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_FDCANRST;
    RCC->CCIPR2 &= ~RCC_CCIPR2_FDCANSEL;

    pins_init();

    /* Out of clock-stop, into configuration mode. */
    can->CCCR &= ~FDCAN_CCCR_CSR;
    if (!wait_cccr(FDCAN_CCCR_CSA, 0u)) {
        return false;
    }
    can->CCCR |= FDCAN_CCCR_INIT;
    if (!wait_cccr(FDCAN_CCCR_INIT, FDCAN_CCCR_INIT)) {
        return false;
    }
    can->CCCR |= FDCAN_CCCR_CCE;

    /* Classic CAN, automatic retransmission, normal (not test/monitor) mode. */
    can->CCCR = FDCAN_CCCR_INIT | FDCAN_CCCR_CCE;
    can->NBTP = ((SJW - 1u) << FDCAN_NBTP_NSJW_Pos) | ((presc - 1u) << FDCAN_NBTP_NBRP_Pos) |
                ((TSEG1 - 1u) << FDCAN_NBTP_NTSEG1_Pos) | ((TSEG2 - 1u) << FDCAN_NBTP_NTSEG2_Pos);

    for (uint32_t off = 0u; off < MRAM_SIZE; off += 4u) {
        *mram(off) = 0u;
    }
    if (!write_filters(filters, n_filters)) {
        return false;
    }

    can->TXBC = 0u; /* TX FIFO mode: frames go out in the order they were queued */

    /* RX FIFO 0 "new message" and "message lost" on interrupt line 0. */
    can->IR = 0xFFFFFFFFu;
    can->IE = FDCAN_IE_RF0NE | FDCAN_IE_RF0LE;
    can->ILS = 0u;
    can->ILE = FDCAN_ILE_EINT0;
    NVIC_SetPriority(TIM16_FDCAN_IT0_IRQn, 1u);
    NVIC_EnableIRQ(TIM16_FDCAN_IT0_IRQn);

    /* Leaving INIT also clears CCE. The controller joins the bus after it
     * sees 11 recessive bits. */
    can->CCCR &= ~FDCAN_CCCR_INIT;
    if (!wait_cccr(FDCAN_CCCR_INIT, 0u)) {
        return false;
    }

    s_bus_off = false;
    s_running = true;
    return true;
}

/* Shared with TIM16 (unused here) and FDCAN2's line 0. */
void TIM16_FDCAN_IT0_IRQHandler(void)
{
    FDCAN_GlobalTypeDef *const can = CAN_FDCAN;
    const uint32_t ir = can->IR;
    can->IR = ir & (FDCAN_IR_RF0N | FDCAN_IR_RF0L); /* clear before draining: no missed frames */

    if ((ir & FDCAN_IR_RF0L) != 0u) {
        s_rx_lost++;
    }

    const uint32_t now = millis();
    while ((can->RXF0S & FDCAN_RXF0S_F0FL_Msk) != 0u) {
        const uint32_t gi = (can->RXF0S & FDCAN_RXF0S_F0GI_Msk) >> FDCAN_RXF0S_F0GI_Pos;
        const volatile uint32_t *e = mram(MRAM_RF0SA + gi * MRAM_RX_SIZE);
        const uint32_t r0 = e[0];
        const uint32_t r1 = e[1];
        const uint32_t d0 = e[2];
        const uint32_t d1 = e[3];
        can->RXF0A = gi;

        can_frame_t f;
        f.ext = (r0 & ELEM_XTD) != 0u;
        f.id = f.ext ? (r0 & CAN_EXT_ID_MAX) : ((r0 >> ELEM_STD_SHIFT) & CAN_STD_ID_MAX);
        f.dlc = (uint8_t)((r1 >> ELEM_DLC_SHIFT) & 0xFu);
        if (f.dlc > CAN_MAX_DLC) {
            f.dlc = CAN_MAX_DLC; /* classic CAN: DLC 9..15 still means 8 bytes */
        }
        for (uint32_t i = 0u; i < 4u; i++) {
            f.data[i] = (uint8_t)(d0 >> (8u * i));
            f.data[i + 4u] = (uint8_t)(d1 >> (8u * i));
        }
        f.time_ms = now;

        if (can_queue_push(&s_rx, &f)) {
            s_rx_frames++;
        } else {
            s_rx_lost++;
        }
    }
}

bool can_receive(can_frame_t *frame)
{
    return can_queue_pop(&s_rx, frame);
}

bool can_send(const can_frame_t *frame)
{
    FDCAN_GlobalTypeDef *const can = CAN_FDCAN;
    const uint32_t id_max = frame->ext ? CAN_EXT_ID_MAX : CAN_STD_ID_MAX;

    if (!s_running || s_bus_off || frame->dlc > CAN_MAX_DLC || frame->id > id_max ||
        (can->PSR & FDCAN_PSR_BO) != 0u || (can->TXFQS & FDCAN_TXFQS_TFQF) != 0u) {
        s_tx_dropped++;
        return false;
    }

    const uint32_t put = (can->TXFQS & FDCAN_TXFQS_TFQPI_Msk) >> FDCAN_TXFQS_TFQPI_Pos;
    volatile uint32_t *e = mram(MRAM_TFQSA + put * MRAM_TX_SIZE);
    uint32_t d0 = 0u;
    uint32_t d1 = 0u;
    for (uint32_t i = 0u; i < frame->dlc; i++) {
        if (i < 4u) {
            d0 |= (uint32_t)frame->data[i] << (8u * i);
        } else {
            d1 |= (uint32_t)frame->data[i] << (8u * (i - 4u));
        }
    }
    e[0] = frame->ext ? (ELEM_XTD | frame->id) : (frame->id << ELEM_STD_SHIFT);
    e[1] = (uint32_t)frame->dlc << ELEM_DLC_SHIFT;
    e[2] = d0;
    e[3] = d1;
    can->TXBAR = 1u << put;

    s_tx_frames++;
    return true;
}

void can_poll(uint32_t now_ms)
{
    if (!s_running) {
        return;
    }

    /*
     * Bus-off: the controller stops and sets INIT. Clearing INIT starts the
     * standard recovery (it rejoins after 128 x 11 recessive bits). Wait a
     * little between attempts so a broken bus isn't hammered.
     */
    if ((CAN_FDCAN->PSR & FDCAN_PSR_BO) != 0u) {
        if (!s_bus_off) {
            s_bus_off = true;
            s_bus_off_ms = now_ms;
            s_bus_off_count++;
        } else if ((uint32_t)(now_ms - s_bus_off_ms) >= BUS_OFF_RETRY_MS) {
            s_bus_off_ms = now_ms;
            CAN_FDCAN->CCCR &= ~FDCAN_CCCR_INIT;
        }
    } else {
        s_bus_off = false;
    }
}

void can_get_stats(can_stats_t *stats)
{
    const uint32_t ecr = CAN_FDCAN->ECR;
    const uint32_t psr = CAN_FDCAN->PSR;

    if (!s_running) {
        stats->state = CAN_STATE_STOPPED;
    } else if ((psr & FDCAN_PSR_BO) != 0u) {
        stats->state = CAN_STATE_BUS_OFF;
    } else if ((psr & FDCAN_PSR_EP) != 0u) {
        stats->state = CAN_STATE_PASSIVE;
    } else if ((psr & FDCAN_PSR_EW) != 0u) {
        stats->state = CAN_STATE_WARNING;
    } else {
        stats->state = CAN_STATE_ACTIVE;
    }
    stats->tx_errors = (uint8_t)((ecr & FDCAN_ECR_TEC_Msk) >> FDCAN_ECR_TEC_Pos);
    stats->rx_errors = (uint8_t)((ecr & FDCAN_ECR_REC_Msk) >> FDCAN_ECR_REC_Pos);
    stats->rx_frames = s_rx_frames;
    stats->rx_lost = s_rx_lost;
    stats->tx_frames = s_tx_frames;
    stats->tx_dropped = s_tx_dropped;
    stats->bus_off_count = s_bus_off_count;
}
