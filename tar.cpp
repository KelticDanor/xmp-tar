// XMPlay TAR archive plugin v1.2 (c) 2021 Nathan Hindley

#include <sstream>
#include <iostream>
#include <fstream>
#include <cstdint>
#include "xmparc.h"

static XMPFUNC_FILE *xmpffile;
static XMPFUNC_MISC *xmpfmisc;

#define ASCII_TO_NUMBER(num) ((num)-48) //Converts an ascii digit to the corresponding number (assuming it is an ASCII digit)

typedef struct {
	int pos;
	int max;
} TARHEAD;

static uint64_t decodeTarOctal(char* data, size_t size = 12) {
    unsigned char* currentPtr = (unsigned char*) data + size;
    uint64_t sum = 0;
    uint64_t currentMultiplier = 1;
    //Skip everything after the last NUL/space character
    //In some TAR archives the size field has non-trailing NULs/spaces, so this is neccessary
    unsigned char* checkPtr = currentPtr; //This is used to check where the last NUL/space char is
    for (; checkPtr >= (unsigned char*) data; checkPtr--) {
        if ((*checkPtr) == 0 || (*checkPtr) == ' ') {
            currentPtr = checkPtr - 1;
        }
    }
    for (; currentPtr >= (unsigned char*) data; currentPtr--) {
        sum += ASCII_TO_NUMBER(*currentPtr) * currentMultiplier;
        currentMultiplier *= 8;
    }
    return sum;
}

typedef struct {
	char name[100];
	char mode[8];
	char ownerid[8];
	char groupid[8];
	char size[12];
	char modified[12];
	char checksum[8];
	char type[1];
	char linked[100];
	char ustar[6];
	char version[2];
	char ownername[32];
	char groupname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
	char padding[12];

    /**
     * @return true if and only if
     */
    bool isUSTAR() {
        return (memcmp("ustar", ustar, 5) == 0);
    }

    /**
     * @return The filesize in bytes
     */
    size_t getFileSize() {
        return decodeTarOctal(size);
    }
	
	/**
     * Return true if and only if the header checksum is correct
     * @return
     */
    bool checkChecksum() {
        //We need to set the checksum to zer
        char originalChecksum[8];
        memcpy(originalChecksum, checksum, 8);
        memset(checksum, ' ', 8);
        //Calculate the checksum -- both signed and unsigned
        int64_t unsignedSum = 0;
        int64_t signedSum = 0;
        for (int i = 0; i < sizeof (TARENTRY); i++) {
            unsignedSum += ((unsigned char*) this)[i];
            signedSum += ((signed char*) this)[i];
        }
        //Copy back the checksum
        memcpy(checksum, originalChecksum, 8);
        //Decode the original checksum
        uint64_t referenceChecksum = decodeTarOctal(originalChecksum);
        return (referenceChecksum == unsignedSum || referenceChecksum == signedSum);
    }
} TARENTRY;

// check if a file can be handled by this plugin
// more thorough checks can be saved for the GetFileList and DecompressFile functions
BOOL WINAPI ARC_CheckFile(XMPFILE file)
{
	TARENTRY e;
	if (xmpffile->Read(file,&e,sizeof(e))!=sizeof(e));
	return e.isUSTAR() && e.checkChecksum();
}

// get file list
// return a series of NULL-terminated entries
char *WINAPI ARC_GetFileList(XMPFILE file)
{
	TARHEAD h;
	char *fl=NULL;
	int p=0;
	h.pos = 0;
	h.max = xmpffile->GetSize(file);
	do {
		TARENTRY e;
		if (!xmpffile->Seek(file,h.pos)) break;
		if (!xmpffile->Read(file,&e,512)) break;
		if (e.name[0]) {
			fl=(char*)xmpfmisc->ReAlloc(fl,p+100+2); // allocate buffer (XMPlay will free it)
			memcpy(fl+p,e.name,100);
			fl[p+100]=0;
			p+=strlen(fl+p)+1;
			fl[p]=0;
		}
		h.pos += 512;
		h.pos += (int)ceil(e.getFileSize() / 512.0) * 512;
	} while (h.pos < h.max && h.pos > 0);

	return fl;
}

// extract a file
// in: len=max amount wanted
// out: len=amount delivered
void *WINAPI ARC_DecompressFile(XMPFILE file, const char *entry, DWORD *len)
{
	TARHEAD h;
	if (strlen(entry)>100) return NULL;
	h.pos = 0;
	h.max = xmpffile->GetSize(file);
	do {
		TARENTRY e;
		if (!xmpffile->Seek(file,h.pos)) break;
		if (!xmpffile->Read(file,&e,512)) break;
		if (e.name[0]) {
			if (!strncmp(e.name,entry,100)) {
				if (!xmpffile->Seek(file,h.pos+512)) break;
				DWORD wanted=min(e.getFileSize(),*len); // limit data to requested amount
				void *buf=xmpfmisc->Alloc(wanted); // allocate buffer (XMPlay will free it)
				if (!buf) break;
				*len=xmpffile->Read(file,buf,wanted);
				return buf; // return pointer to extracted file
			}
		}
		h.pos += 512;
		h.pos += (int)ceil(e.getFileSize() / 512.0) * 512;
	} while (h.pos < h.max && h.pos > 0);

	return NULL;
}

static void WINAPI ARC_About(HWND win)
{
	MessageBox(win,
		"XMPlay TAR plugin (0.0.2.0)\nCopyright (c) 2021 Nathan Hindley\n\nThis plugin allows XMPlay to load/play files packed with UStar tar.\n\nFREE FOR USE WITH XMPLAY",
		"About...",
		MB_ICONINFORMATION);
}

// plugin interface
static XMPARC xmparc={
	XMPARC_FLAG_CONFIG,
	"tar packed files\0tar",
	ARC_CheckFile,
	ARC_GetFileList,
	ARC_DecompressFile,
    ARC_About,
};

#ifdef __cplusplus
extern "C"
#endif
const XMPARC *WINAPI XMPARC_GetInterface(DWORD face, InterfaceProc faceproc)
{
	if (face!=XMPARC_FACE) return NULL;
	xmpffile=(XMPFUNC_FILE*)faceproc(XMPFUNC_FILE_FACE);
	xmpfmisc=(XMPFUNC_MISC*)faceproc(XMPFUNC_MISC_FACE);
	return &xmparc;
}

BOOL WINAPI DllMain(HINSTANCE hDLL, DWORD reason, LPVOID reserved)
{
	switch (reason) {
		case DLL_PROCESS_ATTACH:
			DisableThreadLibraryCalls(hDLL);
			break;
	}
	return TRUE;
}
