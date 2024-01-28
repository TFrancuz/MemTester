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

//Domy�lne parametry testowanego uk�adu
IC_Type_t IC_Type = IC_SRAM;				//Typ uk�adu, kt�ry testujemy
uint8_t AddrNo = 16, CASNo = 8, DataNo = 8;	//Parametry testowanego uk�adu
uint16_t CASMask = 255;						//Maska bitowa dla adresu kolumny

//Wska�niki na funkcje dost�u do pami�ci
Write_t MemWriteFunc;			//Wska�nik na funkcj� zapisu kom�rki pami�ci (adres, warto��)
Read_t MemReadFunc;				//Wska��ik na funkcj� odczytu kom�rki pami�ci (adres)
ReadFast_t MemFastReadFunc;		//Wska�nik na funkcj� odczytu kom�rki pami�ci spod ostatniego u�ytego adresu

void IO_DRAM_Init();						//Inicjalizuje piny IO wykorzystywane do komunikacji z DRAM
void DRAM_Init();							//Inicjalizuje pami�� DRAM - seria impuls�w na RAS

//Makro umieszczaj�ce zadany �a�cuch w przestrzeni adresowej __flash
#define PGM_STR(X) ((const char[]) { X })

const char msg_UnknownCmd[];
const char msg_OK[] = PGM_STR("OK\r\n");
const char msg_UnknownCmd[] = PGM_STR(" - unknown command\r\n");		//Nie rozpoznane polecenie przes�ane z PC
const char msg_Err[] = PGM_STR("Err\r\n");
const char msg_InvalidArgument[] = PGM_STR("Invalid argument\r\n");
const char mgs_OutOfRange[] = PGM_STR("Argument out of range\r\n");

typedef _Bool (*CmdHandler)(char *param, char **last);

typedef struct
{
	const char *cmd;                //Wska�nik do polecenia w formacie ASCIIZ
	const CmdHandler  Handler;      //Funkcja realizuj�ca polecenie
} Command;

_Bool Cmd_A(char *param, char **last);			//Ustaw liczb� linii adresowych
_Bool Cmd_D(char *param, char **last);			//Ustaw liczb� linii danych
_Bool Cmd_CAS(char *param, char **last);		//Ustaw liczb� linii adresowych dla sygna�u RAS
_Bool Cmd_SRAM(char *param, char **last);		//Ustaw typ pami�ci SRAM
_Bool Cmd_DRAM(char *param, char **last);		//Ustaw typ pami�ci DRAM
_Bool StartMemTesting(char *param, char **last);	//Test pami�ci
_Bool MemAccessTime(char *param, char **last);	//Czas dost�pu do pami�ci
_Bool DRAM_CBRTest(char *param, char **last);	//Test od�wie�ania CBR

//Funkcja pobiera jeden argument z ci�gu i zwraca go jako uint8_t, zwraca true je�li ok, false, je�li konwersja si� nie powiod�a
_Bool GetUInt8Argument(char *param, char **last, uint8_t *val)
{
	char* end;
	param=strtok_r(*last, " ,", last);	//Wyszukaj ci�g rozdzielaj�cy - pobieramy offset na stronie
	if(param == NULL) return false;     //B��d

	*val = strtol(param, &end, 0);		//Pobierz offset na programowanej stronie
	if(*end) return false;	//B��d
	return true;
}

_Bool GetUInt16Argument(char *param, char **last, uint16_t *val)
{
	char* end;
	param=strtok_r(*last, " ,", last);	//Wyszukaj ci�g rozdzielaj�cy - pobieramy offset na stronie
	if(param == NULL) return false;     //B��d

	*val = strtol(param, &end, 0);		//Pobierz offset na programowanej stronie
	if(*end) return false;	//B��d
	return true;
}

_Bool GetUInt32Argument(char *param, char **last, uint32_t *val)
{
	char* end;
	param=strtok_r(*last, " ,", last);	//Wyszukaj ci�g rozdzielaj�cy - pobieramy offset na stronie
	if(param == NULL) return false;     //B��d

	*val = strtol(param, &end, 0);		//Pobierz offset na programowanej stronie
	if(*end) return false;	//B��d
	return true;
}

_Bool Cmd_A(char *param, char **last)			//Ustaw liczb� linii adresowych
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

_Bool Cmd_D(char *param, char **last)			//Ustaw liczb� linii danych
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

