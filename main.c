/* 
 * File:   main.c
 * Author: yogi
 *
 * Created on October 16, 2014, 9:07 PM
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include "injector.h"
#include <getopt.h>
#include <string.h>
#include <locale.h>

char *outputPathFile = NULL;
char *inDOSBootSector = NULL;
char *inBootLoader = NULL;
char *inStartModule = NULL;
char *inVisorModule = NULL;
FILE *of = NULL , *vf = NULL, *sf = NULL, *bf = NULL, *df = NULL;
BYTE *vBuff = NULL, *sBuff = NULL, *bBuff = NULL, *dBuff = NULL, *oBuff = NULL;
long oSize, vSize, sSize, bSize, dSize, vdSize, sdSize, bdSize, ddSize;
int clustersNeeded, firstEmptyPartClusterFATEntry, firstEmptyPartClusterAbsLoc;
sBootSect oBootDetails;
long PartDataStartAbsSector;
int totalSpaceNeeded = 0;
int partitionFirstSector;
int outputStartAbsByteNum;
long fatFirstSector;
DWORD blOffset, stOffset, viOffset, dbOffset, directWriteStartBlockNumber;
QWORD lTemp, lTemp2;
BYTE printDiskDetailsAndExit = 0;
BYTE outFileName[11];

int processArgs(int argc, char** argv)
{
    int c;
    int argCount = 0;
    int rc = 0;
    
while ((c = getopt (argc, argv, "hb:ib:d:o:s:v:l:")) != -1)
    switch (c)
      {
        case 'b': //Bootloader
            inBootLoader = optarg;
            argCount++;
            break;
        case 'd': //DOS boot sector
            inDOSBootSector = optarg;
            argCount++;
            break;
        case 'o': //Output File
          outputPathFile = optarg;
          argCount++;
          break;
        case 's':
            inStartModule = optarg;
            argCount++;
            break;
        case 'v':
            inVisorModule = optarg;
            argCount++;
            break;
        case 'l':
            directWriteStartBlockNumber = atoi(optarg);
        case 'i':
            printDiskDetailsAndExit = 1;
            break;
        case 'h':
            fprintf (stderr, "injector - inject files into a file\n");
            fprintf (stderr, "-b - bootloader path & filename\n");
            fprintf (stderr, "-d - DOS boot sector path and filename\n");
            fprintf (stderr, "-o - Output path and filename\n");
            fprintf (stderr, "-s - Startup module path & filename\n");
            fprintf (stderr, "-v - Visor module path & filename\n");
            fprintf (stderr, "-l - Override Start & Visor module location\n");
            fprintf (stderr, "-i - Print details about the disk\n");
          abort ();
        case '?':
          if (optopt == 'o')
            fprintf (stderr, "Option -%c requires an argument.\n", optopt);
          else if (isprint (optopt))
            fprintf (stderr, "Unknown option `-%c'.\n", optopt);
          else
            fprintf (stderr,
                     "Unknown option character `\\x%x'.\n",
                     optopt);
          return 1;
        default: 
            abort;
        }
    //if (!printDiskDetailsAndExit && argCount != 5)
    //    return 227;
    return rc;
}

void CloseFiles()
{
    TheEnd:
    if (bf != NULL)
        fclose(bf);
    if (sf != NULL)
        fclose(sf);
    if (vf != NULL)
        fclose(vf);
    if (df != NULL)
        fclose(df);
    if (of != NULL)
        fclose(of);
    if (bBuff != NULL)
        free (bBuff);
    if (sBuff != NULL)
        free (sBuff);
    if (vBuff != NULL)
        free (vBuff);
    if (dBuff != NULL)
        free (dBuff);
}

int OpenFiles()
{
    int retCode = 0;

    //Open the bootloader RO
    bf = fopen(inBootLoader,"r+");
    if(NULL==bf)
    {retCode = 16;goto TheEnd;}
    fseek(bf, 0, SEEK_END);
    bSize = ftell(bf);
    bdSize = ((ftell(bf) / 512) + 1) * 512;
    bBuff = malloc(bdSize);
    rewind(bf);
    fread(bBuff, bSize, 1, bf);

    //Open the Start Module RO
    sf = fopen(inStartModule,"r");
    if(NULL==sf)
    {retCode = 17;goto TheEnd;}
    fseek(sf, 0, SEEK_END);
    sSize = ftell(sf);
    sdSize = ((ftell(sf) / 512) + 1) * 512;
    sBuff = malloc(sdSize);
    rewind(sf);
    fread(sBuff, sSize, 1, sf);

    //Open the Visor Module RO
    vf = fopen(inVisorModule,"r");
    if(NULL==vf)
    {retCode = 18;goto TheEnd;}
    fseek(vf, 0, SEEK_END);
    vSize = ftell(vf);
    vdSize = ((ftell(vf) / 512) + 1) * 512;
    vBuff = malloc(vdSize);
    rewind(vf);
    fread(vBuff, vSize, 1, vf);
    
    //Open the output file RW
    of = fopen(outputPathFile,"r+");
    if(NULL==of)
    {retCode = 19;goto TheEnd;}
    fseek(of, 0, SEEK_END);
    oSize = ftell(of);
    rewind(of);

    //Open the DOS Boot Sector file RO
    if (inDOSBootSector != NULL)
    {
        df = fopen(inDOSBootSector,"r");
        if(NULL==df)
        {retCode = 20;goto TheEnd;}
        fseek(df, 0, SEEK_END);
        dSize = ftell(df);
        ddSize = ((ftell(df) / 512) + 1) * 512;
        dBuff = malloc(ddSize);
        rewind(df);
        fread(dBuff, dSize, 1, df);
    }

TheEnd:
    return retCode;
}

void ReadPartitionBootSector()
{
    //Find the beginning of partition 1
    fseek(of, PARTITION_1_LBA_FIRST_SECTOR, SEEK_SET);
    fread((void *)&partitionFirstSector, sizeof(int), 1, of);
    //Get the boot sector
    fseek(of, partitionFirstSector * 512, SEEK_SET);
    fread((void *)&oBootDetails, sizeof(sBootSect), 1, of);
    //Data_Area_Start = Reserved_Sectors + FAT_Sectors + Root_Sectors
    PartDataStartAbsSector = partitionFirstSector + oBootDetails.resSectors + 
            (oBootDetails.sectPerFat * oBootDetails.fatCnt) + (32);
}

long FindEmptyDiskClusters()
{
    int fatEntry;
    int cnt, srchCnt;
    WORD words[100];
    int foundCount = 0, foundTimes = 0;
    
    fseek(of, fatFirstSector * 512, SEEK_SET);
    memset(words,0,100);
    for (cnt = 0;cnt < (oBootDetails.sectPerFat * 512); cnt+=2)
    {
        fseek(of, (fatFirstSector * 512) + cnt/*2 bytes per entry*/ + 2 /*First entry F8FF*/, SEEK_SET);
        fread((void *)words, clustersNeeded*2, 1, of);
        foundCount = 0;
        for (srchCnt=0;srchCnt<clustersNeeded;srchCnt++)
            if (words[srchCnt] == 0)
                foundCount++;
            else
                break;
        if (foundCount == clustersNeeded)
        {
            if (foundTimes++<=0x500)
                continue;
            //Mark the clusters used!!!
            //Hokey but it will work ... need X copies of 0xFFF7 to write to the file
            lTemp = 0xfff7fff7fff7fff7;
            lTemp2 = lTemp;
            fseek(of, (fatFirstSector * 512) + cnt/*2 bytes per entry*/ + 2 /*First entry F8FF*/, SEEK_SET);
            fwrite((void *)&lTemp, 1, clustersNeeded*2, of);
            fflush(of);
            return cnt;
        }
    }
    //If we made it here, no free clusters exist
    printf("Not enough sequential free fat entries exist, cannot continue.\nfs");
    return 0xFFFFFFFF;
}

