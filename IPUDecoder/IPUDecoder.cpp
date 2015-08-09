/* Copyright (c) 2008 Holger Kuhn (hawkear@gmx.de) */
/* Modified by Sameh Barakat (http://sres.tumblr.com) */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define kBufSize        4096
#define kByteAlign      0x80
#define kWdPtrStart     0x80

static unsigned char buf[kBufSize + 4];
static unsigned int wdPtr = kWdPtrStart;
static int wdIndex = 0;
static int numInBuf = 0;

typedef struct {
	FILE *file;
	unsigned char outbuf;
	int outcnt;
} BitsOut;

FILE *infile;
BitsOut outfile;
int isempty = 1;

int Get_Bits(unsigned int *destBuf, int numBits);

int Get(int numBits)
{
	unsigned int buf;

	if (!Get_Bits(&buf, numBits))
	{
		fprintf(stderr, "Error (Get - End Of File)\n");
		return(0);
	}

	return(buf);
}

typedef struct t_MBData		/*	Macroblock data	*/
{
	long int Byte;
	unsigned int Bit;
	int dct_dc_y;
	int dct_dc_cb;
	int dct_dc_cr;
	int quant;
} t_MBData;

int get_dcs_y(void);
int get_dcs_c(void);
void put_dcs_y(int);
void put_dcs_c(int);
int vlc(int);
int ivlc(int);
void close(void);
int convert_ipu(char *infilename, char *outfilename, long int Start, char *ipumode);

void setpos(long int Byte, unsigned int Bit){
	fseek(infile, Byte, SEEK_SET);
	numInBuf = 0;
	wdIndex = 0;
	Get(Bit);
}
void getpos(long int *Byte, unsigned int *Bit){
	int count;
	unsigned int wdPtrSave;

	wdPtrSave = wdPtr;
	for (count = 0; wdPtrSave != 0x80; count++)
		wdPtrSave <<= 1;
	*Bit = count; // 0x80=0,0x40=1,0x20=2 usw

	*Byte = ftell(infile) - numInBuf + wdIndex;
}

int Get_Bits(unsigned int *destBuf, int numBits)
{
	int index;
	*destBuf = 0;

	for (index = 0; index < numBits; index++)
	{
		/* refresh buffer if empty */
		if (wdIndex >= numInBuf)
		{
			/* read next kBufSize bytes */
			if ((numInBuf = fread(buf + 4, sizeof(unsigned char), kBufSize,
				infile)) <= 0)
				return(0);              /*      end of bit stream       */

			wdIndex = 0;
			wdPtr = kWdPtrStart;
		}

		/* get next bit */

		*destBuf = ((buf[wdIndex + 4] & wdPtr) ?
			(*destBuf << 1) | 0x01 : (*destBuf << 1));

		/* update bit pointer */
		if (wdPtr == 0x01)
		{
			wdIndex++;
			wdPtr = kWdPtrStart;
		}
		else
			wdPtr >>= 1;
	}
	return(1);
}

int Next_Bits(unsigned int *destBuf, int numBits)
{
	unsigned int    wdPtrSave;
	int                             wdIndexSave;
	int                             index;

	/* save current buffer state */
	wdPtrSave = wdPtr;
	wdIndexSave = wdIndex;

	*destBuf = 0;

	for (index = 0; index < numBits; index++)
	{
		/* refresh buffer if empty */
		if (wdIndex >= numInBuf)
		{
			/* save last 4 bytes of old content */
			buf[0] = buf[numInBuf];
			buf[1] = buf[numInBuf + 1];
			buf[2] = buf[numInBuf + 2];
			buf[3] = buf[numInBuf + 3];

			wdIndexSave -= numInBuf;

			/* read next kBufSize bytes */
			if ((numInBuf = fread(buf + 4, sizeof(unsigned char), kBufSize,
				infile)) <= 0)
				return(0);              /*      end of bit stream       */

			wdIndex = 0;
			wdPtr = kWdPtrStart;
		}

		/* get next bit */

		*destBuf = ((buf[wdIndex + 4] & wdPtr) ?
			(*destBuf << 1) | 0x01 : (*destBuf << 1));

		/* update bit pointer */
		if (wdPtr == 0x01)
		{
			wdIndex++;
			wdPtr = kWdPtrStart;
		}
		else
			wdPtr >>= 1;
	}

	/* restore previous buffer state */
	wdPtr = wdPtrSave;
	wdIndex = wdIndexSave;

	return(1);
}

