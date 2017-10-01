/* 
 * File:   injector.h
 * Author: yogi
 *
 * Created on October 16, 2014, 10:19 PM
 */

#ifndef INJECTOR_H
#define	INJECTOR_H
#define PARTITION_1_LBA_FIRST_SECTOR 0x1c6
#define TOTAL_OUTPUT_SPACE_NEEDED 65536
#define BOOTLOADER_START_BIN_LBA_OFFSET 0xb2
#define BOOTLOADER_VISOR_BIN_LBA_OFFSET 0xe8
#define BOOTLOADER_DOS_MBR_LBA_OFFSET 0x11e
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef unsigned long long QWORD;

typedef struct s_bootSect
{
    DWORD junk, junk2;  //0-7
    BYTE junkB[3];      //8-a
    WORD bps;           //b-c
    BYTE sectPerCluster; //d
    WORD resSectors;    //e-f
    BYTE fatCnt;        //10
    WORD rootEntries;   //11-12
    WORD smallSectors;  //13-14
    BYTE mediaDesc;     //15
    WORD sectPerFat;    //16-17
    WORD sectPerTrack;  //18-19
    WORD heads;         //1a
    DWORD hiddenSect;   //1c-1f
    DWORD largeSectors; //20-23
    BYTE junk4[0x1cc];  //24-200
} __attribute__((packed)) sBootSect;

typedef struct s_dirEntry
{
    BYTE Filename[11];  //0-10
    BYTE Attrib_RO : 1; //11
    BYTE Attrib_H  : 1; //11
    BYTE Attrib_S  : 1; //11
    BYTE Attrib_VL : 1; //11
    BYTE Attrib_SU : 1; //11
    BYTE Attrib_A  : 1; //11
    BYTE Attrib_XX : 2; //11
    BYTE Reserved[10];  //12-21
    WORD Time;          //22-23
    WORD Date;          //24-25
    WORD StartingCluster; //26-27
    DWORD Filesize;     //28-31
} sDirEntry;

#endif	/* INJECTOR_H */

