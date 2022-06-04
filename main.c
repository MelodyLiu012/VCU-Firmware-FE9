/**
  Generated main.c file from MPLAB Code Configurator

  @Company
    Microchip Technology Inc.

  @File Name
    main.c

  @Summary
    This is the generated main.c using PIC24 / dsPIC33 / PIC32MM MCUs.

  @Description
    This source file provides main entry point for system initialization and application code development.
    Generation Information :
        Product Revision  :  PIC24 / dsPIC33 / PIC32MM MCUs - 1.170.0
        Device            :  dsPIC33CH256MP505
    The generated drivers are tested against the following:
        Compiler          :  XC16 v1.61
        MPLAB 	          :  MPLAB X v5.45
*/

/*
    (c) 2020 Microchip Technology Inc. and its subsidiaries. You may use this
    software and any derivatives exclusively with Microchip products.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
    WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
    PARTICULAR PURPOSE, OR ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION
    WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
    BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
    FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
    ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
    THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.

    MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE
    TERMS.
*/


/* Debug only */

//#ifndef _XTAL_FREQ
//#define _XTAL_FREQ  8000000UL
//#endif
//
//#ifndef FCY
//#define FCY 8000000UL
//#endif
//
//#include "mcc_generated_files/mcc.h"
//#include "mcc_generated_files/system.h"
//#include <libpic30.h>

//int main(void)
//{
//    CAN_MSG_OBJ msg_RX;
//    
//    CAN_MSG_OBJ msg_TX;
//    msg_TX.msgId = 0x02;
//    uint8_t data_TX[1] = {0x03};
//    msg_TX.data = data_TX;
//    
//    // initialize the device
//    SYSTEM_Initialize();
//    while (1)
//    {
////        if(CAN1_Receive(&msg_RX))
////        {
////            CAN1_Transmit(CAN1_TX_TXQ, &msg_TX);
////        }
//        CAN1_Transmit(CAN1_TX_TXQ, &msg_TX);
//    }
//    
//    return 1;
//}

/*********/

/**
  Section: Included Files
*/

#ifndef _XTAL_FREQ
#define _XTAL_FREQ  8000000UL
#endif

#ifndef FCY
#define FCY 8000000UL
#endif

#include "mcc_generated_files/mcc.h"
#include "mcc_generated_files/system.h"
#include <libpic30.h>

#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>


/************ States ************/ 

typedef enum {
    LV,
    PRECHARGING,
    HV_ENABLED,
    DRIVE,
    FAULT
} state_t;

typedef enum {
    NONE,
    DRIVE_REQUEST_FROM_LV,
    CONSERVATIVE_TIMER_MAXED,
    BRAKE_NOT_PRESSED,
    HV_DISABLED_WHILE_DRIVING,
    SENSOR_DISCREPANCY,
    BRAKE_IMPLAUSIBLE,
    ESTOP
} error_t;

// Initial FSM state
state_t state = LV;
error_t error = NONE;

const char* STATE_NAMES[] = {
    "LV", 
    "PRECHARGING", 
    "HV_ENABLED", 
    "DRIVE", 
    "FAULT"
};
const char* ERROR_NAMES[] = {
    "NONE", 
    "DRIVE_REQUEST_FROM_LV", 
    "CONSERVATIVE_TIMER_MAXED", 
    "BRAKE_NOT_PRESSED", 
    "HV_DISABLED_WHILE_DRIVE",
    "SENSOR_DISCREPANCY",
    "BRAKE_IMPLAUSIBLE"
};

void change_state(const state_t new_state) {
    // Handle edge cases
    if (state == FAULT && new_state != FAULT) {
        // Reset the error cause when exiting fault state
        error = NONE;
    }
        
    // Print state transition
    printf("%s -> %s\r\n", STATE_NAMES[state], STATE_NAMES[new_state]);
    
    state = new_state;
}

void report_fault(error_t _error) {
    change_state(FAULT);
    
    printf("Error: %s\r\n", ERROR_NAMES[_error]);
    
    // Cause of error
    error = _error;
}