long CalculateOffsets(long startingAddress)
{
    blOffset = 0;
    stOffset = startingAddress;
    viOffset = stOffset + sdSize;
    dbOffset = viOffset +vdSize;
    return dbOffset + ddSize;
}

int InjectStuff(BYTE *buff, int size, DWORD location)
{
    fseek(of, location, SEEK_SET);
    fwrite((void *)buff, size, 1, of);
    return 0;
}

void PatchBootSector()
{
    stOffset /= 512;
    fseek(of, BOOTLOADER_START_BIN_LBA_OFFSET, SEEK_SET);
    printf("Patching output MBR at 0x%x with value 0x%x (Start module abs sector)\n", BOOTLOADER_START_BIN_LBA_OFFSET, stOffset);
    fwrite((void *)&stOffset, 4, 1, of);
    stOffset *= 512;
    viOffset /= 512;
    fseek(of, BOOTLOADER_VISOR_BIN_LBA_OFFSET, SEEK_SET);
    printf("Patching output MBR at 0x%x with value 0x%x (Visor abs sector)\n", BOOTLOADER_VISOR_BIN_LBA_OFFSET, viOffset);
    fwrite((void *)&viOffset, 4, 1, of);
    viOffset *= 512;
    dbOffset /= 512;
    fseek(of,BOOTLOADER_DOS_MBR_LBA_OFFSET, SEEK_SET);
    printf("Patching output MBR at 0x%x with value 0x%x (DOS MBR abs sector)\n", BOOTLOADER_DOS_MBR_LBA_OFFSET, dbOffset);
    fwrite((void *)&dbOffset, 4, 1, of);
    dbOffset *= 512;
    
}

