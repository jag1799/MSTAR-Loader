#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Define MSTAR image type */
#define CHIP_IMAGE   0
#define FSCENE_IMAGE 1

#define ALL_DATA   0
#define MAG_DATA   1
#define HDR_DATA   2

#define SUCCESS    0
#define FAILURE   -1

#define LSB_FIRST    0             /* Implies little-endian CPU... */
#define MSB_FIRST    1             /* Implies big-endian CPU...    */

// Can store any value in the Phoenix header
union Postfix
{
  char string[30];
  int integer;
  float decimal;
};

// Store a line from the metadata
struct MetaLine
{
  // All Phx header metadata prefixes (before the '=' sign) are strings
  char prefix[30];
  union Postfix postfix;
};

/* Function Declarations */
static float          byteswap_SR_IR();
static unsigned short byteswap_SUS_IUS();
static int            CheckByteOrder();
void parseHeaderLine(struct MetaLine *metadata, char* buffer);

int main(argc, argv)

  int   argc;
  char *argv[];

{
    /************************* D E C L A R A T I O N S *************************/

    FILE *MSTARfp=NULL;        /* Input FILE ptr to MSTAR image file     */
    FILE *RAWfp=NULL;          /* Output FILE ptr to MSTAR RAW data file */
    FILE *HDRfp=NULL;          /* Output FILE ptr to MSTAR header file   */

    int   i, j, rv, n, numrows, numcols, numgot;

    char *MSTARname=NULL;      /* Input MSTAR filename           */
    char  RAWname[80];         /* Output MSTAR RAW filename      */
    char  HDRname[80];         /* Phoenix header filename buffer */

    int            outOpt;     /* ALL data, or MAG ONLY...    */
    int            phlen, nhlen, mstartype;
    long           magloc, bytesPerImage, nchunks, totchunks;

    char          *tptr=NULL;  /* Temp buffer ptr */
    char          *phdr=NULL;  /* Ptr to buffer to hold Phoenix header */
    unsigned char  tbuff[1024];

    unsigned short *FSCENEdata=NULL; /* Ptr to Fullscene data buffer */
    float          *CHIPdata=NULL;   /* Ptr to CHIp data buffer      */

    /* Byte Order Variables */
    int            byteorder;
    unsigned char  bigfloatbuf[4];   /* BigEndian float buffer... */
    float          littlefloatval;   /* LittleEndian float value  */
    unsigned char  bigushortbuf[2];  /* BigEndian ushort buffer...*/
    unsigned short littleushortval;  /* LittleEndian ushort value.*/

    /************************ B E G I N  C O D E ****************************/

    if (argc < 3)
    {
      fprintf(stderr,"\nUsage: mstar2raw <MSTAR Input> [Output Option]\n");
      fprintf(stderr,"where: Output Option = [0] --> Output ALL image data\n");
      fprintf(stderr,"                       [1] --> Output MAG data only\n\n");
      exit(1);
    }
    else
    {
      MSTARname = argv[1]; // Input filename
      outOpt    = atoi(argv[2]);
      if ((outOpt != ALL_DATA) && (outOpt != MAG_DATA) && (outOpt != HDR_DATA))
      {
      fprintf(stderr,
              "\nError: Incorrect image output option (0:All data or 1:MAG data only)!\n\n");
      exit(1);
      }
    }

    /* Form output Phoenix header filename */
    HDRname[0] = '\0';
    tptr = (char *) rindex(MSTARname, '/');
    if (tptr == NULL)
    {
      strcat(HDRname, MSTARname);
    }
    else
    {
      sprintf(HDRname, "%s", tptr+1);
    }
    strcat(HDRname, ".hdr");

    /* Form output MSTAR RAW filename */
    RAWname[0] = '\0';
    tptr = (char *) rindex(MSTARname, '/');
    if (tptr == NULL)
    {
      strcat(RAWname, MSTARname);
    }
    else
    {
      sprintf(RAWname, "%s", tptr+1);
    }

    switch (outOpt)
    {
      case ALL_DATA: // Put all converted data into the same file
        strcat(RAWname, ".all");
        break;
      case MAG_DATA: // Put the magnitude in a .mag file and the header in a .hdr file.
        strcat(RAWname, ".mag");
        break;
      case HDR_DATA: // Extract only the header data and put it in a .hdr file
        strcat(RAWname, ".hdr");
        break;
      default:
        perror("Invalid option.  Options are (0) ALL_DATA, (1) MAG_DATA, (2) HDR_DATA");
        exit(1);
    }

    // printf("\nmstar2raw conversion: started!\n\n");

    // Open the phx file for reading as a binary file
    MSTARfp = fopen(MSTARname, "rb");
    if (MSTARfp == NULL)
    {
      fprintf(stderr, "\n\nError: Unable to open [%s] for reading!\n\n", MSTARname);
      exit(1);
    }

    char buffer[200000];
    struct MetaLine metadata[100];

    // Read the entire binary file contents.
    // NOTE: Use fread() instead of fgets().  We're reading in binary data and fgets is not meant for that
    // since any read character could be a '\0' symbol. That or it could never find it and cause a buffer overflow.
    size_t bytes_read = fread(buffer, sizeof(char), sizeof(buffer) / sizeof(char), MSTARfp);
    fclose(MSTARfp);

    if (bytes_read == 0)
    {
      fprintf(stderr, "Failed to read binary file. Exiting...\n");
      exit(1);
    }
    else
    {
      // fprintf(stdout, "Finished reading binary file. Read %ld bytes from stream.\n", bytes_read);
    }

    char* header_end = strstr(buffer, "[EndofPhoenixHeader]");
    int endIdx = 0;

    if (header_end != NULL)
    {
      // Get the position of the start of the Phoenix header end flag. Use pointer arithmetic to get the address
      // of the start of the substring. Subtract the address of the start of the substring from the beginning of
      // the entire string to isolate the whole header.
      endIdx = header_end - buffer;

      // Use the address of the header end to build a new buffer and store the entire header in it. Parse this for the metadata.
      char header[endIdx+1];
      header[endIdx] = '\0';
      char *header_step = strncpy(header, buffer, endIdx);

      // printf("%s\n\n\n", header);
      // Parse the header and store it in the metadata container
      parseHeaderLine(metadata, header);
    }
    else
    {
      fprintf(stderr, "Could not find the [EndofPhoenixHeader] flag. Exiting...\n");
      exit(1);
    }

    // Move the pointer past the header end flag. Now we begin reading the body.
    int bodyStart = endIdx;
    while (buffer[bodyStart] != ']') { ++bodyStart; }
    ++bodyStart;

    return 0;
}

