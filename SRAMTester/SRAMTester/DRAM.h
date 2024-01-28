/*
 * DRAM.h
 *
 * Created: 1/15/2024 6:24:56 PM
 *  Author: tmf
 */ 


#ifndef DRAM_H_
#define DRAM_H_

#include <stdint.h>

void IO_DRAM_Init();				//Inicjalizacja pinów IO steruj¹cych DRAM
void DRAM_Init();					//Inicjalizacja pamiêci DRAM - cykle RAS
void DRAM_Timers_init();			//Inicjalizacja timerów geneurj¹cych CAS/WE/OE
void ReinitTimers();				//Reinicjalizacja powy¿szych timerów
void RefreshTimer_Enable();			//Wy³¹cz timer odpowiedzialny za odœwie¿anie
void RefreshTimer_Disable();		//W³¹cz timer odpowiedzialny za odœwie¿anie
void Refresh_Disable();				//Wy³¹cz odœwie¿anie
void Refresh_Enable();				//W³¹cz odœwie¿anie
void DRAM_Timers_init();			//Timery odpowiedzialne za sygna³y CAS i WE
void RefreshTimerInit();			//Inicjalizacja timera odpowiedzialnego za odœwie¿anie
void ReinitTimers();				//Ponownie odpala timer odpowiedzialny za generowanie sekwencji CAS/WE


void DRAM_EarlyWrite(uint32_t addr, uint8_t data);	//Zapis do pamiêci DRAM pod wskazany adres wykorzystuj¹c early write
uint8_t DRAM_Read(uint32_t addr);					//Odczyt z DRAM
uint8_t DRAM_ReadWOAddr();							//Odczyt z DRAM spod ostatnio u¿ytego adresu

#endif /* DRAM_H_ */