/*
 * SRAMTester.c
 *
 * Created: 10/1/2023 1:23:40 PM
 * Author : tmf
 */ 

#include <avr/io.h>
#include <util/atomic.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <util\delay.h>
#include <UARTInt.h>
#include <RWAccessh.h>
#include <IODefs.h>
#include <FLASH.h>
#include <DRAM.h>
#include <UART_Parser.h>

//Domyœlne parametry testowanego uk³adu
IC_Type_t IC_Type = IC_SRAM;				//Typ uk³adu, który testujemy
uint8_t AddrNo = 16, CASNo = 8, DataNo = 8;	//Parametry testowanego uk³adu
uint16_t CASMask = 255;						//Maska bitowa dla adresu kolumny

//WskaŸniki na funkcje dostêu do pamiêci
Write_t MemWriteFunc;			//WskaŸnik na funkcjê zapisu komórki pamiêci (adres, wartoœæ)
Read_t MemReadFunc;				//WskaŸñik na funkcjê odczytu komórki pamiêci (adres)
ReadFast_t MemFastReadFunc;		//WskaŸnik na funkcjê odczytu komórki pamiêci spod ostatniego u¿ytego adresu

void IO_DRAM_Init();						//Inicjalizuje piny IO wykorzystywane do komunikacji z DRAM
void DRAM_Init();							//Inicjalizuje pamiêæ DRAM - seria impulsów na RAS

//Makro umieszczaj¹ce zadany ³añcuch w przestrzeni adresowej __flash
#define PGM_STR(X) ((const char[]) { X })

const char msg_UnknownCmd[];
const char msg_OK[] = PGM_STR("OK\r\n");
const char msg_UnknownCmd[] = PGM_STR(" - unknown command\r\n");		//Nie rozpoznane polecenie przes³ane z PC
const char msg_Err[] = PGM_STR("Err\r\n");
const char msg_InvalidArgument[] = PGM_STR("Invalid argument\r\n");
const char mgs_OutOfRange[] = PGM_STR("Argument out of range\r\n");

typedef _Bool (*CmdHandler)(char *param, char **last);

typedef struct
{
	const char *cmd;                //WskaŸnik do polecenia w formacie ASCIIZ
	const CmdHandler  Handler;      //Funkcja realizuj¹ca polecenie
} Command;

_Bool Cmd_A(char *param, char **last);			//Ustaw liczbê linii adresowych
_Bool Cmd_D(char *param, char **last);			//Ustaw liczbê linii danych
_Bool Cmd_CAS(char *param, char **last);		//Ustaw liczbê linii adresowych dla sygna³u RAS
_Bool Cmd_SRAM(char *param, char **last);		//Ustaw typ pamiêci SRAM
_Bool Cmd_DRAM(char *param, char **last);		//Ustaw typ pamiêci DRAM
_Bool StartMemTesting(char *param, char **last);	//Test pamiêci
_Bool MemAccessTime(char *param, char **last);	//Czas dostêpu do pamiêci
_Bool DRAM_CBRTest(char *param, char **last);	//Test odœwie¿ania CBR

//Funkcja pobiera jeden argument z ci¹gu i zwraca go jako uint8_t, zwraca true jeœli ok, false, jeœli konwersja siê nie powiod³a
_Bool GetUInt8Argument(char *param, char **last, uint8_t *val)
{
	char* end;
	param=strtok_r(*last, " ,", last);	//Wyszukaj ci¹g rozdzielaj¹cy - pobieramy offset na stronie
	if(param == NULL) return false;     //B³¹d

	*val = strtol(param, &end, 0);		//Pobierz offset na programowanej stronie
	if(*end) return false;	//B³¹d
	return true;
}

_Bool GetUInt16Argument(char *param, char **last, uint16_t *val)
{
	char* end;
	param=strtok_r(*last, " ,", last);	//Wyszukaj ci¹g rozdzielaj¹cy - pobieramy offset na stronie
	if(param == NULL) return false;     //B³¹d

	*val = strtol(param, &end, 0);		//Pobierz offset na programowanej stronie
	if(*end) return false;	//B³¹d
	return true;
}

