/*
 * <:copyright-BRCM:2013:DUAL/GPL:standard
 * 
 *    Copyright (c) 2013 Broadcom Corporation
 *    All Rights Reserved
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation (the "GPL").
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * 
 * A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php, or by
 * writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 * 
 * :>
 */
// BCMFORMAT: notabs reindent:uncrustify:bcm_minimal_i4.cfg

#include "boardparms.h"
#include "bcm_led.h"
#include "bcm_pinmux.h"
#include "shared_utils.h"

#ifdef _CFE_                                                
#include "lib_types.h"
#include "lib_printf.h"
#include "lib_string.h"
#include "bcm_map.h"
#define printk  printf
#else // Linux
#include <linux/kernel.h>
#include <linux/module.h>
#include <bcm_map_part.h>
#include <linux/string.h>
#endif

// #define BCM_LED_DEBUG 1

/*
  These are low level functions that can be called from CFE or from the Linux board driver
  The Linux board driver handles any necessary locking so these functions should not be called
  directly from elsewhere.
*/

/* 
 bcm_led_driver_set(number, state) -- on/off
 bcm_led_driver_toggle(number) 
 future: bcm_led_driver_brightness(number, brighness) -- 0-255
 future: bcm_led_driver_map_glow(number, state, waveform[])

 For now, 
    stete 0 = off
    state 1 = on
 This will be replaced with a more flexible set of states, preserving 0 and 1

*/

static short g_optled_map[BP_PINMUX_MAX];


static struct bcm_led_driver_state {
    unsigned short led_state;
    unsigned short led_bp;
    unsigned char brightness;
    unsigned char is_hw_led;
    // glow_mode will go here eventually
} bcm_led_driver_state[LED_NUM_LEDS];

static int serial_is_inverted = 0;
static int old_led_ctrl = 0x0;
static volatile uint32* LedData;

static void led_do_set(unsigned short num) {
    unsigned short val;
    int al = 0;
    //int current;
    int serialinvert = 0;

    val =  bcm_led_driver_state[num].led_state;
    if( old_led_ctrl ) {
        al =  ((bcm_led_driver_state[num].led_bp & BP_ACTIVE_MASK) == BP_ACTIVE_LOW) ? 1 : 0;
        serialinvert =  (serial_is_inverted && (bcm_led_driver_state[num].led_bp & BP_GPIO_SERIAL)) ? 1 : 0;
    }

    // current = LED->mask & LED_NUM_TO_MASK(num);
    // FIXME - If brightness not 100%, need to compute both brightness and value every time
    if ( bcm_led_driver_state[num].is_hw_led ) {
        // HW LEDs have HW disabled when not in the ON state
        if (val) {
            *LedData |= LED_NUM_TO_MASK(num);
            LED->hWLedEn |= LED_NUM_TO_MASK(num);
        } else {
            *LedData &= ~LED_NUM_TO_MASK(num);
            LED->hWLedEn &= ~LED_NUM_TO_MASK(num);
        }
    } else {
        val = val ^ al ^ serialinvert ;
#ifdef BCM_LED_DEBUG
        printk("LED %d val %d after inversion by al %d invert %d\n",num,val,al,serialinvert);
#endif
        if (val) {
            *LedData |= LED_NUM_TO_MASK(num);
        } else {
            *LedData &= ~LED_NUM_TO_MASK(num);
        }
    }
}

/* bcm_led_driver_get_optled_map()
   permits pinmux init code to get a pointer to the optled_map.  Any time that pinmux init code 
   creates a mapping where a specific GPIO NUMBER maps to an LED register bit number. this
   map must be populated.  This includes direct mapping when, for example, GPIO3 maps to LED3
*/
short * bcm_led_driver_get_optled_map(void)
{
    return(g_optled_map);
}

void bcm_led_driver_set(unsigned short num, unsigned short state)
{
    unsigned short led;
#ifdef BCM_LED_DEBUG
    printk("LED %x set state %d\n",num,state);
#endif
    led = num & BP_GPIO_NUM_MASK;
    led = g_optled_map[led];
    bcm_led_driver_state[led].led_state = state;
    bcm_led_driver_state[led].led_bp = num;
    led_do_set(led);
}

void bcm_led_driver_toggle(unsigned short num)
{
    unsigned short led;
    led = num & BP_GPIO_NUM_MASK;
    led = g_optled_map[led];
    if ( bcm_led_driver_state[led].is_hw_led ) {
        // HW LEDs just toggle
        *LedData = *LedData ^ LED_NUM_TO_MASK(led);
        LED->hWLedEn &= ~LED_NUM_TO_MASK(led);
    } else {
        bcm_led_driver_state[led].led_state =  bcm_led_driver_state[led].led_state ^ 1;
        bcm_led_driver_state[led].led_bp = num;
        led_do_set(led);
    }
}