int Next_Start_Code()
{
	unsigned int buf;

	/* locate next start code */

	if (wdPtr != kByteAlign) /* not byte aligned */
	{
		/* skip stuffed zero bits */
		wdPtr = kByteAlign;
		wdIndex++;
	}

	if (!Next_Bits(&buf, 24))
		return(0); /* end of bitstream */

	while (buf != 0x000001)
	{
		if (!Get_Bits(&buf, 8)) /* zero byte */
			return(0);
		if (!Next_Bits(&buf, 24))
			return(0);
	}

	return(1);
}

void putbits(BitsOut *file, int data, int n)
{
	int i;
	unsigned int mask;

	mask = 1 << (n - 1);

	for (i = 0; i<n; i++)
	{
		(*file).outbuf <<= 1;

		if (data & mask)
			(*file).outbuf |= 1;

		mask >>= 1;
		(*file).outcnt++;

		if ((*file).outcnt == 8)
		{
			putc((*file).outbuf, (*file).file);
			(*file).outcnt = 0;
		}
	}
}

void putbuf(BitsOut *file)
{

	if ((*file).outcnt>0)
		putbits(file, 0, 8 - (*file).outcnt);
}

int main(int argc, char *argv[])
{
	char* mode = "0";
	atexit(close);

	if (argc < 3)
	{
		fprintf(stderr, "IPUDecoder 1.0\n\nIPUDecoder input.ipu output.m2v [mode]\n\n[mode] can either be 0 or 1. 0 is assumed, if skipped\n");
		exit(1);
	}

	if (argc == 4)
		mode = argv[3];

	convert_ipu(argv[1], argv[2], 0, mode);

	exit(1);
}