_Bool GetUInt32Argument(char *param, char **last, uint32_t *val)
{
	char* end;
	param=strtok_r(*last, " ,", last);	//Wyszukaj ci¹g rozdzielaj¹cy - pobieramy offset na stronie
	if(param == NULL) return false;     //B³¹d

	*val = strtol(param, &end, 0);		//Pobierz offset na programowanej stronie
	if(*end) return false;	//B³¹d
	return true;
}

_Bool Cmd_A(char *param, char **last)			//Ustaw liczbê linii adresowych
{
	if(GetUInt8Argument(param, last, &AddrNo) == false)
	{
		USART_SendText(msg_InvalidArgument);
		return false;
	}
	if((AddrNo < 1) || (AddrNo > 19))
	{
		USART_SendText(mgs_OutOfRange);	
		return false;
	}
	return true;
}

_Bool Cmd_D(char *param, char **last)			//Ustaw liczbê linii danych
{
	if(GetUInt8Argument(param, last, &DataNo) == false) 
	{
		USART_SendText(msg_InvalidArgument);		
		return false;
	}
	if((DataNo < 1) || (DataNo > 8))
	{
		USART_SendText(mgs_OutOfRange);
		return false;
	}
	return true;
}

_Bool Cmd_CAS(char *param, char **last)			//Ustaw liczbê linii adresowych dla sygna³u RAS
{
	if(GetUInt8Argument(param, last, &CASNo) == false)
	{
		USART_SendText(msg_InvalidArgument);
		return false;
	}
	if((CASNo > 9) || (CASNo > (AddrNo >> 1)))	//Linii /CAS nie mo¿e byæ wiêcej ni¿ po³owa linii adresowych
	{
		USART_SendText(mgs_OutOfRange);
		return false;
	}
	CASMask = 0x01;
	for(uint8_t i = 1; i < CASNo; i++) CASMask = (CASMask << 1) | 0x01;	//Maska zale¿na od liczby linii CAS
	return true;
}	

_Bool Cmd_SRAM(char *param, char **last)		//Ustaw typ pamiêci SRAM
{
	IC_Type = IC_SRAM;
	MemWriteFunc = SRAM_Write;				//Funkcje dostêpu do pamiêci SRAM
	MemReadFunc = SRAM_Read;
	MemFastReadFunc = SRAM_ReadWOAddr;
	return true;
}

_Bool Cmd_DRAM(char *param, char **last)		//Ustaw typ pamiêci DRAM
{
	IC_Type = IC_DRAM;
	MemWriteFunc = DRAM_EarlyWrite;				//Funkcje dostêpu do pamiêci DRAM
	MemReadFunc = DRAM_Read;
	MemFastReadFunc = DRAM_ReadWOAddr;
	return true;
}

_Bool Cmd_FLASH(char *param, char **last)		//Ustaw typ pamiêci FLASH
{
	Cmd_SRAM(param, last);						//FLASH inicjujemy tak jak SRAM
	IC_Type = IC_FLASH;							//Tylko oznaczamy jako FLASH :)
	return true;
}