int FindRootDirAbsLocation()
{
    int rootDirSector = partitionFirstSector + oBootDetails.resSectors + (oBootDetails.sectPerFat * oBootDetails.fatCnt);
    sDirEntry dir;
    int entryCount = 0;
    
    fseek(of, rootDirSector * 512, SEEK_SET);
    do
    {
        fread((void *)&dir, sizeof(dir), 1, of);
    } while (dir.Filename[0] != 0 && ++entryCount < 128);

    if (entryCount == 127)
    {
        printf("Cannot find a blank root directory entry.  Changes have been committed, but no directory entries will be created");
        return -1;
    }
    return (rootDirSector * 512) + ( (entryCount) * (sizeof(sDirEntry)));
}

int rdtsc()    
{    
    __asm__ __volatile__("rdtsc");    
}   

void UpdateRootDirectory()
{
    int cnt, dirEntryAbsLocaction, result;
    sDirEntry dirEntry;
    srand (rdtsc());
   
    for (cnt=0;cnt<12;cnt++)
    {
        result = (rand()%26) + 65;
        outFileName[cnt] = result;
    }
    outFileName[11] = '\0';
    strcpy(dirEntry.Filename,outFileName);
    dirEntry.Filesize = clustersNeeded * oBootDetails.sectPerCluster * 512;
    dirEntry.Attrib_A = 0;
    dirEntry.Attrib_H = 1;
    dirEntry.Attrib_RO = 1;
    dirEntry.Attrib_S = 1;
    dirEntry.Date = (34 << 9) | (10 << 5) | 19;
    dirEntry.Time = 0x32C0;
    dirEntry.StartingCluster =  (firstEmptyPartClusterFATEntry);
    dirEntryAbsLocaction = FindRootDirAbsLocation();
    fseek(of, dirEntryAbsLocaction, SEEK_SET);
    fwrite((void *)&dirEntry, sizeof(sDirEntry), 1, of);
    for (cnt=0; cnt < clustersNeeded; cnt++)
    {
        fseek(of, firstEmptyPartClusterAbsLoc + (cnt * 2), SEEK_SET);
        WORD wTemp = (firstEmptyPartClusterFATEntry) + cnt + 1;
        if (cnt == clustersNeeded - 1)
            wTemp = 0xFFFF;
        fwrite((void *)&wTemp, 2, 1, of);
        fseek(of, (oBootDetails.sectPerFat * 512) - 2, SEEK_CUR);
        fwrite((void *)&wTemp, 2, 1, of);
    }
    //fflush(of);
}