int convert_ipu(char *infilename, char *outfilename, long int Start, char *ipumode){
	char wavename[258];

	int sizex;
	int sizey;
	int frames;
	int frame;
	int dct_dc_y;
	int dct_dc_cb;
	int dct_dc_cr;
	int quant;
	int flag;
	int mb;
	int mb_source;
	int intraquant;
	int block;
	int eob;
	int size;
	int diff;
	int absval;
	long int FrameByte;
	unsigned int FrameBit;

	t_MBData * MBData;

	if ((infile = fopen(infilename, "rb")) == NULL)
	{
		fprintf(stderr, "Can't open %s\n", infilename);
		exit(1);
	}
	setpos(Start, 0);

	if (!(outfile.file = fopen(outfilename, "wb")))
	{
		fprintf(stderr, "Can't write to %s\n", wavename);
		exit(1);
	}
	outfile.outcnt = 0;

	if (0x6970756d != Get(32)) {
		fprintf(stderr, "%s is no IPU\n", infilename);
		close();
		exit(1);
	}

	Get(32);	// Filesize

	sizex = Get(8);
	sizex = sizex + Get(8)*(1 << 8);

	sizey = Get(8);
	sizey = sizey + Get(8)*(1 << 8);

	frames = Get(8);
	frames = frames + Get(8)*(1 << 8);
	frames = frames + Get(8)*(1 << 16);
	frames = frames + Get(8)*(1 << 24);

	printf("%dx%d\n", sizex, sizey);
	printf("%02d:%02d:%02d.%02d\n\n", frames / 25 / 60 / 60, (frames % (25 * 60 * 60)) / 25 / 60, (frames % (25 * 60)) / 25, frames % 25);

	MBData = (t_MBData*)malloc(((sizex / 16)*(sizey / 16) + 1)*sizeof(t_MBData));
	if (MBData == NULL) {
		fprintf(stderr, "Could not allocate memory\n");
		close();
		exit(1);
	}

	int fps = 30;//25 also. Actually, it is 29.97 rounded to 30.

	for (frame = 0; frame<frames; frame++){
		printf("Frame: %d/%d\r", frame + 1, frames);
		flag = Get(8);

		if (frame == 0) {
			// Write Sequence Header
			putbits(&outfile, 0x1b3, 32);
			putbits(&outfile, sizex, 12);
			putbits(&outfile, sizey, 12);
			putbits(&outfile, 0x1, 4);			// Ascpect Ratio	1     1:1
			//					2     4:3
			//					3    16:9
			//					4  2.21:1

			//putbits(&outfile,0x3,4);			// Framerate		3  25 fps
			putbits(&outfile, 0x4, 4); //Framerate 29 fps

			putbits(&outfile, 0x30d4, 18);	// Bitrate ($3FFFF=Variabel)
			//putbits(&outfile,0x3FFFF,18);	// Bitrate ($3FFFF=Variabel)
			putbits(&outfile, 1, 1);			// Marker, soll immer 1 sein
			putbits(&outfile, 112, 10);		// VBV
			//putbits(&outfile,69,10);		// VBV
			putbits(&outfile, 0, 1);			// Constrained Parameter Flag
			putbits(&outfile, 0, 1);			// Intra Matrix Standard
			putbits(&outfile, 0, 1);			// Non-Intra Matrix Standard
			//			putbits(&outfile,0x2cee100,32);

			if (!(flag & 128)) {
				// Sequence Extension
				putbits(&outfile, 0x1b5, 32);
				putbits(&outfile, 0x1, 4);		// Start Code Identifier
				putbits(&outfile, 0x4, 4);		// Main Profil
				putbits(&outfile, 0x8, 4);		// Main Level
				putbits(&outfile, 0x1, 1);		// Progressive Sequence
				putbits(&outfile, 0x1, 2);		// Chroma Format 4:2:0
				putbits(&outfile, 0x0, 2);		// Breite Extension
				putbits(&outfile, 0x0, 2);		// Höhe Extension
				putbits(&outfile, 0x0, 12);		// Bitrate Extension
				putbits(&outfile, 0x1, 1);		// Marker
				putbits(&outfile, 0x0, 8);		// VBV Buffer Extension
				putbits(&outfile, 0x0, 1);		// Low Delay
				putbits(&outfile, 0x0, 2);		// Framerate Extension Numerator
				putbits(&outfile, 0x0, 5);		// Framerate Extension Denominator
				//				putbits(&outfile,0x148a0001,32);
				//				putbits(&outfile,0,16);

				/*			// Sequence Display Extension
				*				putbits(&outfile,0x1b5,32);
				*				putbits(&outfile,0x2,4);		// Start Code Identifier
				*				putbits(&outfile,0x1,3);		// Video Format		1 PAL
				*				putbits(&outfile,0x0,1);		// Bit Color
				*
				//				putbits(&outfile,0x23050504,32);
				*
				*				putbits(&outfile,sizex,14);	// Display Breite
				*				putbits(&outfile,1,1);		// Marker
				*				putbits(&outfile,sizey,14);  // Display Höhe
				*				putbits(&outfile,0,3);
				*/
			}
		}

		// Write GOP Header
		putbits(&outfile, 0x1b8, 32);
		putbits(&outfile, 0, 1);				// Drop Frame. Since, this is set to 0, a rounded number of fps (29.97 ~ 30) is used for the following
		putbits(&outfile, frame / fps / 60 / 60, 5);	// Stunden
		putbits(&outfile, (frame % (fps * 60 * 60)) / fps / 60, 6); // Minuten
		putbits(&outfile, 1, 1);				// Marker
		putbits(&outfile, (frame % (fps * 60)) / fps, 6);// Sekunden
		putbits(&outfile, frame%fps, 6);		// Frames
		putbits(&outfile, 1, 1);				// Closed GOP
		putbits(&outfile, 0, 6);

		// Write Picture Header
		putbits(&outfile, 0x100, 32);
		putbits(&outfile, 0x0, 10);			// Temporal Reference
		putbits(&outfile, 0x1, 3);				// Coding Type Intra
		putbits(&outfile, 0xffff, 16);			// VBV Delay
		putbits(&outfile, 0, 3);
		//		putbits(&outfile,0xffff8,32);

		// Write Picture Coding Extension
		if (!(flag & 128)) {
			putbits(&outfile, 0x1b5, 32);
			putbits(&outfile, 0x8ffff, 20);
			putbits(&outfile, flag & 3, 2);			// Intra DC Precision
			putbits(&outfile, 3, 2);				// Frame Picture
			putbits(&outfile, 2, 3);
			putbits(&outfile, (flag & 64) / 64, 1);	// QST
			putbits(&outfile, (flag & 32) / 32, 1);		// Intra VLC Format
			putbits(&outfile, (flag & 16) / 16, 1);	// Alternate Scan
			putbits(&outfile, 1, 2);
			putbits(&outfile, 0x80, 8);
		}


		// Get Macroblock-infos
		dct_dc_y = 0;
		dct_dc_cb = 0;
		dct_dc_cr = 0;
		quant = 1;
		for (mb = 0; mb<(sizex / 16)*(sizey / 16); mb++) {
			if (mb>0) {
				if (!Get(1)) {
					fprintf(stderr, "MBA_Incr wrong in IPU\n");
					close();
					exit(1);
				}
			}
			// Save position (in IPU) of Macroblock
			getpos(&MBData[mb].Byte, &MBData[mb].Bit);

			if (Get(1)) {
				intraquant = 0;
			}
			else {
				if (!Get(1)) {
					fprintf(stderr, "MBT wrong in IPU\n");
					close();
					exit(1);
				}
				intraquant = 1;
			}

			if (flag & 4) {
				Get(1);
			}

			if (intraquant) {
				quant = Get(5);
			}
			MBData[mb].quant = quant;

			for (block = 0; block<6; block++) {
				if (block<4) {
					if (size = get_dcs_y()) {
						diff = Get(size);
						if (!(diff & (1 << (size - 1))))
							diff = (-1 << size) | (diff + 1);
						dct_dc_y += diff;
					}
					if (block == 0)
					{
						MBData[mb].dct_dc_y = dct_dc_y;
					}
				}
				else {
					if (size = get_dcs_c()) {
						diff = Get(size);
						if (!(diff & (1 << (size - 1))))
							diff = (-1 << size) | (diff + 1);
					}
					if (block == 4) {
						if (size)
						{
							dct_dc_cb += diff;
						}
						MBData[mb].dct_dc_cb = dct_dc_cb;
					}
					else {
						if (size)
						{
							dct_dc_cr += diff;
						}
						MBData[mb].dct_dc_cr = dct_dc_cr;
					}
				}
				do {
					if (flag & 32)
						eob = ivlc(0);
					else
						eob = vlc(0);
					if (eob == 0) Get(1);
				} while (eob != 1);
			}
		}

		getpos(&FrameByte, &FrameBit);	// Position speichern

		// Write Macroblocks
		dct_dc_y = 0;
		dct_dc_cb = 0;
		dct_dc_cr = 0;
		quant = 1;

		int slicescnt = 1;
		int counter = -1;
		for (mb = 0; mb<(sizex / 16)*(sizey / 16); mb++) {
			if (strcmp(ipumode, "0"))
				mb_source = (mb % (sizex / 16)) * (sizey / 16) + mb / (sizex / 16);	// Singstar IPU
			else
				mb_source = mb;														// Other IPU
			counter++;

			if ((counter % (sizex / 16)) == 0)
			{
				if (outfile.outcnt % 8 > 0 && counter != 0)// Padding the slice into a byte boundary with zeros
				{
					int temp = outfile.outcnt;
					for (int i = 0; i< 8 - (temp % 8); i++)
						putbits(&outfile, 0, 1);
				}

				putbits(&outfile, 0x1, 24); // Splitting the single-slice frame into multi-slice frame
				putbits(&outfile, slicescnt, 8);
				putbits(&outfile, MBData[mb_source].quant, 5);		// Quantiser
				putbits(&outfile, 0, 1);		// Extra Bit clear
				slicescnt++;
			}


			putbits(&outfile, 1, 1);		// MBA_Incr=1

			setpos(MBData[mb_source].Byte, MBData[mb_source].Bit);

			if (Get(1)) {
				intraquant = 0;
			}
			else {
				if (!Get(1)) {
					fprintf(stderr, "MBT wrong in IPU\n");
					close();
					exit(1);
				}
				intraquant = 1;
			}

			putbits(&outfile, 1, 1);



			if (flag & 4) {
				putbits(&outfile, Get(1), 1);
			}

			if (intraquant) { // Skipping the MB quantization value since it is no longer needed.
				Get(5);
			}


			for (block = 0; block<6; block++) {
				if (block == 0) {
					Get(get_dcs_y());	// DCT_DC in Eingabestream überspringen
					diff = MBData[mb_source].dct_dc_y - dct_dc_y;
					dct_dc_y = MBData[mb_source].dct_dc_y;

					if (counter % (sizex / 16) == 0 && counter != 0) // MBData[mb_source].dct_dc_y is the original differential value for the MB's DC. So, no substration is needed.
					{
						diff = MBData[mb_source].dct_dc_y;
					}

					absval = (diff<0) ? -diff : diff; /* abs(val) */
					size = 0;
					while (absval) {
						absval >>= 1;
						size++;
					}
					put_dcs_y(size);
					absval = diff;
					if (absval <= 0)
						absval += (1 << size) - 1;
					putbits(&outfile, absval, size);
				}
				else if (block>3)	{
					Get(get_dcs_c());	// DCT_DC in Eingabestream überspringen
					if (block == 4) {
						diff = MBData[mb_source].dct_dc_cb - dct_dc_cb;
						dct_dc_cb = MBData[mb_source].dct_dc_cb;
						if (counter % (sizex / 16) == 0 && counter != 0)
						{
							diff = MBData[mb_source].dct_dc_cb;
						}
					}
					else {
						diff = MBData[mb_source].dct_dc_cr - dct_dc_cr;
						dct_dc_cr = MBData[mb_source].dct_dc_cr;
						if (counter % (sizex / 16) == 0 && counter != 0)
						{
							diff = MBData[mb_source].dct_dc_cr;
						}
					}
					absval = (diff<0) ? -diff : diff; /* abs(val) */
					size = 0;
					while (absval) {
						absval >>= 1;
						size++;
					}
					put_dcs_c(size);
					absval = diff;
					if (absval <= 0)
						absval += (1 << size) - 1;
					putbits(&outfile, absval, size);
				}
				else {
					size = get_dcs_y();
					put_dcs_y(size);
					diff = Get(size);
					putbits(&outfile, diff, size);		// DCT_DC kopieren (Blöcke 1,2,3)
					if (size) {
						if (!(diff&(1 << (size - 1))))
							diff = (-1 << size) | (diff + 1);
						dct_dc_y += diff;
					}
				}
				do {
					if (flag & 32)
						eob = ivlc(1);
					else
						eob = vlc(1);
					if (eob == 0)
						putbits(&outfile, Get(1), 1);
				} while (eob != 1);
			}
		}
		putbuf(&outfile);

		setpos(FrameByte, FrameBit);

		// Jump to End of Frame
		if (!Next_Start_Code()) {
			fprintf(stderr, "End of Stream\n");
			close();
			exit(1);
		}

		if (Get(32) != 0x000001b0) {
			fprintf(stderr, "No 1b0\n");
			close();
			exit(1);
		}
	}


	putbits(&outfile, 0x1b7, 32);		// Ende

	free(MBData);
	fclose(infile);
	close();
	exit(0);
}