_Bool DRAM_CBRTest(char *param, char **last)	//Test odœwie¿ania CBR
{
	uint16_t ChkAndChange(uint8_t val)	//SprawdŸ czy wybrane bity w ka¿dym rzêdzie maj¹ wartoœæ val i zmieñ ich stan na przeciwny
	{									//zwraca liczbê rzêdów z poprawnymi bitami
		uint16_t cnt = 0;				//Liczba odczytanych rzêdów
		PORTC_OUT = val ^ 0xFF;			//Bêdziemy zapisywaæ inwersjê val
		val &= 0x01;					//Tylko D0 siê liczy
		
		for(uint16_t RAS = 0; RAS <= CASMask; RAS++)
		{
			//Sekwencja CAS before RAS - aktywacja odœwie¿ania
			TCB1.CTRLB = TCB_CNTMODE_SINGLE_gc;		//Od³¹czamy TCB1 od sygna³u CAS, co powoduje jego przejœcie w stan niski
			PORTE_OUTCLR = DMEM_RAS;				//Aktywuj RAS
			TCB1.CTRLB = TCB_CCMPEN_bm | TCB_CNTMODE_SINGLE_gc;	//Aktywuj sterowanie CAS przez timer, co powoduje jego przejœcie w stan wysoki

			//Sekwencja odczytu pamiêci z rzêdu okreœlanego przez wewnêtrzny licznik odœwie¿ania RAS
			TCB1_INTFLAGS = TCB_CAPT_bm;			//Skasuj flagê capt
			EVSYS_SWEVENTA = EVSYS_SWEVENTA_CH1_gc;	//Odmierzamy czas dla strobu CAS
			PORTF_OUTCLR = DMEM_OE;
			while(!(TCB1_INTFLAGS & TCB_CAPT_bm));	//Czekamy na koniec CAS
			uint8_t tmpdata = PORTC_IN & 0x01;		//Interesuje nas wy³¹cznie bit D0
			PORTF_OUTSET = DMEM_OE;					//Deaktywuj OE/RD
			if(tmpdata == val) cnt++;				//Odczytaliœmy kolejny rz¹d prawid³owo
		
			//Sekwencja zapisu do DRAM wartoœci val w kolumnie okreœlonej przez wewnêtrzny licznik RAS
			PORTC_DIR = 0xFF;				//D0-D7 - wyjœcia
			TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;	//Kasujemy flagê OVF
			ReinitTimers();		//Potrzebne dla reinicjalizacji timera wyznaczaj¹cego timingi
			TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc | TCA_SINGLE_ENABLE_bm;	//Aktywuj sekwencjê WR/CAS
			while(!(TCA0.SINGLE.INTFLAGS & TCA_SINGLE_OVF_bm));
			PORTE_OUTSET = DMEM_RAS;		//Deaktywuj RAS
			PORTC_DIR = 0x00;				//D0-D7 - wejœcia
		}
		return cnt;		//Ile odczytaliœmy poprawnie rzêdów?
	}
	
	if(IC_Type != IC_DRAM)
	{
		USART_SendText("CBR test is only for DRAM\r\n");
		return false;
	}

	IO_DRAM_Init();				//Inicjalizacja pinów IO zwi¹zanych z DRAM
	DRAM_Timers_init();			//Inicjalizacja timerów generuj¹cych CAS i WE
	TCA0.SINGLE.CMP0 = 7;		//Poniewa¿ nie korzystamy z early write, zmieniamy po³o¿enie impulsów CAS/WE
	TCA0.SINGLE.CMP1 = 9;		//WE jest opóŸniony wzglêdem CAS

	DRAM_Init();				//Inicjalizacja pamiêci DRAM

	PORTF_OUTSET = DMEM_CAS;	//Aktywuj CAS - na pinie jest w³¹czona inwersja sygna³u,
								//Normalnie pinem steruje timer TCB1
								
	ChkAndChange(1);			//Wype³nij wybrane komórki w ka¿dym rzêdzie zerami
	uint16_t rows1 = ChkAndChange(0);	//SprawdŸ czy s¹ tam jedynki?
	uint16_t rows2 = ChkAndChange(1);	//SprawdŸ czy s¹ tam jedynki?
	
	char txtbuf[255];
	sprintf(txtbuf, "Number of rows tested OK: %d, %d out of %d\r\n", rows1, rows2, CASMask + 1);
	USART_SendText(txtbuf);
	return true;
}

