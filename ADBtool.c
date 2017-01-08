#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

const char CHUNKY_MAGIC[6] = { 'c', 'h', 'u', 'n', 'k', 'y' };
const char ADB_MAGIC[13] = { 'a', 's', 'c', 'i', 'i', 'd', 'a', 't', 'a', 'b', 'a', 's', 'e' };


void fatal(char * msg, ...)
{
	va_list list;
	va_start(list, msg);

	printf("fatal: ");
	vprintf(msg, list);
	printf("\n");

	exit(1);
}
int main(int argc, char *argv[])
{

	if (argc < 2) { //currently only argument is te *.adb file
		fatal("ADB file argument required; if second argument is '1', outputs into .lst file instead");
	}
	int lst = 0;
	if (argc > 2) {
		lst = 1;//output as .lst-like format. using lazy method of switching formats, because I'm lazy and I assume I won't give the source code to anyone anymore.
	}

	char * adbFileName = argv[1];
	FILE * aFile = fopen(adbFileName, "rb");

	printf("Reading magic\n");
	char magic[6];
	fread(&magic, sizeof * magic, 6, aFile); // chunky magic

	if (memcmp(magic, CHUNKY_MAGIC, 6) != 0) {
		fatal("chunky magic mismatch");
	}

	uint32_t readData;
	fread(&readData, 4, 1, aFile); // unk, usually 0
	fread(&readData, 4, 1, aFile); // unk, usually 0
	fread(&readData, 4, 1, aFile); // unk, usually 0x00010000
	fread(&readData, 4, 1, aFile); // unk, usually 1, or 6 for *.mpo files
	
	//checking adb magic
	printf("Reading ADB magic\n");
	char magic2[14] = ""; ///TODO: this and beginString below needs some error handling if we run out of characters
	char ch = EOF; 
	size_t k = 0;
	do //complicated method of retrieving a null-terminated string
	{
		//printf("Reading ADB char...\n");//too much spam!

		ch = fgetc(aFile);
		/* Error handling: */
		if (EOF == ch)
		{
			/* Getting EOF means end of file (unexpected, from the OP's specifications) or an error. */
			if (ferror(aFile))
			{
				perror("fgetc() failed");
			}
			else
			{
				fatal("Unexpected end of file.\n");
			}

			exit(EXIT_FAILURE);
		}

		magic2[k] = ch;
		k++;
	} while ('\0' != ch);
	if (memcmp(magic2, ADB_MAGIC, 13)) {
		fatal("ADB has invalid magic");
	}
	
	fread(&readData, 4, 1, aFile); // unk, usually x00010000

	uint32_t totalLength;
	fread(&totalLength, 4, 1, aFile); // length of everything after this part

	printf("Reading beginString\n");
	char beginString[64] = "";//first string, usually "something_begin"
	ch = EOF;
	k = 0;
	do
	{

		ch = fgetc(aFile);
		/* Error handling: */
		if (EOF == ch)
		{
			/* Getting EOF means end of file (unexpected, from the OP's specifications) or an error. */
			if (ferror(aFile))
			{
				perror("fgetc() failed");
			}
			else
			{
				fatal("Unexpected end of file.\n");
			}

			exit(EXIT_FAILURE);
		}

		beginString[k] = ch;
		k++;
	} while ('\0' != ch);
	
	printf("Reading stringData\n");
	size_t stringsLength;
	fread(&stringsLength, 4, 1, aFile); // length of all the strings at the start
	char * stringsData = malloc(sizeof(char) * stringsLength); //biggest encountered: 202552 chars. sooo, yeah, maybe don't load them into one huge string, but read the offsets straight from the file instead D:
	//actually, found effects.adb, 235019
	///ugh... can we optimize that method? we'd have to use the null-terminated-string-finding algo above for each and every string...
	fread(stringsData, sizeof(char), stringsLength, aFile); //those strings are referenced in the last part of the file

	printf("Reading numLines\n");
	uint32_t numLines;
	fread(&numLines, 4, 1, aFile); // number of 'lines' in the db
	
	printf("Reading offsets\n");
	uint32_t * unk3 = malloc(sizeof(uint32_t) * numLines); //biggest encountered: 4912 numLines
	uint32_t * valuesOffset = malloc(sizeof(uint32_t) * numLines);
	for (uint32_t i = 0; i < numLines; i++) {
		fread(&readData, 4, 1, aFile);
		unk3[i] = readData; //unknown values
		fread(&readData, 4, 1, aFile);
		valuesOffset[i] = readData; //those are offsets/keys of the array of ints from the last part of the file

	}
	printf("Reading values\n");
	uint32_t valuesLength;
	fread(&valuesLength, 4, 1, aFile); //number of ints in the array
	uint32_t * values = malloc(sizeof(uint32_t) * valuesLength);; //biggest encountered: 180060 and 181635 valuesLength
	for (uint32_t i = 0; i < valuesLength; i++) { //biiig array of ints
		fread(&readData, 4, 1, aFile); ///TODO: just read from file instead
		values[i] = readData;

	}
	printf("Closing adb file\n");
	fclose(aFile);
	//end of file

	char outName[64]=""; ///TODO: each array's size is permanently set in this code, because otherwise it crashes. *shrug*. please hire a programmer who knows what he's doing.
	printf("Openning json file\n");
	if (lst == 0) sprintf(outName, "%s.json", argv[1]);
	else sprintf(outName, "%s.lst", argv[1]);
	FILE * outFile = fopen(outName, "wb");

	if (!outFile)
		fatal("failed to open output file for writing");

	printf("Writing first lines...\n");
	char outString[1024]="";
	if (lst == 0) sprintf(outString, "{\n \"beginString\": \"%s\",\n \"variables\": {\n", beginString);//making a json-like output, because why not
	else sprintf(outString, "#%s\n", beginString);
	if (fwrite(outString, sizeof(char), strlen(outString), outFile) != strlen(outString)) {
		fatal("failed to write string to output file");
	}
	for (uint32_t i = 0; i < numLines; i++) {
		//printf("Writing line\n");
		uint32_t offset = valuesOffset[i];
		uint32_t varNum = values[offset];
		char * var = NULL;
		var = strdup(stringsData + unk3[i]);
		if (lst == 0) sprintf(outString, "  \"%s\": [\n", var);
		else sprintf(outString, "\n##%s\n\n", var);
		if (fwrite(outString, sizeof(char), strlen(outString), outFile) != strlen(outString)) {
			fatal("failed to write string to output file");
		}
		//example of a set of ints from the big array:
		//4, "timedhelp_step", "@timedhelp_movement^$GA_Forward~^$GA_StrafeLeft~^$GA_Backward~^$GA_StrafeRight~", "3.0", "60.0", 1, "timedhelp_end", 0
		//the 4 means the next 4 ints are offsets from the start of the big pile of strings
		//after these 4 ints, another one says how many more offsets follow
		//ends with a 0, meaning no ints follow
		while (varNum != 0) {
			//printf("Writing values\n");//too much spam D:
			if (lst == 0) {
				sprintf(outString, "   [\n");
				if (fwrite(outString, sizeof(char), strlen(outString), outFile) != strlen(outString)) {
					fatal("failed to write string to output file");
				}
			}
				for (uint32_t j = 0; j < varNum; j++) {
					var = strdup(stringsData + values[offset + j + 1]);
					if (lst == 0) {
						if (j + 1 == varNum)
							sprintf(outString, "    \"%s\"\n", var);
						else
							sprintf(outString, "    \"%s\",\n", var);
					}
					else {
						if (j + 1 == varNum)
							sprintf(outString, "%s\n", var);
						else
							sprintf(outString, "%s ", var);
					}
					if (fwrite(outString, sizeof(char), strlen(outString), outFile) != strlen(outString)) {
						fatal("failed to write string to output file");
					}
				}
				offset = offset + varNum + 1;
				varNum = values[offset];
				if (lst == 0) {
					if (varNum == 0)
						sprintf(outString, "   ]\n");
					else
						sprintf(outString, "   ],\n");

					if (fwrite(outString, sizeof(char), strlen(outString), outFile) != strlen(outString)) {
						fatal("failed to write string to output file");
					}
				}
			}
		if (lst == 0) {
			if (i + 1 == numLines)
				sprintf(outString, "  ]\n");
			else
				sprintf(outString, "  ],\n");
			if (fwrite(outString, sizeof(char), strlen(outString), outFile) != strlen(outString)) {
				fatal("failed to write string to output file");
			}
		}
		//printf("Done writing line\n");//too much spam D:
	}
	if (lst == 0) {
		sprintf(outString, "  }\n }");//making a json-like output, because why not
		if (fwrite(outString, sizeof(char), strlen(outString), outFile) != strlen(outString)) {
			fatal("failed to write string to output file");
		}
	}
	printf("Closing file\n");
	fclose(outFile);
	///TODO:crashes at the end, probably some undefined behaviour
	///but, it works fine, so whatever :P

    return 0;
}

