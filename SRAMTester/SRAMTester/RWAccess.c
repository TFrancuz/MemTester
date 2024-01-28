/*
 * RWAccess.c
 *
 * Created: 12/3/2023 12:15:19 PM
 *  Author: tmf
 */ 

#include <RWAccessh.h>
#include <avr/io.h>
#include <IODefs.h>

uint16_t Mem_WriteAccessTime = 1;	//Czas dostêpu do pamiêci przy zapisie - w tykniêciach timera
uint16_t Mem_ReadAccessTime = 1;	//Czas dostêpu do pamiêci przy odczycie - w tykniêciach timera

void IO_Parallel_Init()
{
	PORTC.DIR = 0x00;			//PC0-PC7 - linie danych D0-D7
	PORTE_DIRSET = 0b00000011;	//PE0-PE1 - linie adresowe A0-A1
	PORTA_DIRSET = 0b11111100;	//PA2-PA7 - linie adresowe A2-A7
	PORTD_DIRSET = 0xff;		//PD0-PD7 - linie adresowe A8-A15
	PORTB_DIRSET = 0b00110000;	//PB4-PB5 - linie adresowe A16-A17
	PORTF_DIRSET = 0b00000100;	//PF2 - linia adresowa A18
	//	PORTF_OUTSET = PIN4_bm; PORTF_DIRSET = PIN4_bm;	//PF4 -	WE
	PORTF_OUTSET = PIN5_bm; PORTF_DIRSET = PIN5_bm;	//PF5 - OE
	PORTF_OUTSET = PIN3_bm; PORTF_DIRSET = PIN3_bm;	//PF3 - CS
}

void IO_Bus_Off()
{
	PORTC.DIR = 0x00;		//Magistrala danych
	PORTE_DIRCLR = 0b00000011;	//PE0-PE1 - linie adresowe A0-A1
	PORTA_DIRCLR = 0b11111100;	//PA2-PA7 - linie adresowe A2-A7
	PORTD_DIRCLR = 0xff;		//PD0-PD7 - linie adresowe A8-A15
	PORTB_DIRCLR = 0b00110000;	//PB4-PB5 - linie adresowe A16-A17
	PORTF_DIRCLR = 0b00000100;	//PF2 - linia adresowa A18
	//	PORTF_DIRSET = PIN4_bm;	//PF4 -	WE
	PORTF_DIRCLR = PIN5_bm;	//PF5 - OE
	PORTF_DIRCLR = PIN3_bm;	//PF3 - CS
}

void InitRDWRTimer()
{
	// Timer TCB0 bêdzie generowa³ sygna³ WE
	PORTF_DIRSET = PIN4_bm;
	PORTF_PIN4CTRL = PORT_INVEN_bm;		//Odwracamy polaryzacjê sygna³ów
	PORTF_OUTCLR = PIN4_bm;	//Ze wzglêdu na inwersjê - stan wysoki
	PORTMUX_TCBROUTEA = PORTMUX_TCB0_ALT1_gc;		//WO0 na PF4
	TCB0.CTRLB = TCB_CCMPEN_bm | TCB_CNTMODE_SINGLE_gc;
	TCB0.EVCTRL = TCB_CAPTEI_bm;
	TCB0.CCMP = Mem_WriteAccessTime;		//Czas dostêpu przy zapisie
	TCB0.CTRLA = TCB_CLKSEL_DIV1_gc | TCB_DBGRUN_bm | TCB_ENABLE_bm;
	EVSYS_USERTCB0CAPT = EVSYS_USER_CHANNEL0_gc;		//Wyzwalanie tiera poprzez kana³ EVSYS0

	// Timer TCB1 bêdzie generowa³ sygna³ OE
	//	PORTF_DIRSET = PIN5_bm;
	//	PORTF_PIN5CTRL = PORT_INVEN_bm;		//Odwracamy polaryzacjê sygna³ów
	PORTMUX_TCBROUTEA = PORTMUX_TCB0_ALT1_gc | PORTMUX_TCB1_ALT1_gc;		//WO0 na PF4 i WO1 na PF5
	//TCB1.CTRLB = TCB_CCMPEN_bm | TCB_CNTMODE_SINGLE_gc;
	TCB1.CTRLB = TCB_CNTMODE_SINGLE_gc;		//Tryb single, ale bez sterowania pinem IO
	TCB1.EVCTRL = TCB_CAPTEI_bm;
	TCB1.CCMP = Mem_ReadAccessTime;			//Czas dostêpu przy odczycie
	TCB1.CTRLA = TCB_CLKSEL_DIV1_gc | TCB_DBGRUN_bm | TCB_ENABLE_bm;
	EVSYS_USERTCB1CAPT = EVSYS_USER_CHANNEL1_gc;		//Wyzwalanie tiera poprzez kana³ EVSYS1
}