_Bool Cmd_CAS(char *param, char **last)			//Ustaw liczb� linii adresowych dla sygna�u RAS
{
	if(GetUInt8Argument(param, last, &CASNo) == false)
	{
		USART_SendText(msg_InvalidArgument);
		return false;
	}
	if((CASNo > 9) || (CASNo > (AddrNo >> 1)))	//Linii /CAS nie mo�e by� wi�cej ni� po�owa linii adresowych
	{
		USART_SendText(mgs_OutOfRange);
		return false;
	}
	CASMask = 0x01;
	for(uint8_t i = 1; i < CASNo; i++) CASMask = (CASMask << 1) | 0x01;	//Maska zale�na od liczby linii CAS
	return true;
}	

_Bool Cmd_SRAM(char *param, char **last)		//Ustaw typ pami�ci SRAM
{
	IC_Type = IC_SRAM;
	MemWriteFunc = SRAM_Write;				//Funkcje dost�pu do pami�ci SRAM
	MemReadFunc = SRAM_Read;
	MemFastReadFunc = SRAM_ReadWOAddr;
	return true;
}

_Bool Cmd_DRAM(char *param, char **last)		//Ustaw typ pami�ci DRAM
{
	IC_Type = IC_DRAM;
	MemWriteFunc = DRAM_EarlyWrite;				//Funkcje dost�pu do pami�ci DRAM
	MemReadFunc = DRAM_Read;
	MemFastReadFunc = DRAM_ReadWOAddr;
	return true;
}

_Bool Cmd_FLASH(char *param, char **last)		//Ustaw typ pami�ci FLASH
{
	Cmd_SRAM(param, last);						//FLASH inicjujemy tak jak SRAM
	IC_Type = IC_FLASH;							//Tylko oznaczamy jako FLASH :)
	return true;
}

_Bool DRAM_CBRTest(char *param, char **last)	//Test od�wie�ania CBR
{
	uint16_t ChkAndChange(uint8_t val)	//Sprawd� czy wybrane bity w ka�dym rz�dzie maj� warto�� val i zmie� ich stan na przeciwny
	{									//zwraca liczb� rz�d�w z poprawnymi bitami
		uint16_t cnt = 0;				//Liczba odczytanych rz�d�w
		PORTC_OUT = val ^ 0xFF;			//B�dziemy zapisywa� inwersj� val
		val &= 0x01;					//Tylko D0 si� liczy
		
		for(uint16_t RAS = 0; RAS <= CASMask; RAS++)
		{
			//Sekwencja CAS before RAS - aktywacja od�wie�ania
			TCB1.CTRLB = TCB_CNTMODE_SINGLE_gc;		//Od��czamy TCB1 od sygna�u CAS, co powoduje jego przej�cie w stan niski
			PORTE_OUTCLR = DMEM_RAS;				//Aktywuj RAS
			TCB1.CTRLB = TCB_CCMPEN_bm | TCB_CNTMODE_SINGLE_gc;	//Aktywuj sterowanie CAS przez timer, co powoduje jego przej�cie w stan wysoki

			//Sekwencja odczytu pami�ci z rz�du okre�lanego przez wewn�trzny licznik od�wie�ania RAS
			TCB1_INTFLAGS = TCB_CAPT_bm;			//Skasuj flag� capt
			EVSYS_SWEVENTA = EVSYS_SWEVENTA_CH1_gc;	//Odmierzamy czas dla strobu CAS
			PORTF_OUTCLR = DMEM_OE;
			while(!(TCB1_INTFLAGS & TCB_CAPT_bm));	//Czekamy na koniec CAS
			uint8_t tmpdata = PORTC_IN & 0x01;		//Interesuje nas wy��cznie bit D0
			PORTF_OUTSET = DMEM_OE;					//Deaktywuj OE/RD
			if(tmpdata == val) cnt++;				//Odczytali�my kolejny rz�d prawid�owo
		
			//Sekwencja zapisu do DRAM warto�ci val w kolumnie okre�lonej przez wewn�trzny licznik RAS
			PORTC_DIR = 0xFF;				//D0-D7 - wyj�cia
			TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;	//Kasujemy flag� OVF
			ReinitTimers();		//Potrzebne dla reinicjalizacji timera wyznaczaj�cego timingi
			TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc | TCA_SINGLE_ENABLE_bm;	//Aktywuj sekwencj� WR/CAS
			while(!(TCA0.SINGLE.INTFLAGS & TCA_SINGLE_OVF_bm));
			PORTE_OUTSET = DMEM_RAS;		//Deaktywuj RAS
			PORTC_DIR = 0x00;				//D0-D7 - wej�cia
		}
		return cnt;		//Ile odczytali�my poprawnie rz�d�w?
	}
	
	if(IC_Type != IC_DRAM)
	{
		USART_SendText("CBR test is only for DRAM\r\n");
		return false;
	}

	IO_DRAM_Init();				//Inicjalizacja pin�w IO zwi�zanych z DRAM
	DRAM_Timers_init();			//Inicjalizacja timer�w generuj�cych CAS i WE
	TCA0.SINGLE.CMP0 = 7;		//Poniewa� nie korzystamy z early write, zmieniamy po�o�enie impuls�w CAS/WE
	TCA0.SINGLE.CMP1 = 9;		//WE jest op�niony wzgl�dem CAS

	DRAM_Init();				//Inicjalizacja pami�ci DRAM

	PORTF_OUTSET = DMEM_CAS;	//Aktywuj CAS - na pinie jest w��czona inwersja sygna�u,
								//Normalnie pinem steruje timer TCB1
								
	ChkAndChange(1);			//Wype�nij wybrane kom�rki w ka�dym rz�dzie zerami
	uint16_t rows1 = ChkAndChange(0);	//Sprawd� czy s� tam jedynki?
	uint16_t rows2 = ChkAndChange(1);	//Sprawd� czy s� tam jedynki?
	
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
	uint16_t AccessTime;		//Wskazany czas dost�u do pami�ci

	if(GettAccessTimeVal(param, last, &AccessTime) == true)
	{
		Mem_WriteAccessTime = (AccessTime * (F_CPU / 1000000) + 500) / 1000;	//Oblicz op�nienie dla timera
		char *tmplast = *last;		//Wymczasowo przechowujemy warto�� na kt�r� wskazuje wska�nik
		if(GettAccessTimeVal(param, last, &AccessTime) == false)
		{
			*last = tmplast;		//W przypadku b��du odtwarzamy poprzedni� warto��	
		}
		Mem_ReadAccessTime = (AccessTime * (F_CPU / 1000000) + 500) / 1000;		//Access time dla odczytu
		sprintf(tbl, "Access time - write: %i ns, read: %i ns.\r\n", (uint16_t)((Mem_WriteAccessTime * 1000)/(F_CPU/1000000UL)),
																	 (uint16_t)((Mem_ReadAccessTime * 1000)/(F_CPU/1000000UL)));
		USART_SendText(tbl);		//Informujemy u�ytkownika o rzeczywistym, ustawionym czasie dost�pu
		return true;
	}
	USART_SendText(msg_InvalidArgument);	//Wyst�pi� b��d w argumencie
	return false;
}