_Bool MemAccessTime(char *param, char **last)
{
	_Bool GettAccessTimeVal(char *param, char **last, uint16_t *AccessTime)
	{
		if(GetUInt16Argument(param, last, AccessTime) == false)return false;
		if((*AccessTime < 1) || (*AccessTime > 10000))
		{
			USART_SendText(mgs_OutOfRange);
			return false;
		}
		return true;
	}
	
	char tbl[255];
	uint16_t AccessTime;		//Wskazany czas dostêu do pamiêci

	if(GettAccessTimeVal(param, last, &AccessTime) == true)
	{
		Mem_WriteAccessTime = (AccessTime * (F_CPU / 1000000) + 500) / 1000;	//Oblicz opóŸnienie dla timera
		char *tmplast = *last;		//Wymczasowo przechowujemy wartoœæ na któr¹ wskazuje wskaŸnik
		if(GettAccessTimeVal(param, last, &AccessTime) == false)
		{
			*last = tmplast;		//W przypadku b³êdu odtwarzamy poprzedni¹ wartoœæ	
		}
		Mem_ReadAccessTime = (AccessTime * (F_CPU / 1000000) + 500) / 1000;		//Access time dla odczytu
		sprintf(tbl, "Access time - write: %i ns, read: %i ns.\r\n", (uint16_t)((Mem_WriteAccessTime * 1000)/(F_CPU/1000000UL)),
																	 (uint16_t)((Mem_ReadAccessTime * 1000)/(F_CPU/1000000UL)));
		USART_SendText(tbl);		//Informujemy u¿ytkownika o rzeczywistym, ustawionym czasie dostêpu
		return true;
	}
	USART_SendText(msg_InvalidArgument);	//Wyst¹pi³ b³¹d w argumencie
	return false;
}

//Lista rozpoznawanych poleceñ
const Command Polecenia[]={{PGM_STR("-SRAM"), Cmd_SRAM}, {PGM_STR("-DRAM"), Cmd_DRAM}, {PGM_STR("-FLASH"), Cmd_FLASH},
						   {PGM_STR("-A"), Cmd_A}, {PGM_STR("-D"), Cmd_D}, {PGM_STR("-CAS"), Cmd_CAS},
						   {PGM_STR("-Test"), StartMemTesting}, {PGM_STR("-AccessTime"), MemAccessTime},
						   {PGM_STR("-Read"), ReadMemory}, {PGM_STR("-Write"), FLASHWrite}, {PGM_STR("-ID"), FLASHReadSignature},
						   {PGM_STR("-CBRTest"), DRAM_CBRTest}   };

void InterpretCommand(char *cmdline)
{
	_Bool retVal = false;
	uint8_t indeks;
	char *last = cmdline;
	uint8_t max_indeks=sizeof(Polecenia)/sizeof(Polecenia[0]);
	char *cmd;
	do{	
		cmd = strtok_r(last, " ", &last); //Wydziel polecenie z przekazanej linii
		if(cmd != NULL) 		//Jeœli znaleziono poprawny format polecenia, to spróbujmy je wykonaæ
		{
			for(indeks = 0; indeks < max_indeks; indeks++)
			{
				if(strcmp(cmd, Polecenia[indeks].cmd) == 0) //Przeszukaj listê poleceñ
				{
					retVal = Polecenia[indeks].Handler(last, &last);   //Wywo³aj funkcjê obs³ugi przes³anego polecenia
					break;
				}
			}
			if(indeks == max_indeks)  //Jeœli polecenie nieznane lub b³¹d...
			{
				USART_SendText(cmd);			//Wyœlij polecenie, które spowodowa³o b³¹d
				USART_SendText(msg_UnknownCmd); //B³¹d - nieznane polecenie
				break;
			}
		}
	} while((cmd) && (retVal));
	if((cmd == NULL) && (retVal == true)) USART_SendText(msg_OK);
}