void close(void) {
	unsigned int length;

	if (infile != NULL) {
		fclose(infile);	// close file
	}
	if (outfile.file != NULL) {
		putbuf(&outfile);		// flush buffer
		fclose(outfile.file);	// close file
	}
}

int vlc(int write){
	int bits;
	int level = 0;

	bits = Get(2);
	if (write) putbits(&outfile, bits, 2);
	if (bits == 2)
		return(1);		// 10 - EOB
	if (bits == 3)
		return(0);		// 11
	if (bits == 1) {
		bits = Get(1);
		if (write) putbits(&outfile, bits, 1);
		if (bits)
			return(0);
		else {
			bits = Get(1);
			if (write) putbits(&outfile, bits, 1);
			return(0);
		}
	}
	// 00
	bits = Get(1);
	if (write) putbits(&outfile, bits, 1);
	if (bits) {
		// 001
		bits = Get(2);
		if (write) putbits(&outfile, bits, 2);
		if (bits < 1) {
			// 00100
			bits = Get(3);
			if (write) putbits(&outfile, bits, 3);
		}
		// 001xx
		return(0);
	}
	else {
		// 000
		bits = Get(3);
		if (write) putbits(&outfile, bits, 3);
		if (bits >= 4)
			return(0);
		if (bits >= 2) {
			bits = Get(1);
			if (write) putbits(&outfile, bits, 1);
			return(0);
		}
		if (bits) {
			bits = Get(18);
			if (write) putbits(&outfile, bits, 18);
			return(2);	// Escape
		}
		bits = Get(1);
		if (write) putbits(&outfile, bits, 1);
		if (bits) {
			bits = Get(3);
			if (write) putbits(&outfile, bits, 3);
			return(0);
		}
		do {
			bits = Get(1);
			if (write) putbits(&outfile, bits, 1);
			level++;
		} while (!bits && level<6);
		if (level<6) {
			bits = Get(4);
			if (write) putbits(&outfile, bits, 4);
			return(0);
		}
		else {
			fprintf(stderr, "Invalid VLC\n");
			close();
			exit(1);
		}
	}
}

