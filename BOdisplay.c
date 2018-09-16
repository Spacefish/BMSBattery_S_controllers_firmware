/*
 * Copyright (c) 2018 Björn Schmidt
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

/* 
 * File:   BOdisplay.h
 * Author: Björn Schmidt
 *
 * Created on August 30, 2018, 8:03 PM
 */


#include <stdio.h>
#include "config.h"
#include "stm8s.h"
#include "stm8s_itc.h"
#include "BOdisplay.h"
#include "interrupts.h"
#include "ACAeeprom.h"
#include "brake.h" // ugly crossrefernce for brake_is_set(), FIXME
#include "ACAcontrollerState.h"
#include "ACAcommons.h"

#ifdef BLUOSEC

// example
//:304100305F\r\n 
//  0 A 0 (as chars)
uint8_t ui8_rx_buffer[13]; // modbus ascii with max 4 bytes payload (array including padding)
uint8_t ui8_tx_buffer[45]; // (max 20*8bit key + 20*8bit data points + bounced checksum(+ key) + address + function + checksum) (array excluding padding)
uint8_t ui8_rx_converted_buffer[5]; // for decoded ascii values

uint8_t ui8_rx_buffer_counter = 0;
uint8_t ui8_tx_buffer_counter = 0;

uint8_t int2hex(uint8_t in) {
    if (in <= 9)
        return in + 0x30;
    return 0x41 - 10 + in;
}

uint8_t hex2int(uint8_t ch) {
    if (ch >= 0x30 && ch <= 0x39)
        return ch - 0x30;
    if (ch >= 0x41 && ch <= 0x46)
        return ch - 0x41 + 10;
    if (ch >= 0x61 && ch <= 0x66)
        return ch - 0x61 + 10;
    return 0;
}

uint8_t calcLRC(uint8_t ints[], uint8_t start, uint8_t end) {
    uint8_t i;
    uint8_t LRC = 0;
    for (i = start; i < end; i++) {
        LRC = LRC + ints[i];
        //printf("LI %u\r\n",LRC);
    }
    //printf("LIX %u\r\n",LRC);
    return (~LRC) + 1;
}

void sendPreparedPackage(void) {
    uint8_t j;
    putchar(0x3A);
    for (j = 0; j < ui8_tx_buffer_counter; j++) {
        putchar(int2hex(ui8_tx_buffer[j] >> 4));
        putchar(int2hex(ui8_tx_buffer[j]&15));
    }
    putchar(0x0D);
    putchar(0x0A);
}

void addPayload(uint8_t id, uint8_t value) {
    ui8_tx_buffer[ui8_tx_buffer_counter++] = id;
    ui8_tx_buffer[ui8_tx_buffer_counter++] = value;
}

void prepareBasePackage(uint8_t address, uint8_t function) {
    ui8_tx_buffer_counter = 0;
    ui8_tx_buffer[ui8_tx_buffer_counter++] = address;
    ui8_tx_buffer[ui8_tx_buffer_counter++] = function;

}

void signPackage(void) {
    ui8_tx_buffer[ui8_tx_buffer_counter] = calcLRC(ui8_tx_buffer, 0, ui8_tx_buffer_counter);
    ui8_tx_buffer_counter++;
}

void UART2_IRQHandler(void) __interrupt(UART2_IRQHANDLER) {

    if (UART2_GetFlagStatus(UART2_FLAG_RXNE) == SET) {
        if (ui8_rx_buffer_counter < 13) {
            ui8_rx_buffer[ui8_rx_buffer_counter++] = UART2_ReceiveData8();
        } else {
            UART2_ReceiveData8();
        }
    } else //catch errors
    {
        if (UART2_GetITStatus(UART2_IT_IDLE) == SET) {
            UART2_ReceiveData8(); // -> clear!
        }
        if (UART2_GetITStatus(UART2_IT_LBDF) == SET) {
            UART2_ReceiveData8(); // -> clear!
        }
        if (UART2_GetITStatus(UART2_IT_OR) == SET) {
            UART2_ReceiveData8(); // -> clear!
        }
        if (UART2_GetITStatus(UART2_IT_PE) == SET) {
            UART2_ReceiveData8(); // -> clear!
        }

    } //end else
}