// How long to wait for pre-charging to finish before timing out
#define MAX_CONSERVATION_SECS 5
// Keeps track of timer waiting for pre-charging
unsigned int conservative_timer_ms = 0;
// Delay between checking pre-charging state
#define AWAIT_PRECHARGING_DELAY_MS 10

// High voltage state variables
#define DRIVE_REQ_DELAY_MS 1000


/************ Switches ************/

/*
 * global variable for driver switch status
 * 
 * format: 0000 00[Hv][Dr]
 * bit is high if corresponding switch is on, low otherwise
 * 
 * use in conditions:
 *  - Hv = switches & 0b10
 *  - Dr = switches & 0b1
 */
volatile uint8_t switches = 0;

uint8_t is_hv_requested() {
    return switches & 0b10;//IO_RB2_GetValue();
}

uint8_t is_drive_requested() {
    return switches & 0b1;//IO_RB7_GetValue();
}


/************ Pedals ************/

// APPS

volatile double THROTTLE_MULTIPLIER;
volatile uint8_t PACK_TEMP;

const double THROTTLE_MAP[8] = { 95, 71, 59, 47, 35, 23, 11, 5 };

void temp_attenuate() {
    int t = PACK_TEMP - 50;
    if (t < 0) {
        THROTTLE_MULTIPLIER = 1;   
    } else if (t < 8) {
        THROTTLE_MULTIPLIER = THROTTLE_MAP[t] / 100.0; 
    } else if (t >= 8) {
        THROTTLE_MULTIPLIER = THROTTLE_MAP[7] / 100.0; 
    }
}

uint16_t throttle1 = 0;
uint16_t throttle2 = 0;
uint16_t throttle1_max = 0;
uint16_t throttle1_min = 0x7FFF;
uint16_t throttle2_max = 0;
uint16_t throttle2_min = 0x7FFF;
uint16_t throttle_range = 0; // set after max and min values are calibrated
uint16_t per_throttle1 = 0;
uint16_t per_throttle2 = 0;


// Brake

// There is some noise when reading from the brake pedal
// So give some room for error when driver presses on brake
#define BRAKE_ERROR_TOLERANCE 50

uint16_t brake = 0;
uint16_t brake_max = 0;
uint16_t brake_min = 0;
uint16_t brake_range = 0;

// discrepancy timer
#define MAX_DISCREPANCY_MS 100 
unsigned int discrepancy_timer_ms = 0;
#define AWAIT_DISCREPANCY_DELAY_MS 10


// check differential between the throttle sensors
// returns true only if the sensor discrepancy is > 10%
// Note: after verifying there's no discrepancy, can use either sensor(1 or 2) for remaining checks
bool has_discrepancy() {
    // check throttle
    
    // calculate percentage of throttle 1
    uint16_t temp_throttle1 = throttle1;
    if (temp_throttle1 > throttle1_max) {
        temp_throttle1 = throttle1_max;
    } else if (temp_throttle1 < throttle1_min) {
        temp_throttle1 = throttle1_min;
    }
    per_throttle1 = (uint16_t)(((double)(temp_throttle1-throttle1_min) / (throttle1_max - throttle1_min)) * 100);
    
    // calculate percentage of throttle 2
    uint16_t temp_throttle2 = throttle2;
    if (temp_throttle2 > throttle2_max) {
        temp_throttle2 = throttle2_max;
    } else if( temp_throttle2 < throttle2_min) {
        temp_throttle2 = throttle2_min;
    }
    per_throttle2 = (uint16_t)(((double)(temp_throttle2-throttle2_min) / (throttle2_max - throttle2_min)) * 100);
    
    return abs((int)per_throttle1 - (int)per_throttle2) > 10;
}

