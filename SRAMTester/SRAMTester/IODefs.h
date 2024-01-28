/*
 * IODefs.h
 *
 * Created: 12/3/2023 12:19:25 PM
 *  Author: tmf
 */ 


#ifndef IODEFS_H_
#define IODEFS_H_

//PC0-PC7 - linie danych D0-D7
//PE0-PE1 - linie adresowe A0-A1
//PA2-PA7 - linie adresowe A2-A7
//PD0-PD7 - linie adresowe A8-A15
//PB4-PB5 - linie adresowe A16-A17
//PF2 - linia adresowa A18
//PF4 -	WE
//PF5 - OE
//PF3 - CS
//Definicje dla pamiêci statycznych
#define MEM_CS		PIN3_bm
#define MEM_WE		PIN4_bm
#define MEM_OE		PIN5_bm
//Definicje dla pamiêci dynamicznych - PORTF
#define DMEM_WE		PIN4_bm
#define DMEM_OE		PIN3_bm
#define DMEM_CAS	PIN5_bm
//RAS PORTE3
#define DMEM_RAS	PIN3_bm

//PORTB
#define BOARD_LED	PIN3_bm		//LED na p³ytce
#define BOARD_SW	PIN2_bm		//Przycisk na p³ytce



#endif /* IODEFS_H_ */