//Lista rozpoznawanych polece�
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
		if(cmd != NULL) 		//Je�li znaleziono poprawny format polecenia, to spr�bujmy je wykona�
		{
			for(indeks = 0; indeks < max_indeks; indeks++)
			{
				if(strcmp(cmd, Polecenia[indeks].cmd) == 0) //Przeszukaj list� polece�
				{
					retVal = Polecenia[indeks].Handler(last, &last);   //Wywo�aj funkcj� obs�ugi przes�anego polecenia
					break;
				}
			}
			if(indeks == max_indeks)  //Je�li polecenie nieznane lub b��d...
			{
				USART_SendText(cmd);			//Wy�lij polecenie, kt�re spowodowa�o b��d
				USART_SendText(msg_UnknownCmd); //B��d - nieznane polecenie
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
		for(uint8_t i = 1; i < DataNo; i++) bitmask = (bitmask << 1) | 0x01;	//Maska zale�na od szeroko�ci magistrali danych
	
		uint8_t datamask = 0x01;
		for(uint8_t i = 1; i <= DataNo; i++)
		{
			MemWriteFunc(0x0000, datamask);	//Zapisujemy bie��c� mask�
			//dr = SRAM_Read(0x0000);
			dr = MemFastReadFunc();
			if((dr & bitmask) != (datamask & bitmask))
			{
				ret = false;
				break;
			}
			datamask ^= 0xff;
			MemWriteFunc(0x0000, datamask);	//Zapisujemy bie��c� inwersj� maski
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
		for(uint8_t i = 1; i < DataNo; i++) datamask = (datamask << 1) | 0x01;	//Maska zale�na od szeroko�ci magistrali danych
		
		USART_SendText("Address bus test...");
		
		for(uint8_t i = 1; i <= AddrNo; i++)		//Zerujemy wszystkie kom�rki le��ce na granicy kolejnych linii adresowych
		{
			MemWriteFunc(addrmask, 0x00);
			addrmask <<= 1; addrmask |= 0x01;
		}
		
		addrmask = 0x01;
		for(uint8_t i = 1; i <= AddrNo; i++)	//Skanujemy po kolei ka�d� lini� adresow�
		{
			if(MemReadFunc(addrmask) != 0x00)	//Normalnie ka�da lokacja powinna zawiera� 0x00
			{
				ret = false;
				break;
			}
			
			MemWriteFunc(addrmask, 0xff);	//Tymczasowo zapisujemy 0xff
			uint32_t tmpaddrmask = 0x01;
			for(uint8_t h = 1; h <= AddrNo; h++)	//Skanujemy po kolei ka�d� lini� adresow�
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
			MemWriteFunc(addrmask, 0x00);	//Na granicy pot�gi 2 powinno by� 0x00 - odtwarzamy poprzedni� warto��
			if(ret == false) break;			//B��d - dalej nie sprawdzamy
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
		for(uint8_t i = 1; i < DataNo; i++) datamask = (datamask << 1) | 0x01;	//Maska zale�na od szeroko�ci magistrali danych
		
		sprintf(txtbuf, "Data test with pattern 0b%c%c%c%c%c%c%c%c - ", BYTE_TO_BINARY(pattern));
		USART_SendText(txtbuf);
		
		for(addr = 0; addr < maxaddr; addr++)
		{
			MemWriteFunc(addr, pattern);
			datard = MemFastReadFunc() & datamask;
			if(datard != (pattern & datamask))
			{
				ret = false;		//Niezgodno�� pomi�dzy zapisem i odczytem
				break;
			}
			MemWriteFunc(addr, pattern ^ 0xff); //Negacja maski
			datard = MemFastReadFunc() & datamask;
			if(datard != ((pattern ^ 0xFF) & datamask))
			{
				ret = false;		//Niezgodno�� pomi�dzy zapisem i odczytem
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
	
	switch(IC_Type)		//Inicujemy podsystemy zale�nie od typu wybranej pami�ci
	{
		case IC_FLASH:	USART_SendText("Operation not supported for FLASH memory");		//T� funkcj� nie testujemy pami�ci FLASH
						return false;
		case IC_SRAM:
						IO_Parallel_Init();			//Zainoicjuj piny IO dla bie��cej konfiguracji SRAM
						InitRDWRTimer();			//Timer generuj�cy WE i OE
						PORTF_OUTCLR = MEM_CS;		//Aktywuj CS
						break;
		case IC_DRAM:	
						IO_DRAM_Init();				//Inicjalizacja pin�w IO zwi�zanych z DRAM
						DRAM_Timers_init();			//Inicjalizacja timer�w generuj�cych CAS i WE
						RefreshTimerInit();			//Inicjalizacja timera odpowiedzialnego za od�wie�anie
						DRAM_Init();				//Inicjalizacja pami�ci DRAM
						RefreshTimer_Enable();		//W��cz od�wie�anie
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
	USART_SendText(txtbuf);		//Informujemy u�ytkownika o rzeczywistym, ustawionym czasie dost�pu
	
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

	switch(IC_Type)		//Wy��czamy podsystemy zale�nie od typu wybranej pami�ci
	{
		case IC_FLASH:
		case IC_SRAM:
						PORTF_OUTSET = MEM_CS;		//Deaktywuj CS
						break;
		case IC_DRAM:	RefreshTimer_Disable();		//Wy��cz od�wie�anie
						break;
	}	
	
	IO_Bus_Off();
	
	return ret;
}

void DRAM_SlowRefreshTest()			//Sprawd� jak pami�� si� zachowuje z wolnym od�wie�aniem
{
	IO_DRAM_Init();				//Inicjalizacja pin�w IO zwi�zanych z DRAM
	DRAM_Timers_init();			//Inicjalizacja timer�w generuj�cych CAS i WE
	RefreshTimerInit();			//Inicjalizacja timera odpowiedzialnego za od�wie�anie
	DRAM_Init();				//Inicjalizacja pami�ci DRAM
	RefreshTimer_Enable();		//W��cz od�wie�anie
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
	for(uint8_t i = 1; i < DataNo; i++) datamask = (datamask << 1) | 0x01;	//Maska zale�na od szeroko�ci magistrali danych
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
	//Domy�lnie MCU startuje z wew. zegarem 4 MHz
	CPU_CCP = CCP_IOREG_gc;							//Odblokuj dost�p do rejestru chronionego
	CLKCTRL.OSCHFCTRLA = CLKCTRL_FRQSEL_24M_gc;		//Zegar 24 MHz
    
	UART_Init();				//Inicjalizacja USART
	sei();						//Odblokowjemy przerwania
	
	PORTB_DIRSET = PIN3_bm;		//Pin steruj�cy LEDem
	Cmd_SRAM(NULL, NULL);		//Domy�lnie obs�ugujemy SRAM
	
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
			InterpretCommand(RxBuffer);			//Zdekoduj przes�ane polecenie
		}
    }
}
