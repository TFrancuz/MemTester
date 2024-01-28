/*
 * RWAccessh.h
 *
 * Created: 12/3/2023 12:15:00 PM
 *  Author: tmf
 */ 


#ifndef RWACCESSH_H_
#define RWACCESSH_H_
#include <stdint.h>

//Typy obs³ugiwanych pamiêci
typedef enum {IC_SRAM, IC_DRAM, IC_FLASH}
IC_Type_t;

extern IC_Type_t IC_Type;					//Typ uk³adu, który testujemy
extern uint8_t AddrNo, CASNo, DataNo;		//Parametry testowanego uk³adu
extern uint16_t CASMask;					//Maska bitowa dla adresu kolumny

//Typy funkcji dostêpu do pamiêci
typedef void (*Write_t)(uint32_t, uint8_t);		//Funkcja zapisu komórki pamiêci (adres, wartoœæ)
typedef uint8_t (*Read_t)(uint32_t);			//Funkcja odczytu komórki pamiêci (adres)
typedef uint8_t (*ReadFast_t)();				//Funkcja odczytu komórki pamiêci spod ostatniego u¿ytego adresu

extern uint16_t Mem_WriteAccessTime;	//Czas dostêpu do pamiêci przy zapisie - w tykniêciach timera
extern uint16_t Mem_ReadAccessTime;		//Czas dostêpu do pamiêci przy odczycie - w tykniêciach timera

void IO_Parallel_Init();				//Inicjalizuje piny IO dla konfiguracji pamiêci SRAM i FLASH
void IO_Bus_Off();						//Ustaw wszystkie piny magistrali jako wejœcia
void InitRDWRTimer();					//Inicjalizuje timery TCB0 i TCB1 do generowania impulsów OE i WE

void SetAddr(uint32_t addr);			//Ustawia 20-bitowy adres pamiêci

//Operacje na pamiêci SRAM
void SRAM_Write(uint32_t addr, uint8_t data);	//Zapis wskazanej komórki pamiêci
uint8_t SRAM_Read(uint32_t addr);				//Odczyt wskazanej komórki pamiêci
uint8_t SRAM_ReadWOAddr();						//Odczyt komórki pamiêci na kotórej ostatnio odbywa³a siê operacja - nie zmienia adresu

//Operacje na pamiêci FLASH
void FLASH_Write(uint32_t addr, uint8_t data);	//Zapis strony pamiêci
uint8_t FLASH_Read(uint32_t addr);				//Odczyt bajtu pamiêci FLASH

#endif /* RWACCESSH_H_ */