void PrintDiskDetails()
{
    printf("Bytes per Sector:\t%d (0x%x)\nSectors per Cluster:\t%d (0x%x)\nReserved Sectors:\t%d (0x%x)\n",oBootDetails.bps, oBootDetails.bps, oBootDetails.sectPerCluster, oBootDetails.sectPerCluster, oBootDetails.resSectors, oBootDetails.resSectors);
    printf("Number of FATs:\t\t%d (0x%x)\nRoot Dir Entries:\t%d (0x%x)\nTotal Sectors (small):\t\%d (0x%x)\n", oBootDetails.fatCnt, oBootDetails.fatCnt, oBootDetails.rootEntries, oBootDetails.rootEntries, oBootDetails.smallSectors, oBootDetails.smallSectors);
    printf("Media:\t\t\t%d (0x%x)\nSectors per FAT:\t%d (0x%x)\nSectors per Track:\t%d (0x%x)\nHeads per Cyl:\t\t%d (0x%x)\n", oBootDetails.mediaDesc, oBootDetails.mediaDesc, oBootDetails.sectPerFat, oBootDetails.sectPerFat, oBootDetails.sectPerTrack, oBootDetails.sectPerTrack, oBootDetails.heads, oBootDetails.heads);
    printf("Hidden Sectors:\t\t%d (0x%x)\nTotal Sectors (big):\t%d (0x%x)\n", oBootDetails.hiddenSect, oBootDetails.hiddenSect, oBootDetails.largeSectors, oBootDetails.largeSectors);
    printf("FAT First Abs Sector:\t%ld (0x%x)\nBegin FAT Byte\t\t%ld (0x%x)\nFirst Data Sector:\t%ld (0x%x) - Cluster 2\nBegin Data Byte:\t%ld (0x%x)\n", fatFirstSector, fatFirstSector, fatFirstSector * 512, fatFirstSector * 512, PartDataStartAbsSector, PartDataStartAbsSector, PartDataStartAbsSector * 512, PartDataStartAbsSector * 512);
}

/*
 * 
 */
