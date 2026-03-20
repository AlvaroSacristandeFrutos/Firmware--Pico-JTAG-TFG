/*
 * jtag_tap_track.c — Simulación de la FSM TAP IEEE 1149.1 en la DLL.
 *
 * Tabla de transición next_state[estado_actual][tms_bit]:
 *   next = next_state[current][TMS]
 *
 * Fuente: IEEE 1149.1-2013, Figura 6-3 (TAP Controller State Diagram).
 * Los índices corresponden a tap_state_t (0–15).
 */

#include "jtag_tap_track.h"

/*
 * Tabla de transición completa.
 * next_state[s][0] = estado siguiente cuando TMS=0
 * next_state[s][1] = estado siguiente cuando TMS=1
 */
static const tap_state_t next_state[16][2] = {
    /* TAP_RESET      */ { TAP_IDLE,       TAP_RESET      },
    /* TAP_IDLE       */ { TAP_IDLE,       TAP_SELECT_DR  },
    /* TAP_SELECT_DR  */ { TAP_CAPTURE_DR, TAP_SELECT_IR  },
    /* TAP_CAPTURE_DR */ { TAP_SHIFT_DR,   TAP_EXIT1_DR   },
    /* TAP_SHIFT_DR   */ { TAP_SHIFT_DR,   TAP_EXIT1_DR   },
    /* TAP_EXIT1_DR   */ { TAP_PAUSE_DR,   TAP_UPDATE_DR  },
    /* TAP_PAUSE_DR   */ { TAP_PAUSE_DR,   TAP_EXIT2_DR   },
    /* TAP_EXIT2_DR   */ { TAP_SHIFT_DR,   TAP_UPDATE_DR  },
    /* TAP_UPDATE_DR  */ { TAP_IDLE,       TAP_SELECT_DR  },
    /* TAP_SELECT_IR  */ { TAP_CAPTURE_IR, TAP_RESET      },
    /* TAP_CAPTURE_IR */ { TAP_SHIFT_IR,   TAP_EXIT1_IR   },
    /* TAP_SHIFT_IR   */ { TAP_SHIFT_IR,   TAP_EXIT1_IR   },
    /* TAP_EXIT1_IR   */ { TAP_PAUSE_IR,   TAP_UPDATE_IR  },
    /* TAP_PAUSE_IR   */ { TAP_PAUSE_IR,   TAP_EXIT2_IR   },
    /* TAP_EXIT2_IR   */ { TAP_SHIFT_IR,   TAP_UPDATE_IR  },
    /* TAP_UPDATE_IR  */ { TAP_IDLE,       TAP_SELECT_DR  },
};

static tap_state_t s_state = TAP_RESET;

void tap_track_reset(void) {
    s_state = TAP_RESET;
}

void tap_track_tms(const uint8_t *pTMS, uint32_t numBits) {
    for (uint32_t i = 0u; i < numBits; i++) {
        uint8_t tms_bit = (pTMS[i >> 3u] >> (i & 7u)) & 1u;
        s_state = next_state[s_state][tms_bit];
    }
}

tap_state_t tap_track_state(void) {
    return s_state;
}