void bcm_common_led_init(void) {
    int i;
    int j;
    unsigned short gpio;
    unsigned long brightwords[16];
    unsigned char *bright;

    /* 63138/63381 A0/A1 use old led controller*/
    if ( ((CHIP_FAMILY_ID_HEX == 0x63138) || (CHIP_FAMILY_ID_HEX == 0x63381)) && ((UtilGetChipRev()&0xf0) == 0xa0) ) {
        old_led_ctrl = 1;
        LedData = &LED->mask;
    } else {
        old_led_ctrl = 0;
        LedData = &LED->SwData;
    }

    if ( old_led_ctrl == 0 && CHIP_FAMILY_ID_HEX == 0x63138 ) 
        LED->HwPolarity = 0xb8000; // bit 15 and 19 for adsl activity AH, bit 16 and 17 PWR LED AH  

    bright = (unsigned char *)&brightwords;
    if( old_led_ctrl ) {
        if ((BP_SUCCESS == BpGetSerialLedData(&gpio)) && ((gpio & BP_ACTIVE_MASK) == BP_ACTIVE_LOW)) {
            LED->glbCtrl = 0x02; // Serial LEDs are not inverted
            serial_is_inverted = 0;
        } else {
            LED->glbCtrl = 0x00; // Serial LEDs are inverted
            serial_is_inverted = 1;
        }
    } else {
        serial_is_inverted = 0;
        LED->glbCtrl = 0xa; // init to ser led enable AH, failing clk clock edge parity and ser led output parity AH
        if ((BP_SUCCESS == BpGetSerialLedData(&gpio)) && ((gpio & BP_ACTIVE_MASK) == BP_ACTIVE_LOW)) 
            LED->glbCtrl = 0x8; //ser led output AL
    }

    for (i = 0; i < LED_NUM_LEDS/8 ; i++) {
        LED->brightCtrl[i] = 0x88888888;
    }
    for (i = 0; i < LED_NUM_LEDS ; i++) {
        bcm_led_driver_state[i].led_state = 0;
        bcm_led_driver_state[i].brightness = 255;
        bcm_led_driver_state[i].is_hw_led = 0;
    }
    for (j = 0 ; j < 32 ; j++) {
        bright[31-j] = bright[32+j] = 127 -  (j * j * 128 / (32 * 32) );
    }
#ifdef LED_NUM_PWM_LEDS
    for (i = 0; i < LED_NUM_PWM_LEDS ; i++) {
        for (j = 0 ; j < 16 ; j++) {
            LED->pledLut[i][j] = brightwords[j];
        }
    }
#endif
}

void bcm_common_led_setAllSoftLedsOff(void)
{
    unsigned short gpio;
    unsigned short led;
    unsigned int parallel = 0;
    unsigned int serial = 0;
    int i = 0, token = 0, rc;

    for(;;)
    {
        rc = BpGetLedGpio(i, &token, &gpio);
        if( rc == BP_MAX_ITEM_EXCEEDED )
            break;
        else if( rc == BP_SUCCESS && gpio != BP_GPIO_NONE )
        {
            led = gpio & BP_GPIO_NUM_MASK;
            led = g_optled_map[led];

            switch (gpio & (BP_ACTIVE_MASK | BP_GPIO_SERIAL)) {
            case (BP_ACTIVE_LOW | BP_GPIO_SERIAL):
                serial &= ~LED_NUM_TO_MASK(led);
                break;
            case (BP_ACTIVE_HIGH | BP_GPIO_SERIAL):
                serial |= LED_NUM_TO_MASK(led);
                break;
            case (BP_ACTIVE_LOW):
                parallel &= ~LED_NUM_TO_MASK(led);
                break;
            case (BP_ACTIVE_HIGH):
                parallel |= LED_NUM_TO_MASK(led);
                break;
            }

            if (LED->hWLedEn & LED_NUM_TO_MASK(led)) {
                bcm_led_driver_state[led].is_hw_led = 1;
#ifdef BCM_LED_DEBUG
                printk("off: LED %d is HW\n",led);
#endif
            } else {
                bcm_led_driver_set( gpio, 0 );
#ifdef BCM_LED_DEBUG
                printk("off: LED %d is OFF\n",led);
#endif
            }
        }
        else
        {
            token = 0;
            i++;
        }
    }
#ifdef BCM_LED_DEBUG
    for (i = 0; i < 64 ; i++) {
        if ((i & 0x7) == 0) {
            printk("\noptled_map %d:",i);
        }
        printk(" %4d",g_optled_map[i]);
    }
#endif
    if( old_led_ctrl == 0 ) {
       LED->ParallelLedPolarity = parallel;
       LED->SerialLedPolarity = serial;
#ifdef BCM_LED_DEBUG
       printk("parallel 0x%x serial 0x%x\n", parallel, serial);
#endif
    }

    return;
}


void bcm_common_led_setInitial(void)
{
    unsigned short gpio;
    if( BpGetBootloaderPowerOnLedGpio( &gpio ) == BP_SUCCESS )
        bcm_led_driver_set( gpio, 1 );
    if( BpGetWanDataLedGpio( &gpio ) == BP_SUCCESS )
        bcm_led_driver_set( gpio, 0 );
}

#ifndef _CFE_ 
static int bcm_common_led_linux_init(void)
{
    bcm_common_led_init();
    bcm_common_led_setAllSoftLedsOff();
    bcm_common_led_setInitial();
}

arch_initcall(bcm_common_led_linux_init);
#endif /* ! _CFE_ */