void addConfigStateInfos(void) {

    // float casts might be costly but they are only requested once every 10 seconds
    addPayload(CODE_ERPS_FACTOR, (uint16_t) (((float) wheel_circumference) / ((float) GEAR_RATIO)));
    addPayload(CODE_CURRENT_CAL_A, current_cal_a);
    addPayload(CODE_CURRENT_CAL_B_HIGH_BYTE, ui16_current_cal_b >> 8);
    addPayload(CODE_CURRENT_CAL_B, ui16_current_cal_b);
    addPayload(CODE_EEPROM_MAGIC_BYTE, eeprom_magic_byte);
    addPayload(CODE_MAX_SPEED_DEFAULT, ui8_speedlimit_kph);
    addPayload(CODE_MAX_SPEED_WITHOUT_PAS, ui8_speedlimit_without_pas_kph);
    addPayload(CODE_MAX_SPEED_WITH_THROTTLE_OVERRIDE, ui8_speedlimit_with_throttle_override_kph);
    addPayload(CODE_ACA_FLAGS, ui8_aca_flags);
    addPayload(CODE_ASSIST_LEVEL, ui8_assistlevel_global);
    addPayload(CODE_THROTTLE_MIN_RANGE, ui8_throttle_min_range);
    addPayload(CODE_THROTTLE_MAX_RANGE, ui8_throttle_max_range);
    addPayload(CODE_MOTOR_SPECIFIC_ANGLE, ui8_s_motor_angle);
    addPayload(CODE_PAS_TRESHOLD, float2int(flt_s_pas_threshold, 4.0));
    addPayload(CODE_PID_GAIN_P, float2int(flt_s_pid_gain_p, 2.0));
    addPayload(CODE_PID_GAIN_I, float2int(flt_s_pid_gain_i, 2.0));
    addPayload(CODE_RAMP_END, ui16_s_ramp_end >> 4);

    // 3 more elements left/avail (max20)

}

void addHallStateInfos(void) {
    addPayload(CODE_CURRENT_AT_HALL_POSITION_BASE + 0x00, uint8_t_hall_case[0]);
    addPayload(CODE_CURRENT_AT_HALL_POSITION_BASE + 0x01, uint8_t_hall_case[1]);
    addPayload(CODE_CURRENT_AT_HALL_POSITION_BASE + 0x02, uint8_t_hall_case[2]);
    addPayload(CODE_CURRENT_AT_HALL_POSITION_BASE + 0x03, uint8_t_hall_case[3]);
    addPayload(CODE_CURRENT_AT_HALL_POSITION_BASE + 0x04, uint8_t_hall_case[4]);
    addPayload(CODE_CURRENT_AT_HALL_POSITION_BASE + 0x05, uint8_t_hall_case[5]);
    addPayload(CODE_HALL_ORDER_BASE + 0x00, uint8_t_hall_order[0]);
    addPayload(CODE_HALL_ORDER_BASE + 0x01, uint8_t_hall_order[1]);
    addPayload(CODE_HALL_ORDER_BASE + 0x02, uint8_t_hall_order[2]);
    addPayload(CODE_HALL_ORDER_BASE + 0x03, uint8_t_hall_order[3]);
    addPayload(CODE_HALL_ORDER_BASE + 0x04, uint8_t_hall_order[4]);
    addPayload(CODE_HALL_ORDER_BASE + 0x05, uint8_t_hall_order[5]);

    // 8 more elements left/avail (max20)
}

void addDetailStateInfos(void) {
    addPayload(CODE_OFFROAD, ui8_offroad_state);
    addPayload(CODE_PAS_ACTIVE, PAS_act);
    addPayload(CODE_PAS_DIR, PAS_dir);
    addPayload(CODE_CORRECTION_VALUE, ui8_position_correction_value);
    addPayload(CODE_PHASE_CURRENT, ui16_ADC_iq_current >> 2);
    addPayload(CODE_THROTTLE_HIGH_BYTE, ui16_throttle_accumulated >> 8);
    addPayload(CODE_THROTTLE, ui16_throttle_accumulated);
    addPayload(CODE_CURRENT_TARGET_HIGH_BYTE, uint32_current_target >> 8);
    addPayload(CODE_CURRENT_TARGET, uint32_current_target);
    addPayload(CODE_CURRENT_RAMP_HIGH_BYTE, ui16_time_ticks_between_pas_interrupt_smoothed >> 8);
    addPayload(CODE_CURRENT_RAMP, ui16_time_ticks_between_pas_interrupt_smoothed);
    addPayload(CODE_PAS_HIGH_COUNTER_HIGH_BYTE, ui16_PAS_High >> 8);
    addPayload(CODE_PAS_HIGH_COUNTER, ui16_PAS_High);
    addPayload(CODE_PAS_COUNTER_HIGH_BYTE, ui16_time_ticks_between_pas_interrupt >> 8);
    addPayload(CODE_PAS_COUNTER, ui16_time_ticks_between_pas_interrupt);
    addPayload(CODE_VER_SPEED_HIGH_BYTE, ui16_virtual_erps_speed>>8);
    addPayload(CODE_VER_SPEED, ui16_virtual_erps_speed);

    // 3 more elements left/avail (max20)
}

