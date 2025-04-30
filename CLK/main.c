#define F_CPU 1000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <util/delay.h>

volatile uint8_t millisecond=0;
volatile uint8_t second=0;
 // example values for hour and minute
volatile uint8_t minute=30;
volatile uint8_t hour=7;
// if 0: Adjust brightness, 1: Adjust minutes, 2: Adjust hours
volatile uint8_t mode=0;
volatile uint8_t button=0;
// [0..3] LEDs off to brightest setting
volatile uint8_t pwm_mode=0;
volatile uint8_t tmp;
volatile uint8_t bounce=0;
// default is high because of the pull-up
volatile uint8_t portDtmp = 0b00000111;

void PWM(void);

int main(void) {
	// set bits 0..5 in DDR of the C ports to 1, these are outputs
	DDRC = (uint8_t) 0b00111111;
	// 0..2 are buttons, 3..7 are LEDs
	DDRD = (uint8_t) 0b11111000;
	// turn on pull-up resitors pins 0..3, these will be buttons 
	PORTD = (uint8_t) portDtmp;
	
	// set PCIE2 bit to 1 -> allows interrupt enable for PCINT16..23
	PCICR |= (1 << PCIE2);
	// enable interrupt for buttons, PCINT16..18
	PCMSK2 |= (1 << PCINT18) | (1 << PCINT17) | (1 << PCINT16);
	
	GTCCR |= (1 << TSM) | (1 << PSRASY);  // GTCCR -> General Timer/Counter Control Register
	// TSM -> Timer Synchronisation Mode: PSRASY bit will not be set to 0 by hardware
	// PSRASY -> prescaler reset for timer 2
	
	// timer 0 for pulse width modulation:
	TCCR0A |= (1 << WGM01); 	// CTC mode, turns on Clear Timer on Compare Match
	TCCR0B |= (1 << CS01);  	// Prescaler 8
	OCR0A = 125 - 1; 		// TCNT0=4, then end of counter -> 256*4/1000000 = 1024ms Intervall
	TIMSK0 |= (1 << OCIE0A);  	// Enable Compare Interrupt
	TIMSK0 |= (1 << TOIE0);   	// Timer0/Counter0 Interrupt Mask Register, TOIE0 -> Overflow Interrupt enable
	
	// setup asynchronous timer for Timer 2 (quartz crystal) -> keeps time in PWR_SAVE
	ASSR |= (1 << AS2);		// turn async mode on, Timer 2 -> quartz
	TCCR2A |= (1 << WGM21); 	// CTC Mode for Timer 2
	TCCR2B |= (1 << CS22) | (1 << CS21);	// set prescaler to 256
	OCR2A = 128 - 1;
	// Timer 2 frequency = TCCR2B*OCR2A/Quartz Frequency -> 256*128/32768 = 1s intervall
	TIMSK2 |= (1 << OCIE2A);	// enable Compare Interrupt
	TIMSK2 |= (1 << TOIE2);	// enable Overflow Interrupt
	
	// start timers
	GTCCR &= ~(1 << TSM);
	sei(); // enable global interrupts, set interrupt bit to 0 and I-Bit in SREG to 1
	
	// Power saving:
	sleep_enable();
	set_sleep_mode(SLEEP_MODE_PWR_SAVE);
	power_adc_disable();
	power_twi_disable();
	power_timer1_disable();
	power_usart0_disable();
	power_timer0_enable();
	
	sleep_mode(); // toggle sleep mode
	while(1) {
		if(pwm_mode == 0) {
			set_sleep_mode(SLEEP_MODE_PWR_SAVE);
			sleep_mode();
		}
	}
}

// Pulse Width Modulation calculation
void PWM() {
	if((millisecond < ((pwm_mode) * 5) - (4) || pwm_mode == 3) && pwm_mode != 0) { // turn on LEDs constantly at brightest mode
		sleep_disable();
		tmp = minute;
		tmp = (tmp & 0xF0) >> 4 | (tmp & 0x0F) << 4;
		tmp = (tmp & 0xCC) >> 2 | (tmp & 0x33) << 2;
		tmp = (tmp & 0xAA) >> 1 | (tmp & 0x55) << 1;
		// reverse minute byte, because port C is oriented the other way
		PORTC = (tmp >> 2);
		PORTD = (hour << 3) | 0b00000111;
	} else {
		sleep_disable();
		if(mode != 1) {
			PORTC = 0x00;
		}
		if(mode != 2) {
			PORTD = (0x00 << 3) | 0b00000111;
		}
		// set LEDs unused by mode to 0, for seeing which mode you're in
	}
}

// Interruptserviceroutine for PCINT16..18
ISR(PCINT2_vect) { 
	uint8_t changedbits;
	changedbits = PIND ^ portDtmp;
	portDtmp = PIND;
	
	// % 2 for pressing down and letting go
	bounce = (bounce + 1) % 2;  
	if(bounce == 0) {		
		if(changedbits & (1 << PD0)) // PCINT16 changed, down/- button
		{
			switch(mode) {
				case 0: // BRIGHTNESS
					pwm_mode = (pwm_mode - 1) % 4;
					break;
				
				case 1: // HOUR
					hour--;
					if(hour > 24) { // overflow (0xFF), because hour == -1
						hour = 23;
					}
					break;
				
				case 2: // MINUTE
					minute--;
					if(minute > 60) {
						minute = 59;
					}
					break;
			}
		}
		
		// PCINT17 changed, up/+ button
		if(changedbits & (1 << PD1)) 
		{
			switch(mode) {
				case 0: // BRIGHTNESS
					pwm_mode = (pwm_mode + 1) % 4;
					// at brightest pwm and timer 0 get disabled and PWR SAVE sleepmodi is used
					if(pwm_mode == 3) { 
						power_timer0_disable(); // or set_sleep_mode(SLEEP_MODE_PWR_SAVE);
					} else {
						power_timer0_enable(); // or set_sleep_mode(SLEEP_MODE_IDLE);
					}
					break;
				
				case 1: // HOUR
					hour++;
					if(hour == 24) {
						hour = 0;
					}
					break;
				
				case 2: // MINUTE
					minute++;
					if(minute == 60) {
						minute = 0;
					}
					break;
			}
		}

		// PCINT18 changed, mode button
		if(changedbits & (1 << PD2))
		{
			mode = (mode + 1) % 3;
		}
	}
}

// Will be called about ever millisecond
ISR(TIMER0_COMPA_vect) {	// at Timer0 compare,
	millisecond = (millisecond + 1) % 23; // 23 is for the pulse width
	PWM();
}

// Will be called every second
ISR(TIMER2_COMPA_vect) { 	// at Timer2 compare, generate timebase 
	second++;
	if(second >= 60) {
		second = 0;
		minute++;
		if(minute >= 60) {
			minute = 0;
			hour++;
			if(hour >= 24) {hour = 0;}
		}
	}
	
	PORTB = PORTB ^ 0b00000001; // output 0.5Hz at PB0 for measuring accuracy
	PWM();
}