void SetAddr(uint32_t addr)
{
	PORTE_OUTCLR = 0b00000011;
	PORTE_OUTSET = addr & 0b11;		//Linie adresowe A0 i A1
	PORTA_OUTCLR = 0b11111100;
	PORTA_OUTSET = addr & 0b11111100;	//Linie adresowe A2-A7
	addr >>= 8;
	PORTD_OUT = addr & 0xff;		//Linie adresowe A8-A15
	addr >>= 8;
	PORTB_OUTCLR = 0b00110000;
	PORTB_OUTSET = (addr & 0b11) << 4;	//Linie adresowe A16-A17
	PORTF_OUTCLR = 0b00000100;
	PORTF_OUTSET = addr & 0b100;		//Linia adresowa A18
}

//Operacje RW dla pamiêci SRAM
void SRAM_Write(uint32_t addr, uint8_t data)
{
	SetAddr(addr);
	PORTC_DIR = 0xff;
	PORTC_OUT = data;
	TCB0_INTFLAGS = TCB_CAPT_bm;				//Skasuj flagê capt
	EVSYS_SWEVENTA = EVSYS_SWEVENTA_CH0_gc;		//Strob WE za³atwi nam timer
	while(!(TCB0_INTFLAGS & TCB_CAPT_bm));		//Czekamy na zakoñczenie strobu WE - kolejne operacje mog¹ zmieniaæ stan
												//szyny danych lub adresowej
}

uint8_t SRAM_Read(uint32_t addr)
{
	SetAddr(addr);
	PORTC_DIR = 0x00;
	PORTF_OUTCLR = MEM_OE;						//Aktywuj OE/RD
	TCB1_INTFLAGS = TCB_CAPT_bm;				//Skasuj flagê capt
	EVSYS_SWEVENTA = EVSYS_SWEVENTA_CH1_gc;		//Odmierzamy czas dla strobu RD/OE
	while(!(TCB1_INTFLAGS & TCB_CAPT_bm));
	uint8_t tmpdata = PORTC_IN;
	PORTF_OUTSET = MEM_OE;		//Deaktywuj OE/RD
	return tmpdata;
}

uint8_t SRAM_ReadWOAddr()
{
	PORTC_DIR = 0x00;
	PORTF_OUTCLR = PIN5_bm;		//Aktywuj OE/RD
	TCB1_INTFLAGS = TCB_CAPT_bm;		//Skasuj flagê capt
	EVSYS_SWEVENTA = EVSYS_SWEVENTA_CH1_gc;		//Odmierzamy czas dla strobu RD/OE
	while(!(TCB1_INTFLAGS & TCB_CAPT_bm));
	uint8_t tmpdata = PORTC_IN;
	PORTF_OUTSET = PIN5_bm;		//Deaktywuj OE/RD
	return tmpdata;
}

//Operacje na pamiêci FLASH
void FLASH_Write(uint32_t addr, uint8_t data)
{
	SetAddr(addr);
	PORTC_DIR = 0xff;
	PORTC_OUT = data;
	PORTF_OUTCLR = MEM_CS;
	TCB0_INTFLAGS = TCB_CAPT_bm;		//Skasuj flagê capt
	EVSYS_SWEVENTA = EVSYS_SWEVENTA_CH0_gc;		//Strob WE za³atwi nam timer
	while(!(TCB0_INTFLAGS & TCB_CAPT_bm));		//Czekamy na zakoñczenie strobu WE - kolejne operacje mog¹ zmieniaæ stan
												//szyny danych lub adresowej
	PORTF_OUTSET = MEM_CS;
}

uint8_t FLASH_Read(uint32_t addr)
{
	SetAddr(addr);
	PORTC_DIR = 0x00;
	PORTF_OUTCLR = MEM_CS | MEM_OE;
	TCB1_INTFLAGS = TCB_CAPT_bm;		//Skasuj flagê capt
	EVSYS_SWEVENTA = EVSYS_SWEVENTA_CH1_gc;		//Odmierzamy czas dla strobu RD/OE
	while(!(TCB1_INTFLAGS & TCB_CAPT_bm));
	uint8_t tmpdata = PORTC_IN;
	PORTF_OUTSET = MEM_OE | MEM_CS;		//Deaktywuj OE/RD
	return tmpdata;
}