void addBasicStateInfos(void) {
    addPayload(CODE_ACTUAL_MAX_SPEED, ui8_speedlimit_actual_kph);
    addPayload(CODE_ASSIST_LEVEL, ui8_assistlevel_global);
    addPayload(CODE_BRAKE_STATUS, (int) brake_is_set());
    addPayload(CODE_MOTOR_STATE, ui8_motor_state);
    addPayload(CODE_BATTERY_VOLTAGE, ui8_BatteryVoltage);
    addPayload(CODE_ER_SPEED_HIGH_BYTE, ui16_motor_speed_erps>>8);
    addPayload(CODE_ER_SPEED, ui16_motor_speed_erps);
    addPayload(CODE_SPEED_HIGH_BYTE, ui32_SPEED_km_h >> 8);
    addPayload(CODE_SPEED, ui32_SPEED_km_h);
    addPayload(CODE_BATTERY_CURRENT_HIGH_BYTE, ui16_BatteryCurrent >> 8);
    addPayload(CODE_BATTERY_CURRENT, ui16_BatteryCurrent);
    addPayload(CODE_SUM_TORQUE, ui16_sum_torque);
    addPayload(CODE_SETPOINT, ui16_setpoint);
    addPayload(CODE_SETPOINT_STATE, ui8_control_state);
    addPayload(CODE_UPTIME, ui8_uptime);

    // 5 more elements left/avail (max20)
}

void gatherDynamicPayload(uint8_t function) {
    switch (function) {
        case FUN_BASIC_INFOS:
            addBasicStateInfos();
            break;
        case FUN_DETAIL_INFOS:
            addDetailStateInfos();
            break;
        case FUN_HALL_INFOS:
            addHallStateInfos();
            break;
        default:
            addPayload(CODE_ERROR, CODE_ERROR);
    }
}

void gatherStaticPayload(uint8_t function) {
    switch (function) {
        case FUN_CONFIG_INFOS:addConfigStateInfos();
            break;
        default:
            addPayload(CODE_ERROR, CODE_ERROR);
    }
}

uint8_t digestConfigRequest(uint8_t configAddress, uint8_t requestedCode, uint8_t requestedValue) {
    switch (requestedCode) {
        case CODE_OFFROAD:
            ui8_offroad_state = requestedValue;
            return ui8_offroad_state;
            break;
        case CODE_ASSIST_LEVEL:
            ui8_assistlevel_global = requestedValue;
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_ASSIST_LEVEL, requestedValue);
            }
            return ui8_assistlevel_global;
            break;
        case CODE_ACA_FLAGS:
            ui8_aca_flags = requestedValue;
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_ACA_FLAGS, requestedValue);
            }
            return ui8_aca_flags;
            break;
        case CODE_THROTTLE_MIN_RANGE:
            ui8_throttle_min_range = requestedValue;
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_THROTTLE_MIN_RANGE, requestedValue);
            }
            return ui8_throttle_min_range;
            break;
        case CODE_THROTTLE_MAX_RANGE:
            ui8_throttle_max_range = requestedValue;
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_THROTTLE_MAX_RANGE, requestedValue);
            }
            return ui8_throttle_max_range;
            break;

        case CODE_MOTOR_SPECIFIC_ANGLE:
            ui8_s_motor_angle = requestedValue;
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_MOTOR_ANGLE, requestedValue);
            }
            return ui8_s_motor_angle;
            break;
        
        case CODE_PAS_TRESHOLD:
            flt_s_pas_threshold = int2float(requestedValue, 4.0);
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_PAS_TRESHOLD, requestedValue);
            }
            return float2int(flt_s_pas_threshold, 4.0);
            break;
        case CODE_PID_GAIN_P:
            flt_s_pid_gain_p = int2float(requestedValue, 2.0);
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_PID_GAIN_P, requestedValue);
            }
            return float2int(flt_s_pid_gain_p, 2.0);
            break;
        case CODE_PID_GAIN_I:
            flt_s_pid_gain_i = int2float(requestedValue, 2.0);
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_PID_GAIN_I, requestedValue);
            }
            return float2int(flt_s_pid_gain_i, 2.0);
            break;
        case CODE_RAMP_END:
            ui16_s_ramp_end = requestedValue << 4;
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_RAMP_END, requestedValue);
            }
            return ui16_s_ramp_end >> 4;
            break;
        case CODE_MAX_SPEED_DEFAULT:
            ui8_speedlimit_kph = requestedValue;
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_MAX_SPEED_DEFAULT, requestedValue);
            }
            setSignal(SIGNAL_SPEEDLIMIT_CHANGED);
            return ui8_speedlimit_kph;
            break;
        case CODE_MAX_SPEED_WITHOUT_PAS:
            ui8_speedlimit_without_pas_kph = requestedValue;
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_MAX_SPEED_WITHOUT_PAS, requestedValue);
            }
            setSignal(SIGNAL_SPEEDLIMIT_CHANGED);
            return ui8_speedlimit_without_pas_kph;
            break;
        case CODE_MAX_SPEED_WITH_THROTTLE_OVERRIDE:
            ui8_speedlimit_with_throttle_override_kph = requestedValue;
            if (configAddress == EEPROM_ADDRESS) {
                eeprom_write(OFFSET_MAX_SPEED_WITH_THROTTLE_OVERRIDE, requestedValue);
            }
            setSignal(SIGNAL_SPEEDLIMIT_CHANGED);
            return ui8_speedlimit_with_throttle_override_kph;
            break;
        

        default:
            addPayload(CODE_ERROR, CODE_ERROR);
            return 0;
    }

}

