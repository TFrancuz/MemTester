/*
 * DRAM.c
 *
 * Created: 1/15/2024 6:25:08 PM
 *  Author: tmf
 */ 

#include <DRAM.h>
#include <avr\io.h>
#include <util/atomic.h>
#include <avr/interrupt.h>
#include <RWAccessh.h>
#include <IODefs.h>
#include <util\delay.h>

static uint32_t DRAM_LastAddr;				//Ostatni adres w DRAM pod kt�ry odbywa� si� dost�p

void IO_DRAM_Init()
{
	PORTC.DIR = 0x00;			//PC0-PC7 - linie danych D0-D7
	PORTE_DIRSET = 0b00000011;	//PE0-PE1 - linie adresowe A0-A1
	PORTA_DIRSET = 0b11111100;	//PA2-PA7 - linie adresowe A2-A7
	PORTD_DIRSET = 0xff;		//PD0-PD7 - linie adresowe A8-A15
	PORTF_OUTSET = DMEM_CAS | DMEM_WE | DMEM_OE; PORTF_DIRSET = DMEM_CAS | DMEM_WE | DMEM_OE;	//PF5 - OE, PF4 - WE, PF3 - CAS
	PORTE_DIRSET = DMEM_RAS; PORTE_OUTSET = DMEM_RAS;
}

void DRAM_Init()
{
	PORTE_OUTSET = DMEM_RAS;			//Deaktywuj RAS
	PORTF_OUTSET = DMEM_WE | DMEM_CAS | DMEM_OE;	//Deaktywuj OE/RD, CAS i WE
	_delay_ms(1);						//Po power up pami�� wymaga czasu, tu zazwyczaj niepotrzene
	for(uint8_t i = 0; i < 8; i++)		//Wi�kszo�c DRAM wymaga 8 cykli RAS w celu inicjalizacji
	{
		PORTE_OUTTGL = DMEM_RAS; asm volatile ("nop");
		PORTE_OUTTGL = DMEM_RAS; asm volatile ("nop");
	}
}

void DRAM_Timers_init()
{
	// Timer TCB0 b�dzie generowa� sygna� WE
	PORTF_DIRSET = PIN4_bm;
	PORTF_PIN4CTRL = PORT_INVEN_bm;		//Odwracamy polaryzacj� sygna��w
	PORTF_OUTCLR = PIN4_bm;				//Ze wzgl�du na inwersj� - stan wysoki
	PORTMUX_TCBROUTEA = PORTMUX_TCB0_ALT1_gc;		//WO0 na PF4
	TCB0.CTRLB = TCB_CCMPEN_bm | TCB_CNTMODE_SINGLE_gc;
	TCB0.EVCTRL = TCB_CAPTEI_bm;
	TCB0.CCMP = Mem_WriteAccessTime; //Czas trwania WE
	TCB0.CTRLA = TCB_CLKSEL_DIV1_gc | TCB_DBGRUN_bm | TCB_ENABLE_bm;
	EVSYS_USERTCB0CAPT = EVSYS_USER_CHANNEL0_gc;		//Wyzwalanie tiera poprzez kana� EVSYS0

	// Timer TCB1 b�dzie generowa� sygna� CAS
	PORTF_DIRSET = PIN5_bm;
	PORTF_PIN5CTRL = PORT_INVEN_bm;		//Odwracamy polaryzacj� sygna��w
	PORTF_OUTCLR = PIN5_bm;				//Ze wzgl�du na inwersj� - stan wysoki
	PORTMUX_TCBROUTEA = PORTMUX_TCB0_ALT1_gc | PORTMUX_TCB1_ALT1_gc;		//WO0 na PF4 i WO1 na PF5
	TCB1.CTRLB = TCB_CCMPEN_bm | TCB_CNTMODE_SINGLE_gc;
	TCB1.EVCTRL = TCB_CAPTEI_bm;
	TCB1.CCMP = Mem_ReadAccessTime; //Czas trwania CAS
	TCB1.CTRLA = TCB_CLKSEL_DIV1_gc | TCB_DBGRUN_bm | TCB_ENABLE_bm;
	EVSYS_USERTCB1CAPT = EVSYS_USER_CHANNEL1_gc;		//Wyzwalanie tiera poprzez kana� EVSYS1

	//Timer TCA0 b�dzie sterowa� po�o�eniem impuls�w generowanych przez TCB0 i TCB1
	TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc;
	TCA0.SINGLE.PER = 0xffff;	//Jest to warto�� domy�lna, ale porzebne przy reinicjalizacji timera
	TCA0.SINGLE.CMP0 = 9;
	TCA0.SINGLE.CMP1 = 8;
	TCA0.SINGLE.CNT = 10;
	TCA0.SINGLE.PERBUF = 0x8000;		//Dzi�ki temu timer po pierwszym zdarzeniu update b�dzie zablokowany
	TCA0.SINGLE.CTRLESET = TCA_SINGLE_DIR_bm;	//Odliczamy w d�, dzi�ki czemu mo�emy symulowa� tryb single shot
	EVSYS.CHANNEL0 = EVSYS_CHANNEL0_TCA0_CMP0_LCMP0_gc; //Kana� CMP0 generuje zdarzenie, kt�re wyzwala timer TCB0
	EVSYS.CHANNEL1 = EVSYS_CHANNEL0_TCA0_CMP1_LCMP1_gc; //Kana� CMP1 generuje zdarzenie, kt�re wyzwala timer TCB1
	//	TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc | TCA_SINGLE_ENABLE_bm;
}