_Bool StartMemTesting(char *param, char **last) 
{
	_Bool DataBusTest()
	{
		char txtbuf[255];
		_Bool ret = true;
		uint8_t bitmask = 0x01;
		uint8_t dr;
		
		USART_SendText("Data bus test...");
		for(uint8_t i = 1; i < DataNo; i++) bitmask = (bitmask << 1) | 0x01;	//Maska zale¿na od szerokoœci magistrali danych
	
		uint8_t datamask = 0x01;
		for(uint8_t i = 1; i <= DataNo; i++)
		{
			MemWriteFunc(0x0000, datamask);	//Zapisujemy bie¿¹c¹ maskê
			//dr = SRAM_Read(0x0000);
			dr = MemFastReadFunc();
			if((dr & bitmask) != (datamask & bitmask))
			{
				ret = false;
				break;
			}
			datamask ^= 0xff;
			MemWriteFunc(0x0000, datamask);	//Zapisujemy bie¿¹c¹ inwersjê maski
			//dr = SRAM_Read(0x0000);
			dr = MemFastReadFunc();
			if((dr & bitmask) != (datamask & bitmask))
			{
				ret = false;
				break;
			}
			datamask ^= 0xff;
			datamask <<= 1;	//Kolejna linia danych do sprawdzenia					
		}

		if(ret == false)
		{
			sprintf(txtbuf, "failed. The mask is: 0x%02x, but read back: 0x%02x.\r\n", datamask, dr);
			USART_SendText(txtbuf);
		} else USART_SendText("OK.\r\n");
		return ret;
	}
	
	_Bool AddressBusTest()
	{
		_Bool ret = true;
		uint32_t addrmask = 0x01;
		uint8_t datamask = 0x01;
		for(uint8_t i = 1; i < DataNo; i++) datamask = (datamask << 1) | 0x01;	//Maska zale¿na od szerokoœci magistrali danych
		
		USART_SendText("Address bus test...");
		
		for(uint8_t i = 1; i <= AddrNo; i++)		//Zerujemy wszystkie komórki le¿¹ce na granicy kolejnych linii adresowych
		{
			MemWriteFunc(addrmask, 0x00);
			addrmask <<= 1; addrmask |= 0x01;
		}
		
		addrmask = 0x01;
		for(uint8_t i = 1; i <= AddrNo; i++)	//Skanujemy po kolei ka¿d¹ liniê adresow¹
		{
			if(MemReadFunc(addrmask) != 0x00)	//Normalnie ka¿da lokacja powinna zawieraæ 0x00
			{
				ret = false;
				break;
			}
			
			MemWriteFunc(addrmask, 0xff);	//Tymczasowo zapisujemy 0xff
			uint32_t tmpaddrmask = 0x01;
			for(uint8_t h = 1; h <= AddrNo; h++)	//Skanujemy po kolei ka¿d¹ liniê adresow¹
			{
				uint8_t dr = MemReadFunc(tmpaddrmask);
				if(tmpaddrmask == addrmask)
				{
					if((dr & datamask) != (0xff & datamask))
					{
						ret = false;
						break;
					}
				} else if((dr & datamask) != 0x00)
						{
							ret = false;
							break;
						}
				tmpaddrmask <<= 1;	tmpaddrmask |= 0x01;	//Kolejna linia adresowa do sprawdzenia
			}
			MemWriteFunc(addrmask, 0x00);	//Na granicy potêgi 2 powinno byæ 0x00 - odtwarzamy poprzedni¹ wartoœæ
			if(ret == false) break;			//B³¹d - dalej nie sprawdzamy
			addrmask <<= 1;	addrmask |= 0x01;			//Kolejna linia adresowa
		}
		
		if(ret == false)
		{
			char txtbuf[255];
			sprintf(txtbuf, "failed at address line 0x%06lx.\r\n", addrmask);
			USART_SendText(txtbuf);
		} else USART_SendText("OK.\r\n");		
		return ret;		
	}

#define BYTE_TO_BINARY(byte)  \
								((byte) & 0x80 ? '1' : '0'), \
								((byte) & 0x40 ? '1' : '0'), \
								((byte) & 0x20 ? '1' : '0'), \
								((byte) & 0x10 ? '1' : '0'), \
								((byte) & 0x08 ? '1' : '0'), \
								((byte) & 0x04 ? '1' : '0'), \
								((byte) & 0x02 ? '1' : '0'), \
								((byte) & 0x01 ? '1' : '0')
	
	_Bool MemTest(uint8_t pattern)
	{
		_Bool ret = true;
		char txtbuf[255];
		uint32_t addr;
		uint32_t maxaddr = 1UL << AddrNo;
		uint8_t datard = datard;
		uint8_t datamask = 0x01;
		for(uint8_t i = 1; i < DataNo; i++) datamask = (datamask << 1) | 0x01;	//Maska zale¿na od szerokoœci magistrali danych
		
		sprintf(txtbuf, "Data test with pattern 0b%c%c%c%c%c%c%c%c - ", BYTE_TO_BINARY(pattern));
		USART_SendText(txtbuf);
		
		for(addr = 0; addr < maxaddr; addr++)
		{
			MemWriteFunc(addr, pattern);
			datard = MemFastReadFunc() & datamask;
			if(datard != (pattern & datamask))
			{
				ret = false;		//Niezgodnoœæ pomiêdzy zapisem i odczytem
				break;
			}
			MemWriteFunc(addr, pattern ^ 0xff); //Negacja maski
			datard = MemFastReadFunc() & datamask;
			if(datard != ((pattern ^ 0xFF) & datamask))
			{
				ret = false;		//Niezgodnoœæ pomiêdzy zapisem i odczytem
				break;
			}
		}
	
		if(ret) USART_SendText("ok.\r\n");
			else
			{
				sprintf(txtbuf, "mismatch at addr: 0x%06lx, read back: 0x%02x\r\n", addr, datard);
				USART_SendText(txtbuf);
			}
		return ret;
	}
	
	_Bool ret = false;
	
	switch(IC_Type)		//Inicujemy podsystemy zale¿nie od typu wybranej pamiêci
	{
		case IC_FLASH:	USART_SendText("Operation not supported for FLASH memory");		//T¹ funkcj¹ nie testujemy pamiêci FLASH
						return false;
		case IC_SRAM:
						IO_Parallel_Init();			//Zainoicjuj piny IO dla bie¿¹cej konfiguracji SRAM
						InitRDWRTimer();			//Timer generuj¹cy WE i OE
						PORTF_OUTCLR = MEM_CS;		//Aktywuj CS
						break;
		case IC_DRAM:	
						IO_DRAM_Init();				//Inicjalizacja pinów IO zwi¹zanych z DRAM
						DRAM_Timers_init();			//Inicjalizacja timerów generuj¹cych CAS i WE
						RefreshTimerInit();			//Inicjalizacja timera odpowiedzialnego za odœwie¿anie
						DRAM_Init();				//Inicjalizacja pamiêci DRAM
						RefreshTimer_Enable();		//W³¹cz odœwie¿anie
						break;
	}
	
	char txtbuf[255];
	sprintf(txtbuf, "Memory Test\r\n Address lines: %i, data lines %i\r\n", AddrNo, DataNo);
	USART_SendText(txtbuf);
	sprintf(txtbuf, "Memory type: ");
	switch(IC_Type)
	{
		case IC_SRAM:	strcat(txtbuf, "SRAM\r\n"); break;
		case IC_FLASH:	strcat(txtbuf, "FLASH\r\n"); break;
		case IC_DRAM:	strcat(txtbuf, "DRAM\r\n"); break;
	}
	USART_SendText(txtbuf);
	sprintf(txtbuf, "Access time - write: %i ns, read: %i ns.\r\n", (uint16_t)((TCB0.CCMP * 1000)/(F_CPU/1000000UL)),
																	(uint16_t)((TCB1.CCMP * 1000)/(F_CPU/1000000UL)));
	USART_SendText(txtbuf);		//Informujemy u¿ytkownika o rzeczywistym, ustawionym czasie dostêpu
	
	if(DataBusTest())
	 if(AddressBusTest())
	  if(MemTest(0x00))
	   if(MemTest(0x01))
	    if((DataNo > 1) && (MemTest(0x02)))
		 if((DataNo > 2) && (MemTest(0x04)))
		  if((DataNo > 3) && (MemTest(0x08)))
		   if((DataNo > 4) && (MemTest(0x10)))
		    if((DataNo > 5) && (MemTest(0x20)))
		     if((DataNo > 6) && (MemTest(0x40)))
		 	  if((DataNo > 7) && (MemTest(0x80))) ret = true;			 
	
	USART_SendText("Test finished.\r\n");		 

	switch(IC_Type)		//Wy³¹czamy podsystemy zale¿nie od typu wybranej pamiêci
	{
		case IC_FLASH:
		case IC_SRAM:
						PORTF_OUTSET = MEM_CS;		//Deaktywuj CS
						break;
		case IC_DRAM:	RefreshTimer_Disable();		//Wy³¹cz odœwie¿anie
						break;
	}	
	
	IO_Bus_Off();
	
	return ret;
}