int main(int argc, char** argv) 
{
    int lTemp;
    int firstEmptyClusterNumber;
    int cnt;
    char errorMessage[255];
    
    memset(errorMessage, 0x0, 255);
    //Set locale to support the ' in printf formatting strings (commas)
    setlocale(LC_ALL,"");
    if (lTemp = processArgs(argc, argv))
        return (lTemp);
    
    if (inStartModule == NULL)
        strcpy(errorMessage, "Start Module parameter not specified, cannot continue.\n");
    else if (directWriteStartBlockNumber == 0 && inDOSBootSector == NULL)
        strcpy(errorMessage, "DOS Boot Sector parameter not specified, cannot continue.\n");
    else if (inBootLoader == NULL)
        strcpy(errorMessage, "Boot Loader parameter not specified, cannot continue.\n");
    else if (inVisorModule == NULL)
        strcpy(errorMessage, "Visor Module parameter not specified, cannot continue.\n");
    else if (outputPathFile == NULL)
        strcpy(errorMessage, "Output file parameter not specified, cannot continue.\n");
    
    if (errorMessage[0] != '\0')
    {
        printf("%s",errorMessage);
        return -3;
    }
    
    printf("Opening files ...\n");
    OpenFiles();

    ReadPartitionBootSector();

    CalculateOffsets(0);
   
    //User passed a disk physical address to write at.  We will write everyting to this addres except for the boot sector
    //Also we will grab the current boot sector and write it at the DPA
    if (directWriteStartBlockNumber != 0)
    {
        stOffset += directWriteStartBlockNumber;
        printf("Injecting Start module (0x%x/0x%x bytes) at %d (0x%x)\n", (WORD)sdSize, (WORD)sdSize, stOffset, stOffset);
        InjectStuff(sBuff, sdSize, stOffset);
        viOffset += directWriteStartBlockNumber;
        printf("Injecting Hypervisor (0x%x/0x%x bytes) at %d (0x%x)\n", (WORD)vdSize, (WORD)vdSize, viOffset, viOffset);
        InjectStuff(vBuff, vdSize, viOffset);
        dSize = 512;
        ddSize = 512;
        dBuff = malloc(ddSize);
        fseek(of, 0, SEEK_SET);
        fread(dBuff, 512, 1, of);
        dbOffset += directWriteStartBlockNumber;
        printf("Moving original boot sector from sector 0 to %d (0x%x)\n",dbOffset, dbOffset);
        InjectStuff(dBuff, 512, dbOffset);
        printf("Injecting Bootloader (0x%x  bytes)at %d (0x%x)\n", (WORD)bSize, blOffset, blOffset);
        InjectStuff(bBuff, bSize, blOffset);
        PatchBootSector();
        CloseFiles();
        return 0;
    }
    
    totalSpaceNeeded = ((CalculateOffsets(0) / (oBootDetails.sectPerCluster * 512)) + 1) * (oBootDetails.sectPerCluster * 512);
    clustersNeeded = totalSpaceNeeded / 512 / oBootDetails.sectPerCluster;

    fatFirstSector = partitionFirstSector + oBootDetails.resSectors;

    if (printDiskDetailsAndExit)
    {
        PrintDiskDetails();
    }
    else
    {
        printf("Reading boot sector for partition 1 ...\n");
        //Calculate offsets from zero to get total size needed
        printf("Payload (Boot + Start + Visor + DOS Boot) = %'d bytes. (each payload is rounded to sector size)\n", totalSpaceNeeded);
        printf("Scanning output FAT for sequential free sectors to place payload at ...\n");
        firstEmptyPartClusterFATEntry = FindEmptyDiskClusters();
        firstEmptyClusterNumber = firstEmptyPartClusterFATEntry / 2;
        firstEmptyPartClusterFATEntry = (firstEmptyPartClusterFATEntry / 2) + 1;
        if (firstEmptyPartClusterFATEntry == -1)
            return -3;

        int absSector = (int)(PartDataStartAbsSector + ((firstEmptyClusterNumber - 1) * oBootDetails.sectPerCluster));
        outputStartAbsByteNum = absSector * 512;
        firstEmptyPartClusterAbsLoc = (firstEmptyPartClusterFATEntry) * 2 + (fatFirstSector * 512);
        printf("Found %'d bytes at FAT entry %'d (BOD + 0x%x), cluster=%'d, abs sector=%'d, which starts at BOD offset %'d (0x%X).\n", 
                totalSpaceNeeded, firstEmptyPartClusterFATEntry, firstEmptyPartClusterAbsLoc, 
                firstEmptyPartClusterFATEntry-2, absSector, outputStartAbsByteNum, outputStartAbsByteNum);
        CalculateOffsets(outputStartAbsByteNum);

        printf("Injecting Bootloader (0x%x  bytes)at %d (0x%x)\n", (WORD)bSize, blOffset, blOffset);
        InjectStuff(bBuff, bSize, blOffset);

        printf("Injecting Start module (0x%x/0x%x bytes) at %d (0x%x)\n", (WORD)sSize, (WORD)sdSize, stOffset, stOffset);
        InjectStuff(sBuff, sdSize, stOffset);

        printf("Injecting Hypervisor (0x%x/0x%x bytes) at %d (0x%x)\n", (WORD)vSize, (WORD)vdSize, viOffset, viOffset);
        InjectStuff(vBuff, vdSize, viOffset);

        printf("Injecting DOS Boot sector (0x%x/0x%x bytes) at %d (0x%x)\n", (WORD)dSize, (WORD)ddSize, dbOffset, dbOffset);
        InjectStuff(dBuff, ddSize, dbOffset);

        PatchBootSector();

        UpdateRootDirectory();
        printf("Wrote root directory entry for file %s",outFileName);
    }
    CloseFiles();
    return 0;
}
