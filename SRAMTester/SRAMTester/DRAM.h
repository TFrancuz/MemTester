/*
 * DRAM.h
 *
 * Created: 1/15/2024 6:24:56 PM
 *  Author: tmf
 */ 


#ifndef DRAM_H_
#define DRAM_H_

#include <stdint.h>

void IO_DRAM_Init();				//Inicjalizacja pin�w IO steruj�cych DRAM
void DRAM_Init();					//Inicjalizacja pami�ci DRAM - cykle RAS
void DRAM_Timers_init();			//Inicjalizacja timer�w geneurj�cych CAS/WE/OE
void ReinitTimers();				//Reinicjalizacja powy�szych timer�w
void RefreshTimer_Enable();			//Wy��cz timer odpowiedzialny za od�wie�anie
void RefreshTimer_Disable();		//W��cz timer odpowiedzialny za od�wie�anie
void Refresh_Disable();				//Wy��cz od�wie�anie
void Refresh_Enable();				//W��cz od�wie�anie
void DRAM_Timers_init();			//Timery odpowiedzialne za sygna�y CAS i WE
void RefreshTimerInit();			//Inicjalizacja timera odpowiedzialnego za od�wie�anie
void ReinitTimers();				//Ponownie odpala timer odpowiedzialny za generowanie sekwencji CAS/WE


void DRAM_EarlyWrite(uint32_t addr, uint8_t data);	//Zapis do pami�ci DRAM pod wskazany adres wykorzystuj�c early write
uint8_t DRAM_Read(uint32_t addr);					//Odczyt z DRAM
uint8_t DRAM_ReadWOAddr();							//Odczyt z DRAM spod ostatnio u�ytego adresu

#endif /* DRAM_H_ */