int ivlc(int write){
	int bits;
	int level = 0;

	bits = Get(2);
	if (write) putbits(&outfile, bits, 2);

	if (bits == 3)
	{
		//11
		bits = Get(1);
		if (write) putbits(&outfile, bits, 1);
		if (bits == 0)
			return(0); //110
		//111

		bits = Get(1);
		if (write) putbits(&outfile, bits, 1);
		if (bits == 0)
		{
			bits = Get(1);
			if (write) putbits(&outfile, bits, 1);
			return(0);//1110x
		}

		//1111

		bits = Get(1);
		if (write) putbits(&outfile, bits, 1);
		if (bits == 0)
		{
			bits = Get(2);
			if (write) putbits(&outfile, bits, 2);
			return(0); //11110xx
		}

		//11111

		bits = Get(2);
		if (write) putbits(&outfile, bits, 2);
		if (bits == 0)
			return(0); //1111100
		bits = Get(1);
		if (write) putbits(&outfile, bits, 1); //11111xxx
		return(0);
	}

	if (bits == 2)
		return(0);		// 10
	if (bits == 1) {
		// 01
		bits = Get(1);
		if (write) putbits(&outfile, bits, 1);
		if (bits)
		{
			// 011
			bits = Get(1);
			if (write) putbits(&outfile, bits, 1);
			if (bits)
				return(0); // 0111
			return(1); // EOB 0110
		}
		return(0); // 010
	}
	// 00
	bits = Get(1);
	if (write) putbits(&outfile, bits, 1);
	if (bits) {
		// 001
		bits = Get(2);
		if (write) putbits(&outfile, bits, 2);
		if (bits == 0) {
			// 00100
			bits = Get(3);
			if (write) putbits(&outfile, bits, 3); // 00100xxx

		}
		// 001xx
		return(0);
	}
	else {
		// 000
		bits = Get(3);
		if (write) putbits(&outfile, bits, 3); // 000xxx
		if (bits >= 4)
			return(0); // 0001xx
		if (bits >= 2) {
			bits = Get(1);
			if (write) putbits(&outfile, bits, 1);
			return(0);
		}
		if (bits) {
			bits = Get(18);
			if (write) putbits(&outfile, bits, 18);
			return(2);	// Escape
		}
		bits = Get(1);
		if (write) putbits(&outfile, bits, 1);
		if (bits) { // 0000001
			bits = Get(2);
			if (write) putbits(&outfile, bits, 2);
			if (bits != 2) // 0000001xx
				return(0);
			bits = Get(1); // 00000011xx
			if (write) putbits(&outfile, bits, 1);
			return(0);
		} // 0000000
		do {
			bits = Get(1);
			if (write) putbits(&outfile, bits, 1);
			level++;
		} while (!bits && level<6);
		if (level<6) {
			bits = Get(4);
			if (write) putbits(&outfile, bits, 4);
			return(0);
		}
		else {
			fprintf(stderr, "Invalid VLC\n");
			close();
			exit(1);
		}
	}

}