// check for soft BSPD
// see EV.5.7 of FSAE 2022 rulebook
bool brake_implausible() {
    uint16_t temp_throttle = throttle1; 
    
    // subtract dead zone 15%
    uint16_t temp_brake = brake - ((throttle_range)/6);
    if (temp_brake > brake_max){
        temp_brake = brake_max;
    }
    if (temp_brake < brake_min){
        temp_brake = brake_min;
    }
    temp_brake = (uint16_t)((temp_brake-brake_min)*100.0 / throttle_range);
    
    
    if (state == FAULT && error == BRAKE_IMPLAUSIBLE) {
        // once brake implausibility detected, can only revert to normal if throttle unapplied
        return !(temp_throttle < throttle_range * 0.05);
    }
    else {
        // if both brake and throttle applied, brake implausible
        return (temp_brake > 0 && temp_throttle > throttle_range * 0.25);
    }
    
}

uint16_t getConversion(ADC1_CHANNEL channel){
    uint16_t conversion;
    ADC1_Enable();
    ADC1_ChannelSelect(channel);
    ADC1_SoftwareTriggerEnable();
    //Provide Delay
    __delay_ms(1);
    ADC1_SoftwareTriggerDisable();
    while(!ADC1_IsConversionComplete(channel));
    conversion = ADC1_ConversionResultGet(channel);
    ADC1_Disable();
    return conversion;
}


// Update sensors

// On the breadboard, the range of values for the potentiometer is 0 to 4095
#define PEDAL_MAX 4095

bool init_calibration = true;
void run_calibration() {
    if (init_calibration) {
        // set up values at start of calibration
        throttle1_max = 0;
        throttle1_min = 0x7FFF;
        throttle2_max = 0;
        throttle2_min = 0x7FFF;
        brake_max = 0;
        brake_min = 0x7FFF;
        init_calibration = false;
    }
    else {
        // APPS1 = pin8 = RA0
        throttle1 = getConversion(channel_AN0);
        // APPS2 = pin9 = RA1
        throttle2 = getConversion(channel_AN1);
        // BSE1 = pin11 = RA3
        // BSE2 = pin12 = RA4
        brake = getConversion(channel_AN3);

        if (throttle1 > throttle1_max) {
            throttle1_max = throttle1;
        }
        if (throttle1 < throttle1_min) {
            throttle1_min = throttle1;
        }
        if (throttle2 > throttle2_max) {
            throttle2_max = throttle2;
        }
        if (throttle2 < throttle2_min) {
            throttle2_min = throttle2;    
        }

        if (brake > brake_max) {
            brake_max = brake;
        }
        if (brake < brake_min) {
            brake_min = brake;
        }    

         printf("throttle1: %d\r\n", throttle1);
         printf("throttle1_max: %d\r\n", throttle1_max);
         printf("throttle1_min: %d\r\n", throttle1_min); 
         printf("throttle2: %d\r\n", throttle2);         
         printf("throttle2_max: %d\r\n", throttle2_max);
         printf("throttle2_min: %d\r\n", throttle2_min);
         printf("brake: %d\r\n", brake);
         printf("brake_max: %d\r\n", brake_max);
         printf("brake_min: %d\r\n", brake_min);
    }
}

// storage variables used to return to previous state when discrepancy is resolved
state_t temp_state = LV; // state before sensor discrepancy error
error_t temp_error = NONE; // error state before sensor discrepancy error (only used when going from one fault to discrepancy fault)

void update_sensor_vals() {
//     APPS1 = pin8 = RA0
    throttle1 = getConversion(channel_AN0);
//     APPS2 = pin9 = RA1
    throttle2 = getConversion(channel_AN1);
//     BSE1 = pin11 = RA3
//     BSE2 = pin12 = RA4 
    brake = getConversion(channel_AN3);
    
     printf("State: %s\r\n", STATE_NAMES[state]);
     printf("Throttle 1: %d\r\n", throttle1);
     printf("Throttle 2: %d\r\n", throttle2);
     printf("Brake: %d\r\n", brake);

   if (error != SENSOR_DISCREPANCY && has_discrepancy() ) {
       temp_state = state;
       temp_error = error;
       report_fault(SENSOR_DISCREPANCY);
   }
}

/************ Capacitor ************/

uint16_t capacitor_volt = 0;
#define PRECHARGE_THRESHOLD 5806 // 90% of accumulator voltage, scaled from range of 0-12800(0V-200V)