void DRAM_SlowRefreshTest()			//SprawdŸ jak pamiêæ siê zachowuje z wolnym odœwie¿aniem
{
	IO_DRAM_Init();				//Inicjalizacja pinów IO zwi¹zanych z DRAM
	DRAM_Timers_init();			//Inicjalizacja timerów generuj¹cych CAS i WE
	RefreshTimerInit();			//Inicjalizacja timera odpowiedzialnego za odœwie¿anie
	DRAM_Init();				//Inicjalizacja pamiêci DRAM
	RefreshTimer_Enable();		//W³¹cz odœwie¿anie
	AddrNo = 16; DataNo = 4;
	uint32_t maxaddr = 1UL << AddrNo;
	USART_SendText("Writing test pattern...");
	for(uint32_t i = 0; i < maxaddr; i++)
	{
		if(i & 1) DRAM_EarlyWrite(i, 0xaa);
			else DRAM_EarlyWrite(i, 0x55);
	}
	USART_SendText("waiting\r\n");
	TCA1.SINGLE.PER = 0xffff;
	Refresh_Disable();
	_delay_ms(1000);
	Refresh_Enable();
	
	uint8_t data;
	uint8_t datamask = 0x01;
	for(uint8_t i = 1; i < DataNo; i++) datamask = (datamask << 1) | 0x01;	//Maska zale¿na od szerokoœci magistrali danych
	uint8_t pattern1 = 0xaa & datamask;
	uint8_t pattern2 = 0x55 & datamask;
	
	uint32_t errcnt = 0;
	for(uint32_t i = 0; i < maxaddr; i++)
	{
		data = DRAM_Read(i) & datamask;
		if(i & 1)
		{
			if(data != pattern1) errcnt++;
		} else if(data != pattern2)	errcnt++;
	}
	char tmptxt[255];
	sprintf(tmptxt, "Errors: %ld\r\n", errcnt);
	USART_SendText(tmptxt);
}

static volatile uint8_t SRead[100];

int main(void)
{
	//Domyœlnie MCU startuje z wew. zegarem 4 MHz
	CPU_CCP = CCP_IOREG_gc;							//Odblokuj dostêp do rejestru chronionego
	CLKCTRL.OSCHFCTRLA = CLKCTRL_FRQSEL_24M_gc;		//Zegar 24 MHz
    
	UART_Init();				//Inicjalizacja USART
	sei();						//Odblokowjemy przerwania
	
	PORTB_DIRSET = PIN3_bm;		//Pin steruj¹cy LEDem
	Cmd_SRAM(NULL, NULL);		//Domyœlnie obs³ugujemy SRAM
	
/*Mem_WriteAccessTime = (120 * (F_CPU / 1000000) + 500) / 1000;
Mem_ReadAccessTime = (120 * (F_CPU / 1000000) + 500) / 1000;
while(1){
DRAM_SlowRefreshTest();
_delay_ms(9000);
}*/

    while (1) 
    {
		if(CmdReceived)
		{
			CmdReceived = false;
			InterpretCommand(RxBuffer);			//Zdekoduj przes³ane polecenie
		}
    }
}