void ReinitTimers()
{
	TCA0.SINGLE.PER = 0xffff;	//Jest to warto�� domy�lna, ale porzebne przy reinicjalizacji timera
	TCA0.SINGLE.PERBUF = 0;		//Dzi�ki temu timer po pierwszym zdarzeniu update b�dzie zablokowany
	TCA0.SINGLE.CNT = 10;
}

void DRAM_SetAddress(uint32_t addr)		//�aduje RAS  i wystawia adresy kolumn dla pami�ci DRAM, lecz nie aktywuje CAS
{
	DRAM_LastAddr = addr;				//Zapami�taj adres pod jaki si� odwo�ujemy - nie mo�emy go przechowywa� ze wzgl�du na
	//zmiany RAS/CAS
	uint16_t RASaddr = addr >> CASNo;	//RASaddr zawiera bardziej znacz�c� cz�� adresu - RAS
	uint16_t CASaddr = addr & CASMask;
	PORTE_OUTCLR = 0b00000011;
	PORTE_OUTSET = RASaddr & 0b11;			//Linie adresowe A0 i A1
	PORTA_OUTCLR = 0b11111100;
	PORTA_OUTSET = RASaddr & 0b11111100;	//Linie adresowe A2-A7
	PORTD_OUT = 0x00;
	PORTD_OUTSET = RASaddr >> 8;			//Linia A8 i teoretycznie kolejne (nieu�ywane w DRAM)
	PORTE_OUTCLR = DMEM_RAS;				//Aktywuj RAS
	asm volatile ("nop"); asm volatile ("nop"); asm volatile ("nop");
	PORTE_OUTCLR = 0b00000011;
	PORTE_OUTSET = CASaddr & 0b11;			//Linie adresowe A0 i A1
	PORTD_OUT = 0x00;
	PORTD_OUTSET = CASaddr >> 8;			//Linia A8 i teoretycznie kolejne (nieu�ywane w DRAM)
	PORTA_OUTCLR = 0b11111100;
	PORTA_OUTSET = CASaddr & 0b11111100;	//Linie adresowe A2-A7
}

void DRAM_EarlyWrite(uint32_t addr, uint8_t data)
{
	Refresh_Disable();		//Wy��cz od�wie�anie
	DRAM_SetAddress(addr);
	PORTC_DIR = 0xff;
	PORTC_OUT = data;
	TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;	//Kasujemy flag� OVF
	ReinitTimers();		//Potrzebne dla reinicjalizacji timera wyznaczaj�cego timingi
	TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc | TCA_SINGLE_ENABLE_bm;	//Aktywuj sekwencj� WR/CAS
	while(!(TCA0.SINGLE.INTFLAGS & TCA_SINGLE_OVF_bm));
	PORTE_OUTSET = DMEM_RAS;				//Deaktywuj RAS
	PORTC_DIR = 0x00;		//Magistrala danych jako wej�cie
	TCA0.SINGLE.CTRLA = 0;	//Zablokuj timer
	Refresh_Enable();		//W��cz od�wie�anie
}

uint8_t DRAM_Read(uint32_t addr)
{
	Refresh_Disable();		//Wy��cz od�wie�anie
	DRAM_SetAddress(addr);
	PORTF_OUTCLR = DMEM_OE;
	TCB1_INTFLAGS = TCB_CAPT_bm;			//Skasuj flag� capt
	EVSYS_SWEVENTA = EVSYS_SWEVENTA_CH1_gc;	//Odmierzamy czas dla strobu RD/OE
	while(!(TCB1_INTFLAGS & TCB_CAPT_bm));
	uint8_t tmpdata = PORTC_IN;
	PORTF_OUTSET = DMEM_OE;					//Deaktywuj OE/RD
	PORTE_OUTSET = DMEM_RAS;				//Deaktywuj RAS
	Refresh_Enable();		//W��cz od�wie�anie
	return tmpdata;
}

uint8_t DRAM_ReadWOAddr()
{
	return DRAM_Read(DRAM_LastAddr);	//Tu ne ma szybkiego odczytu, bo musimy wystawi� adres RAS/CAS
}

ISR(TCA1_OVF_vect)		//Od�wie�anie wg schematu RAS only
{
	static uint8_t RefreshCounter;
	
	PORTE_OUTCLR = 0b00000011;
	PORTE_OUTSET = RefreshCounter & 0b11;		//Linie adresowe A0 i A1
	PORTA_OUTCLR = 0b11111100;
	PORTA_OUTSET = RefreshCounter & 0b11111100;	//Linie adresowe A2-A7
	PORTE_OUTCLR = DMEM_RAS;					//Aktywuj RAS
	RefreshCounter++;							//Te dwie instrukcje przy okazji wyd�u�aj� czas trwania RAS
	TCA1.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;	//Skasuj flag� OVF - nie jest kasowana automatycnzie
	PORTE_OUTSET = DMEM_RAS;					//Deaktywuj RAS
}

void RefreshTimerInit()			//Timer odpowiedzialny za generowanie cykli od�wie�ania
{
	TCA1.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc;					//Tryb PWM
	TCA1.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;								//Odblokowujemy przerwanie OVF
	TCA1.SINGLE.PER = F_CPU/250/(1 << CASNo);								//Oblicz co ile przerwanie przy za�o�eniu od�wie�ania co 4 ms
}

void RefreshTimer_Enable()
{
	TCA1.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc | TCA_SINGLE_ENABLE_bm;	//Aktywuj timer
}

void RefreshTimer_Disable()
{
	TCA1.SINGLE.CTRLA = 0;	//Deaktywuj timer
}

void Refresh_Disable()
{
	TCA1.SINGLE.INTCTRL = 0;		//Zablokuj przerwanie OVF
}

void Refresh_Enable()
{
	TCA1.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;	//Odblokuj przerwanie OVF
}