void processBoMessage() {
    if (ui8_rx_buffer_counter == 13) {

        uint8_t calculatedLrc;

        ui8_rx_converted_buffer[0] = (hex2int(ui8_rx_buffer[1]) << 4) + hex2int(ui8_rx_buffer[2]);
        ui8_rx_converted_buffer[1] = (hex2int(ui8_rx_buffer[3]) << 4) + hex2int(ui8_rx_buffer[4]);
        ui8_rx_converted_buffer[2] = (hex2int(ui8_rx_buffer[5]) << 4) + hex2int(ui8_rx_buffer[6]);
        ui8_rx_converted_buffer[3] = (hex2int(ui8_rx_buffer[7]) << 4) + hex2int(ui8_rx_buffer[8]);
        ui8_rx_converted_buffer[4] = (hex2int(ui8_rx_buffer[9]) << 4) + hex2int(ui8_rx_buffer[10]);
        calculatedLrc = calcLRC(ui8_rx_converted_buffer, 0, 4);


        if (calculatedLrc == ui8_rx_converted_buffer[4]) {
            uint8_t requestedFunction = ui8_rx_converted_buffer[1];
            uint8_t requestedCode = ui8_rx_converted_buffer[2];
            uint8_t requestedValue = ui8_rx_converted_buffer[3];

            if (ui8_rx_converted_buffer[0] == DYNAMIC_DATA_ADDRESS) {
                prepareBasePackage(DYNAMIC_DATA_ADDRESS, requestedFunction);
                gatherDynamicPayload(requestedFunction);
                addPayload(CODE_LRC_CHECK, calculatedLrc);
                signPackage();
                sendPreparedPackage();
            } else if (ui8_rx_converted_buffer[0] == STATIC_DATA_ADDRESS) {
                prepareBasePackage(STATIC_DATA_ADDRESS, requestedFunction);
                gatherStaticPayload(requestedFunction);
                addPayload(CODE_LRC_CHECK, calculatedLrc);
                signPackage();
                sendPreparedPackage();
            } else if ((ui8_rx_converted_buffer[0] == CONFIG_ADDRESS) || (ui8_rx_converted_buffer[0] == EEPROM_ADDRESS)) {
                prepareBasePackage(ui8_rx_converted_buffer[0], requestedFunction);
                addPayload(requestedCode, digestConfigRequest(ui8_rx_converted_buffer[0], requestedCode, requestedValue));
                addPayload(CODE_LRC_CHECK, calculatedLrc);
                signPackage();
                sendPreparedPackage();
            }
        } else {
            // let sender know what was received and what the correct lrc would have been
            prepareBasePackage(ERROR_ADDRESS, ui8_rx_converted_buffer[1]);
            addPayload(CODE_ERROR, ui8_rx_converted_buffer[0]);
            addPayload(CODE_ERROR + 1, ui8_rx_converted_buffer[1]);
            addPayload(CODE_ERROR + 2, ui8_rx_converted_buffer[2]);
            addPayload(CODE_ERROR + 3, ui8_rx_converted_buffer[3]);
            addPayload(CODE_ERROR + 4, ui8_rx_converted_buffer[4]);
            addPayload(CODE_LRC_CHECK, calculatedLrc);
            signPackage();
            sendPreparedPackage();
        }

        // allow fetching of new data
        ui8_rx_buffer_counter = 0;
    }
}


#endif // BLUOSEC