/************************************************
 * Function:    byteswap_SR_IR                  *
 *   Author:    Dave Hascher (Veridian Inc.)    *
 *     Date:    06/05/97                        *
 *    Email:    dhascher@dytn.veridian.com      *
 ************************************************
 * 'SR' --> Sun 32-bit float value              *
 * 'IR' --> PC-Intel 32-bit float value         *
 ************************************************/

static float byteswap_SR_IR(pointer)
unsigned char *pointer;
{
  float *temp;
  unsigned char iarray[4], *charptr;

  iarray[0] = *(pointer + 3);
  iarray[1] = *(pointer + 2);
  iarray[2] = *(pointer + 1);
  iarray[3] = *(pointer );
  charptr = iarray;
  temp    = (float *) charptr;
  return *(temp);
}


/************************************************
 * Function:    byteswap_SUS_IUS                *
 *   Author:    John Querns (Veridian Inc.)     *
 *     Date:    06/05/97                        *
 *    Email:    jquerns@dytn.veridian.com       *
 ************************************************
 * 'SUS' --> Sun 16-bit uns short value         *
 * 'IUS' --> PC-Intel 16-bit uns short value    *
 ************************************************/

static unsigned short byteswap_SUS_IUS(pointer)
unsigned char *pointer;
{
  unsigned short *temp;
  unsigned char iarray[2], *charptr;

  iarray[0] = *(pointer + 1);
  iarray[1] = *(pointer );
  charptr = iarray;
  temp    = (unsigned short *) charptr;
  return *(temp);
}



/**********************************
 *   checkByteOrder()             *
 **********************************
 * Taken from:                    *
 *                                *
 *   Encyclopedia of Graphic File *
 *   Formats, Murray & Van Ryper, *
 *   O'Reilly & Associates, 1994, *
 *   pp. 114-115.                 *
 *                                *
 * Desc: Checks byte-order of CPU.*
 **********************************/

static int CheckByteOrder(void)

{
  short   w = 0x0001;
  char   *b = (char *) &w;

  return(b[0] ? LSB_FIRST : MSB_FIRST);
}


void parseHeaderLine(struct MetaLine *metadata, char* buffer)
{
}