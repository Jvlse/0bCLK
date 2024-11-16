#define F_CPU 1000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <util/delay.h>

volatile uint8_t millisekunde=0;
volatile uint8_t sekunde=0;
volatile uint8_t minute=30;
volatile uint8_t stunde=7;
volatile uint8_t modus=0; // Wenn 0 Helligkeit, wenn 1 Minuten, 2 Stunde einstellen
volatile uint8_t button=0;
volatile uint8_t pwm_modus=0; // 0 dunkelster Modus, 5 hellster
volatile uint8_t tmp;
volatile uint8_t prell=0;
volatile uint8_t portdtmp = 0b00000111; // default is high because the pull-up

void PWM(void);

int main(void) {
	DDRC=(uint8_t)0b00111111; // Setzt Bits 0-5 im DDR von C Ports (DDC5) auf 1, pins sind Ausgang
	DDRD=(uint8_t)0b111111000; // Letzte 3 sind Buttons, erste 5 LEDS (PD7 NOCH NICHT ANGESCHLOSSEN)
	PORTD=(uint8_t)portdtmp; // Letzte 3 Pullup-Widerstände aktiviert für Buttons, letzte 5 sind high/low Pegel
		
	// Interrupt enable
	PCICR|=(1<<PCIE2); // PCIE2 Bit auf 1 setzen -> PCINT16 bis PCINT23 aktiviert aktiviert
	PCMSK2|=(1<<PCINT18) | (1<<PCINT17) | (1<<PCINT16);// PCINT16 und PCINT17 aktivieren
	
	GTCCR|=(1<<TSM) | (1<<PSRASY);  // General Timer/Counter Control Register
	// TSM -> Timer Synchronisation Mode: PSRASY bit wird nicht von hardware resetet, PSRASY -> Prescaler Reset für Timer 2
	// Wichtig um Timer ohne probleme einzustellen
	
	// Timer0 für PWM:
	TCCR0A|=(1<<WGM01); // CTC Modus
	TCCR0B|=(1<<CS01);  // Prescaler 8
	OCR0A=125-1; // TCNT0=4, dann ENDE counter -> 256*4/1000000 = 1024ms Intervall
	// OCR0A=(0xFF*temp)/100;
	TIMSK0|=(1<<OCIE0A);  // Enable Compare Interrupt
	TIMSK0|=(1<<TOIE0);   // Timer/Counter0 Interrupt Mask Register, TOIE0 -> Overflow Interrupt enable
	
	// ASYNCHRON? Zeitbasis generieren für Timer 2 (Quarz) -> Funktioniert im PWR_SAVE sleepmode
	ASSR|=(1<<AS2);		// Asynchron Mode einschalten, Timer 2 -> Quarz
	TCCR2A|=(1<<WGM21); // CTC Modus für Timer 2
	TCCR2B|=(1<<CS22) | (1<<CS21);  // Prescaler 256
	OCR2A=128-1; // -> 256*128/32768 = 1s Intervall
	TIMSK2|=(1<<OCIE2A);
	TIMSK2|=(1<<TOIE2);
	
	GTCCR&=~(1<<TSM); // Timer starten
	sei(); // Interrupts global erlauben, interrupt bit wird 0, setzt I-Bit in SREG auf 1
	
	// STROM SPAREN:
	sleep_enable();
	set_sleep_mode(SLEEP_MODE_PWR_SAVE);
	power_adc_disable();
	power_twi_disable();
	power_timer1_disable();
	power_usart0_disable();
	power_timer0_enable(); // Wenn im power save modus timer 0 enable um pwm zu nutzen
	
	sleep_mode();
	while(1) {
		if(pwm_modus==0) {
			set_sleep_mode(SLEEP_MODE_PWR_SAVE);
			sleep_mode();
		}
	}
}

// PWM
void PWM() {
	if((millisekunde<((pwm_modus)*5)-(4) || pwm_modus==3) && pwm_modus!=0) { // bei höchstem Modus wird led komplett eingeschaltet
		sleep_disable();
		tmp=minute;
		tmp = (tmp & 0xF0) >> 4 | (tmp & 0x0F) << 4;
		tmp = (tmp & 0xCC) >> 2 | (tmp & 0x33) << 2;
		tmp = (tmp & 0xAA) >> 1 | (tmp & 0x55) << 1;
		PORTC=(tmp>>2);
		PORTD=(stunde<<3)|0b00000111;
	} else {
		sleep_disable();
		if(modus!=1) {PORTC=0x00;}
		if(modus!=2) {PORTD=(0x00<<3)|0b00000111;}
	}
}

ISR(PCINT2_vect) { // Interruptserviceroutine für PCINT16, PCINT17 und PCINT18
	uint8_t changedbits;
	changedbits = PIND ^ portdtmp; // portänderung wird mit xor erkannt, aber nur jede 2. durch prell zugelassen, da drücken und loslassen zählt
	portdtmp = PIND;
	
	prell=(prell+1)%2;
	if(prell==0) {		
		if(changedbits & (1 << PD0)) // UNTEN / PCINT16 changed
		{
			switch(modus) {
				case 0: // HELLIGKEIT
				pwm_modus=(pwm_modus-1)%4;
				break;
				
				case 1: // STUNDE
				stunde--;
				if(stunde>24) { // Overflow (0xFF), weil stunde -1
					stunde=23;
				}
				break;
				
				case 2: // MINUTE
				minute--;
				if(minute>60) { // Overflow (0xFF), weil minute -1
					minute=59;
				}
				break;
			}
		}
		
		if(changedbits & (1 << PD1)) // OBEN / PCINT17 changed
		{
			switch(modus) {
				case 0: // Helligkeit
				pwm_modus=(pwm_modus+1)%4;
				if(pwm_modus==3) { // bei hellster stufe wird pwm und timer 1 komplett ausgeschaltet und PWR SAVE sleepmodi genutzt
					power_timer0_disable();
					// oder set_sleep_mode(SLEEP_MODE_PWR_SAVE);
				} else {
					power_timer0_enable();
					// oder set_sleep_mode(SLEEP_MODE_IDLE);
				}
				break;
				
				case 1: // Stunde
				stunde++;
				if(stunde==24) {
					stunde=0;
				}
				break;
				
				case 2: //Minute
				minute++;
				if(minute==60) {
					minute=0;
				}
				break;
			}
		}
		
		if(changedbits & (1 << PD2))
		{
			modus=(modus+1)%3; // PCINT18 changed
		}
	}
}

ISR(TIMER0_COMPA_vect) {
	millisekunde=(millisekunde+1)%23;
	PWM();
}

ISR(TIMER2_COMPA_vect) { // Bei Timer2 Compare, Zeitbasis generieren
	sekunde++;
	if(sekunde>=60) {
		sekunde=0;
		minute++;
		if(minute>=60) {
			minute=0;
			stunde++;
			if(stunde>=24) {stunde=0;}
		}
	}
	
	PORTB = PORTB ^ 0b00000001; // ausgabe von 0.5Hz an PB0
	PWM();
}