/************ CAN ************/

typedef enum {
    VEHICLE_STATE = 0x0c0,
    TORQUE_REQUEST_COMMAND = 0x766,
    BMS_TEMPERATURES = 0x389,
    SWITCHES = 0x0d0,
    MOTOR_CONTROLLER_ESTOP = 0x366,
    MOTOR_CONTROLLER_PDOSEND = 0x566,
    BRAKE_COMMAND = 0x767
} CAN_ID;

CAN_MSG_OBJ msg_RX;

void can_receive() {
    // Debug only
//    if(CAN1_ReceivedMessageCountGet() > 0) {
//        switches = 101;
//    }
//    if (CAN1_Receive(&msg_RX)) {
//        switches = 102;
//    }
    
    // gets message and updates values
    if (CAN1_Receive(&msg_RX)) {
        INDICATOR_1_Toggle();
        switch (msg_RX.msgId) {
            case SWITCHES:
                switches = msg_RX.data[0]; 
                break;
            case BMS_TEMPERATURES:
                PACK_TEMP = msg_RX.data[7];
                temp_attenuate();
                break;
            case MOTOR_CONTROLLER_ESTOP:
                INDICATOR_2_Toggle();
                if (msg_RX.data[0]) { // if estop detected in any state
                    report_fault(ESTOP);
                }
            case MOTOR_CONTROLLER_PDOSEND:
                capacitor_volt = (msg_RX.data[0] << 8); // upper bits
                capacitor_volt += msg_RX.data[1]; // lower bits
            default:
                // no valid input received
                break;
        }
    } 
}

void CAN_transmit_torque_request() {
    CAN_MSG_OBJ msg_TX_torque;

    CAN_MSG_FIELD field_TX_torque; 
    field_TX_torque.dlc = 5; 
    field_TX_torque.idType = 0;
    field_TX_torque.formatType = 0; 
    field_TX_torque.frameType = 0; 
    field_TX_torque.brs = 0;

    uint8_t data_TX_torque[3] = {
        is_hv_requested(), 
        (uint16_t)(throttle1 * THROTTLE_MULTIPLIER) >> 8, // torque request upper
        (uint16_t)(throttle1 * THROTTLE_MULTIPLIER) & 0b11111111 // torque request lower
    };

    msg_TX_torque.field = field_TX_torque; 
    msg_TX_torque.msgId = TORQUE_REQUEST_COMMAND;
    msg_TX_torque.data = data_TX_torque;

    CAN1_Transmit(CAN1_TX_TXQ, &msg_TX_torque);
}

/*
                         Main application
 */