int get_dcs_y(void){
	int bits;

	if (!(bits = Get(2)))
		return(1);		// 00
	if (bits == 1)
		return(2);		// 01
	bits <<= 1;
	bits |= Get(1);
	if (bits == 4)
		return(0);		// 100
	if (bits == 5)
		return(3);		// 101
	if (bits == 6)
		return(4);		// 110
	if (!Get(1))
		return(5);
	if (!Get(1))
		return(6);
	if (!Get(1))
		return(7);
	if (!Get(1))
		return(8);
	if (!Get(1))
		return(9);
	if (!Get(1))
		return(10);
	return(11);
}

int get_dcs_c(void){
	int bits;

	if (!(bits = Get(2)))
		return(0);		// 00
	if (bits == 1)
		return(1);		// 01
	if (bits == 2)
		return(2);		// 10
	if (!Get(1))
		return(3);
	if (!Get(1))
		return(4);
	if (!Get(1))
		return(5);
	if (!Get(1))
		return(6);
	if (!Get(1))
		return(7);
	if (!Get(1))
		return(8);
	if (!Get(1))
		return(9);
	if (!Get(1))
		return(10);
	return(11);
}

void put_dcs_y(int len){
	switch (len) {
	case 0:
		putbits(&outfile, 4, 3);
		break;
	case 1:
		putbits(&outfile, 0, 2);
		break;
	case 2:
		putbits(&outfile, 1, 2);
		break;
	case 3:
		putbits(&outfile, 5, 3);
		break;
	case 4:
		putbits(&outfile, 6, 3);
		break;
	case 5:
		putbits(&outfile, 14, 4);
		break;
	case 6:
		putbits(&outfile, 30, 5);
		break;
	case 7:
		putbits(&outfile, 62, 6);
		break;
	case 8:
		putbits(&outfile, 126, 7);
		break;
	case 9:
		putbits(&outfile, 254, 8);
		break;
	case 10:
		putbits(&outfile, 510, 9);
		break;
	case 11:
		putbits(&outfile, 511, 9);
		break;
	}
}

void put_dcs_c(int len){
	switch (len) {
	case 0:
		putbits(&outfile, 0, 2);
		break;
	case 1:
		putbits(&outfile, 1, 2);
		break;
	case 2:
		putbits(&outfile, 2, 2);
		break;
	case 3:
		putbits(&outfile, 6, 3);
		break;
	case 4:
		putbits(&outfile, 14, 4);
		break;
	case 5:
		putbits(&outfile, 30, 5);
		break;
	case 6:
		putbits(&outfile, 62, 6);
		break;
	case 7:
		putbits(&outfile, 126, 7);
		break;
	case 8:
		putbits(&outfile, 254, 8);
		break;
	case 9:
		putbits(&outfile, 510, 9);
		break;
	case 10:
		putbits(&outfile, 1022, 10);
		break;
	case 11:
		putbits(&outfile, 1023, 10);
		break;
	}
}