int main(void)
{
    // initialize the device
    SYSTEM_Initialize();

    // Set up ADCC for reading analog signals
    //ADCC_DischargeSampleCapacitor();
    
    // Set up CAN
    CAN1_OperationModeSet(CAN_CONFIGURATION_MODE);
    if(CAN_CONFIGURATION_MODE == CAN1_OperationModeGet()) {
        if(CAN_OP_MODE_REQUEST_SUCCESS == CAN1_OperationModeSet(CAN_NORMAL_2_0_MODE)) {
            // set up successful
            // blink lights here
            // (tested and worked)
        }
    }

    // Only for debugging. Use this to test the controls on the breadboard
    #if 0
    while (1)
    {
        update_sensor_vals();
        printf("Throttle 1: %d\r\n", throttle1);
        printf("Throttle 2: %d\r\n", throttle2);
        printf("Brake : %d\r\n", brake);
        printf("HV switch: %d\r\n", is_hv_requested());
        printf("Drive switch: %d\r\n\n", is_drive_requested());
    }
    #endif
    
    printf("Starting in %s state", STATE_NAMES[state]);
    
    while (1) {
        // Main FSM
        // Source: https://docs.google.com/document/d/1q0RL4FmDfVuAp6xp9yW7O-vIvnkwoAXWssC3-vBmNGM/edit?usp=sharing
        
        printf("-----------------------\r\n");
        
        // CAN receive
        can_receive();
       
        // CAN transmit state
        CAN_MSG_OBJ msg_TX_state;
        
        CAN_MSG_FIELD field_TX_state; 
        field_TX_state.dlc = 1; 
        field_TX_state.idType = 0;
        field_TX_state.formatType = 0; 
        field_TX_state.frameType = 0; 
        field_TX_state.brs = 0; 
        
        uint8_t data_TX_state[1] = {state}; 
        if (state == FAULT) { 
            data_TX_state[0] = 0b10000000 + error; // greatest bit = 1 if fault 
        }
        
        msg_TX_state.field = field_TX_state; 
        msg_TX_state.data = data_TX_state;
        msg_TX_state.msgId = VEHICLE_STATE;
        CAN1_Transmit(CAN1_TX_TXQ, &msg_TX_state);
        
        
        // CAN transmit brake command
        CAN_MSG_OBJ msg_TX_brake;
        
        CAN_MSG_FIELD field_TX_brake; 
        field_TX_brake.dlc = 1; 
        field_TX_brake.idType = 0;
        field_TX_brake.formatType = 0; 
        field_TX_brake.frameType = 0; 
        field_TX_brake.brs = 0; 
        
        uint8_t data_TX_brake[1] = {brake}; 
        
        msg_TX_brake.field = field_TX_state; 
        msg_TX_brake.data = data_TX_state;
        msg_TX_brake.msgId = BRAKE_COMMAND;
        CAN1_Transmit(CAN1_TX_TXQ, &msg_TX_brake);
        
        
        //  CAN transmit torque request command 
        CAN_MSG_OBJ msg_TX_torque;

        CAN_MSG_FIELD field_TX_torque; 
        field_TX_torque.dlc = 5; 
        field_TX_torque.idType = 0;
        field_TX_torque.formatType = 0; 
        field_TX_torque.frameType = 0; 
        field_TX_torque.brs = 0;

        uint8_t data_TX_torque[3] = {
            is_hv_requested(), 
            (uint16_t)(throttle1 * THROTTLE_MULTIPLIER) >> 8, // torque request upper
            (uint16_t)(throttle1 * THROTTLE_MULTIPLIER) & 0b11111111 // torque request lower
        };

        msg_TX_torque.field = field_TX_torque; 
        msg_TX_torque.msgId = TORQUE_REQUEST_COMMAND;
        msg_TX_torque.data = data_TX_torque;

        CAN1_Transmit(CAN1_TX_TXQ, &msg_TX_torque);
        
        
        // CAN transmit switch (only for debugging)
//        CAN_MSG_OBJ msg_TX_switch;
//        CAN_MSG_FIELD field_TX_switch; 
//
//        field_TX_switch.dlc = 1; 
//        field_TX_switch.idType = 0;
//        field_TX_switch.formatType = 0; 
//        field_TX_switch.frameType = 0; 
//        field_TX_switch.brs = 0; 
//        
//        msg_TX_switch.field = field_TX_switch; 
//        msg_TX_switch.msgId = SWITCHES;
//        uint8_t data_TX_switch[1] = {switches}; 
//        msg_TX_switch.data = data_TX_switch;
//        CAN1_Transmit(CAN1_TX_TXQ, &msg_TX_switch);
        

        switch (state) {
            case LV:
                run_calibration();
                
                if (is_drive_requested()) {
                    // Drive switch should not be enabled during LV
                    report_fault(DRIVE_REQUEST_FROM_LV);
                    break;
                }

                if (is_hv_requested()) {
                    // HV switch was flipped
                    
                    // Set throttle and brake range since calibration is done
                    throttle_range = throttle1_max - throttle1_min;
                    brake_range = brake_max - brake_min; // idk where this is even used
                    
                    CAN_transmit_torque_request();
                    
                    // Start charging the car to high voltage state
                    change_state(PRECHARGING);
                } 
                
                break;
            case PRECHARGING:
                if (!is_hv_requested()) {
                    conservative_timer_ms = 0;
                    change_state(LV);
                }
                
                if (conservative_timer_ms >= MAX_CONSERVATION_SECS * 1000) {
                    // Pre-charging took too long
                    conservative_timer_ms = 0;
                    report_fault(CONSERVATIVE_TIMER_MAXED);
                    break;
                }
                     
                if (capacitor_volt > PRECHARGE_THRESHOLD) {
                    // Finished charging to HV on time
                    conservative_timer_ms = 0;
                    change_state(HV_ENABLED);
                    break;
                }
                
                // Check if pre-charge is finished for every delay
                __delay_ms(AWAIT_PRECHARGING_DELAY_MS);
                conservative_timer_ms += AWAIT_PRECHARGING_DELAY_MS;
                break;
            case HV_ENABLED:
                update_sensor_vals();

                if (!is_hv_requested() || capacitor_volt < PRECHARGE_THRESHOLD) {
                    // Driver flipped off HV switch
                    change_state(LV);
                    break;
                }
                
                if (is_drive_requested()) {
                    // Driver flipped on drive switch
                    // Need to press on pedal at the same time to go to drive
                    if (brake >= PEDAL_MAX - BRAKE_ERROR_TOLERANCE) {
                        change_state(DRIVE);                        
                    } else {
                        // Driver didn't press pedal
                        report_fault(BRAKE_NOT_PRESSED);
                    }
                }
                
                break;
            case DRIVE:
                update_sensor_vals();

                if (!is_drive_requested()) {
                    // Drive switch was flipped off
                    // Revert to HV
                    change_state(HV_ENABLED);
                   break;
                }

                if (!is_hv_requested()) {
                    // HV switched flipped off, so can't drive
                    report_fault(HV_DISABLED_WHILE_DRIVING);
                    break;
                }

                if (brake_implausible()) {
                    report_fault(BRAKE_IMPLAUSIBLE);
                }
                
                break;
            case FAULT:
                switch (error) {
                    case DRIVE_REQUEST_FROM_LV:
                        if (!is_drive_requested()) {
                            // Drive switch was flipped off
                            // Revert to LV
                            change_state(LV);
                        }
                        break;
                    case CONSERVATIVE_TIMER_MAXED:
                        if (!is_hv_requested() && !is_drive_requested()) {
                            // Drive and HV switch must both be reset
                            // to revert to LV
                            change_state(LV);
                        }
                        break;
                    case BRAKE_NOT_PRESSED:
                        if (!is_drive_requested()) {
                            // Ask driver to reset drive switch and try again
                            change_state(HV_ENABLED);
                        }
                        break;
                    case HV_DISABLED_WHILE_DRIVING:
                        update_sensor_vals();

                        if (!is_drive_requested()) {
                            // Ask driver to flip off drive switch to properly go back to LV
                            change_state(LV);
                        }
                        break;
                    case SENSOR_DISCREPANCY:
                        update_sensor_vals();
                        
                        // stop power to motors if discrepancy persists for >100ms
                        // see rule T.4.2.5 in FSAE 2022 rulebook
                        if (discrepancy_timer_ms > MAX_DISCREPANCY_MS) {
                            discrepancy_timer_ms = 0;
                            change_state(LV);
                        }

                        if (!has_discrepancy()) {
                            discrepancy_timer_ms = 0;
                            // if discrepancy resolved, change back to previous state
                            if (temp_state == FAULT) {
                                discrepancy_timer_ms = 0;
                                report_fault(temp_error);
                            } else {
                                discrepancy_timer_ms = 0;
                                change_state(temp_state);
                            }
                        }
                        __delay_ms(AWAIT_DISCREPANCY_DELAY_MS);
                        discrepancy_timer_ms += AWAIT_DISCREPANCY_DELAY_MS;
                        
                        break;
                    case BRAKE_IMPLAUSIBLE:
                        update_sensor_vals();

                        if (!brake_implausible()) {
                            change_state(DRIVE);
                        }
                    case ESTOP:
                        if (!is_hv_requested() && !is_drive_requested()) {
                            change_state(LV);
                        }
                }
                break;
        }
        
    }

}


/**
 End of File
*/

